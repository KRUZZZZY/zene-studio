/*
 * ControlWindowCommands.h - the `window.*` group: the editors' windows as commands
 *
 * The main window's seven editor toggles (View menu Ctrl+1..7 and the window
 * toolbar) were the largest block of the agent-surface grandfather list: actions
 * an agent could not reach and the command palette could not list. They are now
 * one registered command, `window.toggle {editor}`, and the menu items and toolbar
 * buttons DISPATCH THROUGH it (the plan of record's item 0a, "GUI actions become
 * registered commands"), so the interface and the socket are one path.
 * `window.get_state` reads which editors are showing.
 *
 * Interface state, not project state: nothing is journalled and nothing is
 * undone (the `not_mutating` rows in ControlReversibilityTablePassive.cpp, the
 * same rule `midi.learn_toggle` follows). Neither command needs the engine. In a
 * process with no interface (the CLI renderer) both answer `requires`, typed.
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
 */

#ifndef LMMS_CONTROL_WINDOW_COMMANDS_H
#define LMMS_CONTROL_WINDOW_COMMANDS_H

#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;

LMMS_EXPORT void registerWindowCommands(ControlRegistry& registry);

//! The `editor` values window.toggle accepts, in the menu's Ctrl+1..7 order.
LMMS_EXPORT QStringList windowEditorNames();

//! What a View-menu item or toolbar button does: window.toggle through the
//! registry, exactly as an agent on the socket would.
LMMS_EXPORT void dispatchWindowToggle(const QString& editor);

} // namespace lmms

#endif // LMMS_CONTROL_WINDOW_COMMANDS_H
