/*
 * VisibilitySetMenu.cpp - the song editor's Views menu: named track-visibility sets
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

#include "VisibilitySetMenu.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

VisibilitySetPrompt& prompt()
{
	static VisibilitySetPrompt s_prompt;
	return s_prompt;
}

std::optional<QString> askForName()
{
	if (prompt()) { return prompt()(); }
	bool ok = false;
	const QString name = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Save view"),
		QMenu::tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !name.isEmpty() ? std::optional<QString>(name) : std::nullopt;
}

ControlResult run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

QAction* item(QMenu* menu, const QString& text, const char* command)
{
	QAction* action = menu->addAction(text);
	action->setData(QString::fromLatin1(command));
	return action;
}

void saveVisible()
{
	const std::optional<QString> name = askForName();
	if (!name) { return; }
	QJsonArray shown;
	for (const Track* track : Engine::getSong()->tracks())
	{
		if (track->isVisible()) { shown.append(control::trackIdOf(track)); }
	}
	run("track.visibility_set_save", {{QStringLiteral("name"), *name}, {QStringLiteral("tracks"), shown}});
}

} // namespace

void setVisibilitySetPrompt(VisibilitySetPrompt replacement)
{
	prompt() = std::move(replacement);
}

void populateVisibilitySetMenu(QMenu* menu)
{
	menu->clear();
	const QJsonObject listed = run("track.visibility_set_list").result;
	const QString active = listed.value(QStringLiteral("active")).toString();
	QAction* all = item(menu, QMenu::tr("All tracks"), "track.visibility_show_all");
	all->setCheckable(true);
	all->setChecked(active.isEmpty());
	QObject::connect(all, &QAction::triggered, menu, [] { run("track.visibility_show_all"); });
	QStringList names;
	for (const QJsonValue& value : listed.value(QStringLiteral("sets")).toArray())
	{
		names << value.toObject().value(QStringLiteral("name")).toString();
	}
	for (const QString& name : names)
	{
		QAction* apply = item(menu, name, "track.visibility_set_apply");
		apply->setCheckable(true);
		apply->setChecked(name == active);
		QObject::connect(apply, &QAction::triggered, menu, [name] { run("track.visibility_set_apply", {{QStringLiteral("name"), name}}); });
	}
	menu->addSeparator();
	QObject::connect(item(menu, QMenu::tr("Save visible tracks as..."), "track.visibility_set_save"),
		&QAction::triggered, menu, [] { saveVisible(); });
	QMenu* remove = menu->addMenu(QMenu::tr("Remove"));
	remove->setEnabled(!names.isEmpty());
	for (const QString& name : names)
	{
		QObject::connect(item(remove, name, "track.visibility_set_remove"), &QAction::triggered, menu,
			[name] { run("track.visibility_set_remove", {{QStringLiteral("name"), name}}); });
	}
}

} // namespace lmms::gui
