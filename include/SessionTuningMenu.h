/*
 * SessionTuningMenu.h - Edit > Session Tuning: load a Scala scale or keymap, reset, publish over MTS-ESP
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

#ifndef LMMS_GUI_SESSION_TUNING_MENU_H
#define LMMS_GUI_SESSION_TUNING_MENU_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "No GUI reaches the table": a "Session Tuning" submenu of @a menu, re-read from
 *  mts.get_state each time it opens - a line naming the table's state and source; Load Scala Scale...
 *  and Load Keymap... (a file picker, then mts.load_scale / mts.load_keymap); Reset to 12-TET
 *  (mts.reset); and Publish as MTS-ESP Master (checkable, mts.master_set; disabled where the build has
 *  no MTS-ESP library). */
LMMS_EXPORT QMenu* addSessionTuningMenu(QMenu* menu);

//! The line for a mts.get_state reply.
LMMS_EXPORT QString sessionTuningStatusText(const QJsonObject& state);

//! Tests replace the file picker (default: a file dialog filtered by @a filter; empty cancels).
LMMS_EXPORT void setSessionTuningPicker(std::function<QString(const QString& filter)> picker);

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_TUNING_MENU_H
