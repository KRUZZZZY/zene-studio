/*
 * ControlNoteRecord.h - a live recording take: which notes a MIDI keyboard is holding, and
 *                        whether the take's undo step is already open
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

#ifndef LMMS_CONTROL_NOTE_RECORD_H
#define LMMS_CONTROL_NOTE_RECORD_H

#include <QString>
#include <QVector>

#include "lmms_export.h"

namespace lmms::control
{

/*! One live recording take per clip: the keys a MIDI keyboard is holding, the note each one
 *  opened, and whether the take's journal checkpoint is already taken.
 *
 *  THIS IS EDIT STATE, NOT PROJECT STATE. Nothing here is saved: the notes it opens are ordinary
 *  `Note`s in the clip, and this map only remembers which of them are still held (so a note-off
 *  can close the right one) and whether the take has already taken its one MidiClip journal
 *  checkpoint. A project made before recording existed opens and renders exactly as it did.
 *
 *  ONE CHECKPOINT PER TAKE. The classic piano roll takes one `addJournalCheckPoint` per recording
 *  pass (src/gui/editors/PianoRoll.cpp:4410) and never one per note; this is the same rule, so one
 *  `control.undo` takes a whole take back rather than the last note of it.
 *
 *  THREAD: read and written only on the thread that runs control commands (the GUI thread, where
 *  the control server, the command handlers and the note editor live). The MIDI edges arrive there
 *  over the editor's queued connection, and the transport's `stopped` signal is emitted from
 *  `Song::stop()` on the same thread. The audio thread never reads or writes it, so there is no
 *  lock, no atomic and no allocation on that path.
 */

//! True when @a clipId's take has already taken its journal checkpoint.
LMMS_EXPORT bool recordTakeOpen(const QString& clipId);

//! Open @a clipId's take. The caller takes the clip's journal checkpoint; this only remembers that
//! it has been taken, so the next note of the same take does not take another.
LMMS_EXPORT void beginRecordTake(const QString& clipId);

//! End @a clipId's take: the next note takes a new checkpoint, and the next `control.undo` takes
//! back only what that take wrote. Held notes are forgotten (the caller closes them first).
LMMS_EXPORT void endRecordTake(const QString& clipId);

//! Remember that @a key's note @a noteId is held in @a clipId, and that a take is open.
LMMS_EXPORT void holdRecordNote(const QString& clipId, int key, const QString& noteId);

//! The note id @a key is holding in @a clipId, or an empty string.
LMMS_EXPORT QString heldRecordNote(const QString& clipId, int key);

//! Forget the note @a key holds in @a clipId (the caller has closed it).
LMMS_EXPORT void releaseRecordNote(const QString& clipId, int key);

//! The keys @a clipId is holding, in the order they went down.
LMMS_EXPORT QVector<int> heldRecordKeys(const QString& clipId);

//! Forget everything about @a clipId - its take went away with the clip.
LMMS_EXPORT void forgetRecordClip(const QString& clipId);

/*! End every open take. `control.undo` and `control.redo` call it before they unwind a step, because
 *  undoing the step a take was writing into leaves the take's checkpoint gone - notes written after
 *  it would then belong to NO undo step, and one `control.undo` would take back an older edit while
 *  the new note stayed. Ending the take makes the next note start its own checkpoint, so a take is
 *  never written outside a step it owns.
 */
LMMS_EXPORT void endAllRecordTakes();

} // namespace lmms::control

#endif // LMMS_CONTROL_NOTE_RECORD_H
