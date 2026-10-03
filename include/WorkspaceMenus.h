/*
 * WorkspaceMenus.h - the 0.5.0 W3 customisation and access entries in the menus
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

#ifndef LMMS_GUI_WORKSPACE_MENUS_H
#define LMMS_GUI_WORKSPACE_MENUS_H

#include "lmms_export.h"

class QMenu;
class QMenuBar;

namespace lmms::gui
{

/*! 0.5.0 W3 L13 / L14: where a person finds the lane's commands. Every entry runs a registered command
 *  (CommandForm / CommandListDialog), so the menu and the socket are one path (SPEC A11). */

//! The Window menu and the Help menu's additions; installed once, by Shell when it is first shown.
LMMS_EXPORT void installWorkspaceMenus(QMenuBar* bar);

//! The Customise menu's theme, palette, density and settings entries (ShellCommandMenus::populateCustomise
//! ends with it).
LMMS_EXPORT void populateCustomiseExtras(QMenu* menu);

} // namespace lmms::gui

#endif // LMMS_GUI_WORKSPACE_MENUS_H
