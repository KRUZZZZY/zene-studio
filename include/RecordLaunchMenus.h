/*
 * RecordLaunchMenus.h - the Record and Launcher menus of the menu bar
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
#ifndef LMMS_GUI_RECORD_LAUNCH_MENUS_H
#define LMMS_GUI_RECORD_LAUNCH_MENUS_H

#include "lmms_export.h"

class QMenuBar;

namespace lmms::gui
{

/*! 0.5.0 L03 / L02: two menus of the menu bar, every entry a registered command, so each can be given
 *  keys with shortcuts.set like any menu action:
 *  - Record: record, count-in and pre-roll, the record mode, arm and disarm every audio track, cycle
 *    the take at the play position on the selected track (ED-168);
 *  - Launcher: launch scenes 1-8 and stop all from the keyboard, and the controller bindings - connect,
 *    bind the next note, list, unbind (WF-069). */
LMMS_EXPORT void installRecordLaunchMenus(QMenuBar* bar);

} // namespace lmms::gui

#endif // LMMS_GUI_RECORD_LAUNCH_MENUS_H
