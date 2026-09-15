/*
 * ControlNoteShared.h - the vocabulary the note-EDITING verbs of the 0.3.0
 *                       note/scale wave share (board task #648, feature-list
 *                       rows 11 and 66).
 *
 * ONE definition for the three translation units that use it
 * (ControlCommandsNoteRandom.cpp, ControlCommandsNoteSlide.cpp and
 * ControlCommandsNoteTransform.cpp): the scope a verb edits and the notes that
 * scope resolves to. It is a header rather than a copy in each file because a
 * second copy of a helper is exactly the drift this split exists to prevent - the
 * rule ControlGrooveSupport.h, ControlVcaShared.h and
 * ControlCommandsSessionShared.h already follow.
 *
 * The clip-side half of the note vocabulary (resolveMidiClip, resolveNote,
 * noteState, snapshotNotes, readTicks) is NOT here: it is the shared surface of
 * every note and groove verb and lives in ControlEdit.h / ControlGrooveSupport.h.
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

#ifndef LMMS_CONTROL_NOTE_SHARED_H
#define LMMS_CONTROL_NOTE_SHARED_H

#include <QJsonObject>
#include <QString>

#include "ControlVocabulary.h" // the shared schema + id vocabulary
#include "Note.h"

namespace lmms
{

class MidiClip;
struct ControlResult;

namespace control
{

//! The largest value the schema subset's integer carries; the bound a seed and
//! every tick-shaped argument travel within (ControlCommandsGrooveEdit's own).
constexpr int kMaxSchemaInteger = 2147483647;

/*! Which of a clip's notes a verb edits.
 *
 *  `note.select` chooses the selection and the piano roll edits the same set, so
 *  "selection" is the scope an agent uses to transpose four notes out of a chord
 *  without touching the rest.
 */
enum class NoteScope
{
	Clip,      //!< every note of the clip
	Selection  //!< the notes note.select chose, in the clip's own order
};

/*! Reads the optional `scope` argument; absent means the whole clip. Anything
 *  else is refused typed rather than defaulted, because silently editing the
 *  whole clip when a selection was asked for is the kind of success a caller
 *  cannot detect. */
bool readScope(const QJsonObject& args, NoteScope* out, ControlResult* error);

/*! The notes a verb edits, as the clip's own Note pointers.
 *
 *  Built fresh per call (a vector of pointers, never copies of the notes), because
 *  the clip's list is re-sorted after an edit and a stale iterator would then
 *  point at the wrong element. An index the selection no longer holds is skipped
 *  rather than refused: a selection can outlive the notes it named, and skipping
 *  it edits exactly the notes that are still there.
 */
NoteVector scopeNotes(const MidiClip& clip, NoteScope scope, const QString& clipIdString);

//! The wire name of a scope ("clip" / "selection"), so every verb reports the
//! same string a caller passed in.
QString scopeName(NoteScope scope);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_NOTE_SHARED_H
