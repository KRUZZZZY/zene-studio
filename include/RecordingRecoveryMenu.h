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

#include <functional>

#include <QJsonArray>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

class QJsonObject;
class QMenu;
class QWidget;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "Recording crash recovery is journalling and recovery, not an import": a
 *  "Recover Recordings" submenu of @a menu, rebuilt from record.recovery_get_state each time it
 *  opens - a line with the count, and per interrupted take a submenu with Place on a New Sample
 *  Track (record.recovery_place: a new sample track with the recovered file in a clip at bar 1, one undo step) and Dismiss (record.recovery_discard:
 *  the journal goes, the audio file stays). */
LMMS_EXPORT QMenu* addRecordingRecoveryMenu(QMenu* menu);

//! What the startup offer did.
enum class StartupRecoveryChoice { NothingToOffer, Place, Later, QueryFailed };

/*! The reply of record.recovery_get_state: \a takes receives the interrupted takes the command
 *  lists (empty when there are none). A FAILED query leaves \a takes empty and returns the typed
 *  failure - an empty list from a failed query means the takes are UNKNOWN, not absent. */
LMMS_EXPORT ControlResult interruptedTakes(QJsonArray* takes);

/*! Q050-046: the startup offer. Asks \a ask (default: a message box) ONLY when record.recovery_get_state
 *  reports an interrupted take; an unattended run never gets a dialog (stderr line, journals left).
 *  Choosing Place runs record.recovery_place for every take, each one undo step.
 *  A FAILED query is never "nothing to offer": the failure is reported (always logged, and shown to
 *  a human unless it is the transient 'the model is not addressable yet' refusal) and the offer
 *  returns StartupRecoveryChoice::QueryFailed - the takes are unknown, not absent. */
LMMS_EXPORT StartupRecoveryChoice offerRecordingRecovery(QWidget* parent,
	const std::function<StartupRecoveryChoice(const QJsonArray&)>& ask);

//! The same, deferred to the first turn of the event loop; main.cpp calls this once the project exists.
LMMS_EXPORT void offerRecordingRecoveryAtStartup(QWidget* parent);

//! The item text for one take of a record.recovery_get_state reply.
LMMS_EXPORT QString recoveredTakeText(const QJsonObject& take);

} // namespace lmms::gui

#endif // LMMS_GUI_RECORDING_RECOVERY_MENU_H
