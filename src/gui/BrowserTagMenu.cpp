/*
 * BrowserTagMenu.cpp - the file browser's Tags menu: a file's tags, added and removed
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "BrowserTagMenu.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>

#include "BrowserCatalog.h"
#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

BrowserTagPrompt& prompt()
{
	static BrowserTagPrompt s_prompt;
	return s_prompt;
}

BrowserCategoryPrompt& categoryPrompt()
{
	static BrowserCategoryPrompt s_prompt;
	return s_prompt;
}

std::optional<QString> askForTag()
{
	if (prompt()) { return prompt()(); }
	bool ok = false;
	const QString text = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("New tag"),
		QMenu::tr("Tag:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !text.isEmpty() ? std::optional<QString>(text) : std::nullopt;
}

std::optional<QString> askForCategory()
{
	if (categoryPrompt()) { return categoryPrompt()(); }
	bool ok = false;
	const QString text = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("New category"),
		QMenu::tr("Category:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !text.isEmpty() ? std::optional<QString>(text) : std::nullopt;
}

//! Runs the add/remove command. The result is returned so a caller can tell the user when the
//! store refused or failed to persist: a tag that was not saved must not look like one that was.
ControlResult setTag(const QString& path, const QString& tag, bool on)
{
	return ControlRegistry::instance()->invoke(on ? QStringLiteral("browser.tag.add") : QStringLiteral("browser.tag.remove"),
		{{QStringLiteral("path"), path}, {QStringLiteral("tag"), tag}});
}

//! Puts every checkable entry of the Category submenu back the way the STORE holds it (not the
//! way the click left it): the submenu's checks are a read-back of the one-category-per-file
//! state, and a failed write changes none of it.
void syncCategoryChecks(QMenu* categories, const QString& path)
{
	const QString current = BrowserTagStore::instance().categoryOf(path);
	for (QAction* action : categories->actions())
	{
		if (!action->isCheckable()) { continue; }
		const QString entry = action->data().toString();
		const bool checked = entry == QLatin1String("browser.category.clear")
			? current.isEmpty()
			: QString::compare(current, action->text(), Qt::CaseInsensitive) == 0;
		const QSignalBlocker block(action);
		action->setChecked(checked);
	}
}

} // namespace

void setBrowserTagPrompt(BrowserTagPrompt replacement)
{
	prompt() = std::move(replacement);
}

void setBrowserCategoryPrompt(BrowserCategoryPrompt replacement)
{
	categoryPrompt() = std::move(replacement);
}

void addBrowserTagMenu(QMenu* menu, const QString& path)
{
	QMenu* tags = menu->addMenu(QMenu::tr("Tags"));
	const QJsonArray known = ControlRegistry::instance()->invoke(QStringLiteral("browser.tags"), QJsonObject{})
		.result.value(QStringLiteral("tags")).toArray();
	// Each file's own tags are read from the store under the key the commands use (browser.tags gives
	// each tag's file COUNT, not the files). Reading only: every change goes through a command.
	for (const QJsonValue& value : known)
	{
		const QString tag = value.toObject().value(QStringLiteral("tag")).toString();
		QAction* action = tags->addAction(tag);
		action->setCheckable(true);
		// The store compares tags case-insensitively, so ask it rather than QStringList::contains.
		action->setChecked(BrowserTagStore::instance().has(browserCanonicalKey(path), tag));
		action->setData(QStringLiteral("browser.tag.add"));
		QObject::connect(action, &QAction::toggled, tags, [path, tag, action](bool on) {
			const ControlResult result = setTag(path, tag, on);
			if (result.ok) { return; }
			// The tag was not persisted: undo the check the click just made, and say why.
			const QSignalBlocker block(action);  // no recursion: the revert is not another edit
			action->setChecked(!on);
			action->setStatusTip(result.errorMessage);
		});
	}
	if (!known.isEmpty()) { tags->addSeparator(); }
	QAction* fresh = tags->addAction(QMenu::tr("New tag..."));
	fresh->setData(QStringLiteral("browser.tag.add"));
	QObject::connect(fresh, &QAction::triggered, tags, [path, fresh] {
		const std::optional<QString> tag = askForTag();
		if (!tag) { return; }
		const ControlResult result = setTag(path, *tag, true);
		fresh->setStatusTip(result.ok ? QString() : result.errorMessage);
	});
}

void addBrowserCategoryMenu(QMenu* menu, const QString& path)
{
	QMenu* categories = menu->addMenu(QMenu::tr("Category"));
	// The vocabulary is the library's own (browser.categories reads the store): a category is
	// listed because a file carries it. Reading only: every change goes through a command.
	const QJsonArray known = ControlRegistry::instance()->invoke(QStringLiteral("browser.categories"), QJsonObject{})
		.result.value(QStringLiteral("categories")).toArray();
	// One category per file, so the entries are exclusive: "No category" is the empty state.
	QAction* none = categories->addAction(QMenu::tr("No category"));
	none->setCheckable(true);
	none->setData(QStringLiteral("browser.category.clear"));
	QList<QAction*> choices;
	for (const QJsonValue& value : known)
	{
		QAction* action = categories->addAction(value.toObject().value(QStringLiteral("category")).toString());
		action->setCheckable(true);
		action->setData(QStringLiteral("browser.category.set"));
		choices.append(action);
	}
	// The checks are the store's read-back; the connections below put them back there after
	// every click, so what is ticked is always what the file carries.
	syncCategoryChecks(categories, path);
	for (QAction* action : choices)
	{
		QObject::connect(action, &QAction::triggered, categories, [path, categories, action] {
			const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("browser.category.set"),
				{{QStringLiteral("path"), path}, {QStringLiteral("category"), action->text()}});
			// One category per file: the entry that was ticked before is unticked now, and a
			// failed write ticks nothing new - the store decides, both ways.
			syncCategoryChecks(categories, path);
			if (!result.ok) { action->setStatusTip(result.errorMessage); }
		});
	}
	QObject::connect(none, &QAction::triggered, categories, [path, categories, none] {
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("browser.category.clear"),
			{{QStringLiteral("path"), path}});
		syncCategoryChecks(categories, path);
		if (!result.ok) { none->setStatusTip(result.errorMessage); }
	});
	categories->addSeparator();
	QAction* fresh = categories->addAction(QMenu::tr("New category..."));
	fresh->setData(QStringLiteral("browser.category.set"));
	QObject::connect(fresh, &QAction::triggered, categories, [path, categories, fresh] {
		const std::optional<QString> category = askForCategory();
		if (!category) { return; }
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("browser.category.set"),
			{{QStringLiteral("path"), path}, {QStringLiteral("category"), *category}});
		fresh->setStatusTip(result.ok ? QString() : result.errorMessage);
		if (result.ok) { syncCategoryChecks(categories, path); }
	});
}

} // namespace lmms::gui
