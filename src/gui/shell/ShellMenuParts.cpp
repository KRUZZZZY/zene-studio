/*
 * ShellMenuParts.cpp - the entries the shell's command menus are built from
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
#include "ShellMenuParts.h"

#include <QMenu>

#include "CommandForm.h"
#include "CommandListDialog.h"

namespace lmms::gui::menuparts
{

QString tr(const char* text)
{
	return QObject::tr(text);
}

//! An entry running @a id with @a fixed - through its form when @a ask, with @a defaults.
QAction* add(QMenu* menu, const char* label, const char* id, const QJsonObject& fixed, bool ask,
	const QJsonObject& defaults, bool showResult)
{
	const QString title = tr(label).remove(QStringLiteral("..."));
	return addCommandAction(menu, tr(label), QLatin1String(id), [id, fixed, ask, defaults, showResult, title] {
		return CommandCall{QLatin1String(id), fixed, defaults, title, ask, showResult};
	});
}

//! A submenu of @a id's values for @a key (enum or On/Off), each run at once with @a fixed.
void choices(QMenu* menu, const char* label, const char* id, const char* key, const QJsonObject& fixed)
{
	addCommandChoices(menu, tr(label), QLatin1String(id), QLatin1String(key),
		[id, fixed] { return CommandCall{QLatin1String(id), fixed, {}, QString(), false}; });
}

//! An entry opening the list surface @a make builds.
void list(QMenu* menu, const char* label, std::function<ListSpec()> make)
{
	QAction* action = menu->addAction(tr(label));
	// The list command it reads is what the entry declares (SPEC A11's agent-surface sweep).
	action->setProperty("controlCommand", make().listId);
	QObject::connect(action, &QAction::triggered, menu, [menu, make = std::move(make)] {
		QWidget* host = menu;
		while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
		showCommandList(host, make());
	});
}

QJsonObject with(QJsonObject base, const char* key, const QJsonValue& value)
{
	base.insert(QLatin1String(key), value);
	return base;
}


} // namespace lmms::gui::menuparts
