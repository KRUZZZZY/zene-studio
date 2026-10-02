/*
 * MenuBarExtras.h - the menu bar's own Zene menus: Navigate and Macros
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

#ifndef LMMS_GUI_MENU_BAR_EXTRAS_H
#define LMMS_GUI_MENU_BAR_EXTRAS_H

#include "lmms_export.h"

class QMenu;
class QMenuBar;

namespace lmms::gui
{

//! Put @a menu in @a bar before its last menu (Help), unless an object of that name is already
//! there; answers the menu that is in the bar.
LMMS_EXPORT QMenu* insertBeforeHelp(QMenuBar* bar, QMenu* menu);

/*! The Navigate menu (0.5.0 L13): zoom back, fit the song / loop / selection / tracks, the tool
 *  menu at the pointer, zoom in and out - each a registry command. In the menu bar so each is in
 *  the palette and takes a shortcut (ShortcutMap); none comes bound, the user binds them. */
LMMS_EXPORT QMenu* installNavigateMenu(QMenuBar* bar);

} // namespace lmms::gui

#endif // LMMS_GUI_MENU_BAR_EXTRAS_H
