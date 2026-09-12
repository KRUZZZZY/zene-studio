/*
 * ControlEdit.h - shared helpers for the notes / clips / tracks command groups
 *                 of the agent control surface (SPEC-zene-studio.md A11-A16).
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

#ifndef LMMS_CONTROL_EDIT_H
#define LMMS_CONTROL_EDIT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "ControlVocabulary.h" // the shared schema + id vocabulary
#include "Track.h" // Track::Type, for trackTypeNameOf() and the ClipRef ids
#include "lmms_export.h"

namespace lmms
{

class Clip;
class MidiClip;
class Note;
struct ControlResult;

namespace control
{

// The JSON-schema helpers and the id formatters (objectSchema,
// stringProperty, clipId, noteId, ...) live in ControlVocabulary.h - one
// definition for the whole surface. See that header's comment for why.

// ---------------------------------------------------------------------------
// Stable ids (AGENT-TOOLING.md #4): trk-<n> / clip-<n> / note-<n>.
//
// trk-<n>   index of the track in the song container.
// clip-<n>  ordinal of the clip in ARRANGEMENT order: tracks in song order, and
//           inside a track the clips sorted by start position (ties keep the
//           track's own order). One ordinal space for the whole song, so a clip
//           id is unambiguous without naming its track.
// note-<n>  index of the note in its clip's note list (the list addNote() keeps
//           sorted by position, and rearrangeAllNotes() re-sorts after an edit).
//
// All three are index-derived, like the mixer lane's ch-<n>: they are stable for
// the state a get_state returned them for, and are not persisted in the project
// file (the project's own serialization has no id attribute for clips/notes).
// ---------------------------------------------------------------------------
QString clipId(int ordinal);
QString noteId(int index);
//! Wire name of a track type ("instrument", "pattern", ...).
QString trackTypeNameOf(Track::Type type);

//! One addressable clip: the clip itself, its track, and both ids.
struct ClipRef
{
	Clip* clip = nullptr;
	Track* track = nullptr;
	int trackIndex = -1;    //!< index of the owning track in the song
	int indexInTrack = -1;  //!< index of the clip in its track's clip vector
	int ordinal = -1;       //!< clip-<ordinal>
};

//! Every clip of the song, in arrangement order (the enumeration clip-<n> uses).
QVector<ClipRef> enumerateClips();

//! Resolve "trk-<n>"; nullptr with \p error filled when it does not exist.
Track* resolveTrack(const QString& id, ControlResult* error);
//! Resolve "clip-<n>"; false with \p error filled when it does not exist.
bool resolveClip(const QString& id, ClipRef* ref, ControlResult* error);
//! Resolve a clip that carries notes. Refused (typed) when the clip's track is
//! not an instrument track, i.e. when there is no MidiClip to edit.
MidiClip* resolveMidiClip(const QString& id, ClipRef* ref, ControlResult* error);
//! Resolve "note-<n>" inside \p clip; \p index receives the note's index.
Note* resolveNote(MidiClip* clip, const QString& id, int* index, ControlResult* error);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
QJsonObject clipState(const ClipRef& ref);
QJsonObject noteState(const Note* note, int index);
//! The piano-roll view of one clip: the clip itself plus every note of it.
QJsonObject rollState(const ClipRef& ref);

// ---------------------------------------------------------------------------
// Selection. The GUI keeps its selection in views (QGraphicsItem state), which
// is not part of the project model and is not serialized, so the control surface
// keeps its own: it survives across commands, is reported by roll.get_state and
// arrangement.get_state, and never touches the document. UI-thread only, like
// every handler.
// ---------------------------------------------------------------------------
void selectClip(const QString& id);
QString selectedClipId();
//! Records the selection; out-of-range indices are dropped.
void selectNotes(const QString& clip, int noteCount, const QVector<int>& indices);
bool noteSelected(const QString& clip, int index);
QVector<int> selectedNoteIndices(const QString& clip);

// ---------------------------------------------------------------------------
// SPEC A16 transactions
// ---------------------------------------------------------------------------
//! The private "__transaction" payload a mutating handler returns; the registry
//! records it and strips it from the wire result.
QJsonObject transactionPayload(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs, bool reversible, const QString& mechanism);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_EDIT_H
