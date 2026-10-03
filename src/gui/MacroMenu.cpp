/*
 * MacroMenu.cpp - the menu bar's Macros menu: one action per saved macro
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

#include "MacroMenu.h"

#include <QMenuBar>

#include "CommandForm.h"
#include "CommandLists.h"
#include "MenuBarExtras.h"
#include "UiConfigStore.h"

namespace lmms::gui
{

MacroMenu::MacroMenu(QWidget* parent) :
	QMenu(tr("&Macros"), parent)
{
	setObjectName(QStringLiteral("macroMenu"));
	refresh();
}

void MacroMenu::refresh()
{
	clear();
	const QJsonObject macros = uistore::read(QStringLiteral("macros")).value(QStringLiteral("macros")).toObject();
	for (auto it = macros.begin(); it != macros.end(); ++it)
	{
		const QString name = it.key();
		// A '&' in the name is shown as itself, not taken as a mnemonic (the run keeps the name as saved).
		addCommandAction(this, QString(name).replace(QLatin1Char('&'), QStringLiteral("&&")), QStringLiteral("macro.run"), [name] {
			return CommandCall{QStringLiteral("macro.run"), {{QStringLiteral("name"), name}}, {}, name, false};
		});
	}
	if (!macros.isEmpty()) { addSeparator(); }
	// 0.5.0 ED-056: record what you do as a macro.
	addCommandAction(this, tr("Record a Macro..."), QStringLiteral("macro.record_start"),
		[] { return CommandCall{QStringLiteral("macro.record_start"), {}, {{QStringLiteral("name"), QStringLiteral("My macro")}}, tr("Record a Macro"), true}; });
	addCommandAction(this, tr("Stop Recording the Macro"), QStringLiteral("macro.record_stop"),
		[] { return CommandCall{QStringLiteral("macro.record_stop"), {}, {}, QString(), false}; });
	QAction* edit = addAction(tr("Edit Macros..."));
	edit->setProperty("controlCommand", QStringLiteral("macro.list"));
	connect(edit, &QAction::triggered, this, [this] { showCommandList(parentWidget(), CommandLists::macros()); });
}

MacroMenu* MacroMenu::install(QMenuBar* bar)
{
	if (auto* existing = bar->window()->findChild<MacroMenu*>()) { return existing; }
	return static_cast<MacroMenu*>(insertBeforeHelp(bar, new MacroMenu(bar->window())));
}

} // namespace lmms::gui
