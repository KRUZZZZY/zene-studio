/*
 * RecordingRecoveryMenu.h - File > Recover Recordings: place or dismiss a capture an abnormal exit left
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

#ifndef LMMS_GUI_RECORDING_RECOVERY_MENU_H
#define LMMS_GUI_RECORDING_RECOVERY_MENU_H

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "Recording crash recovery is journalling and recovery, not an import": a
 *  "Recover Recordings" submenu of @a menu, rebuilt from record.recovery_get_state each time it
 *  opens - a line with the count, and per interrupted take a submenu with Place on a New Sample
 *  Track (record.recovery_restore, then the recovered file is loaded into a clip at bar 1 of a new
 *  sample track - the import the command itself does not make) and Dismiss (record.recovery_discard:
 *  the journal goes, the audio file stays). */
LMMS_EXPORT QMenu* addRecordingRecoveryMenu(QMenu* menu);

//! The item text for one take of a record.recovery_get_state reply.
LMMS_EXPORT QString recoveredTakeText(const QJsonObject& take);

} // namespace lmms::gui

#endif // LMMS_GUI_RECORDING_RECOVERY_MENU_H
