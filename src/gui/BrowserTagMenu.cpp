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

std::optional<QString> askForTag()
{
	if (prompt()) { return prompt()(); }
	bool ok = false;
	const QString text = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("New tag"),
		QMenu::tr("Tag:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !text.isEmpty() ? std::optional<QString>(text) : std::nullopt;
}

void setTag(const QString& path, const QString& tag, bool on)
{
	ControlRegistry::instance()->invoke(on ? QStringLiteral("browser.tag.add") : QStringLiteral("browser.tag.remove"),
		{{QStringLiteral("path"), path}, {QStringLiteral("tag"), tag}});
}

} // namespace

void setBrowserTagPrompt(BrowserTagPrompt replacement)
{
	prompt() = std::move(replacement);
}

void addBrowserTagMenu(QMenu* menu, const QString& path)
{
	QMenu* tags = menu->addMenu(QMenu::tr("Tags"));
	const QJsonArray known = ControlRegistry::instance()->invoke(QStringLiteral("browser.tags"), QJsonObject{})
		.result.value(QStringLiteral("tags")).toArray();
	// The file's own tags, read from the store under the key the commands use (browser.tags gives
	// each tag's file COUNT, not the files). Reading only: every change goes through a command.
	const QStringList mine = BrowserTagStore::instance().tagsOf(browserCanonicalKey(path));
	for (const QJsonValue& value : known)
	{
		const QString tag = value.toObject().value(QStringLiteral("tag")).toString();
		QAction* action = tags->addAction(tag);
		action->setCheckable(true);
		action->setChecked(mine.contains(tag));
		action->setData(QStringLiteral("browser.tag.add"));
		QObject::connect(action, &QAction::toggled, tags, [path, tag](bool on) { setTag(path, tag, on); });
	}
	if (!known.isEmpty()) { tags->addSeparator(); }
	QAction* fresh = tags->addAction(QMenu::tr("New tag..."));
	fresh->setData(QStringLiteral("browser.tag.add"));
	QObject::connect(fresh, &QAction::triggered, tags, [path] {
		if (const std::optional<QString> tag = askForTag()) { setTag(path, *tag, true); }
	});
}

} // namespace lmms::gui
