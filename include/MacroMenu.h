/*
 * MacroMenu.h - the menu bar's Macros menu: one action per saved macro
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

#ifndef LMMS_GUI_MACRO_MENU_H
#define LMMS_GUI_MACRO_MENU_H

#include <QMenu>

#include "lmms_export.h"

class QMenuBar;

namespace lmms::gui
{

/*! WF-114: a macro is a named run of registry commands (macro.save); this menu has one action
 *  per macro - each runs macro.run - so a macro is in the palette and takes a shortcut like any
 *  menu action (ShortcutMap). It is rebuilt when the macros change, never on show, so a key
 *  bound to a macro's action stays bound. */
class LMMS_EXPORT MacroMenu : public QMenu
{
	Q_OBJECT
public:
	explicit MacroMenu(QWidget* parent = nullptr);

	//! Re-read the saved macros (ui/macros) and rebuild the actions.
	void refresh();

	//! The main window's Macros menu, added to @a bar before Help the first time.
	static MacroMenu* install(QMenuBar* bar);
};

} // namespace lmms::gui

#endif // LMMS_GUI_MACRO_MENU_H
