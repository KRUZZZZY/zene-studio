/*
 * ControlGrooveSupport.h - the helpers the groove.* command group shares
 *                           between its read half (ControlCommandsGroove.cpp)
 *                           and its edit half (ControlCommandsGrooveEdit.cpp).
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

#ifndef LMMS_CONTROL_GROOVE_SUPPORT_H
#define LMMS_CONTROL_GROOVE_SUPPORT_H

#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlVocabulary.h" // the shared schema + id vocabulary
#include "GroovePool.h"
#include "GrooveTemplate.h"
#include "LmmsTypes.h"
#include "Note.h"

namespace lmms
{

class MidiClip;
struct ControlResult;

namespace control
{

struct ClipRef;

// ---------------------------------------------------------------------------
// The pool
// ---------------------------------------------------------------------------

/*! The project's groove pool, through the Song that owns it.
 *
 *  A groove pool is project state (docs/GROOVE-POOL.md section 4), so it is
 *  addressed through the engine's own Song rather than a control-surface
 *  static: loading a project replaces it, and a new project clears it.
 *  nullptr only before the engine exists, which every handler's engine check
 *  has already refused. */
GroovePool* projectGroovePool();

//! The pool's own XML, the before-state a recorded pool edit restores.
QString groovePoolXml();

/*! Records the inverse of a pool edit as an ACTION checkpoint
 *  (control::addUndoStep) and nothing else.
 *
 *  A pool edit has no live object a journal checkpoint could restore: the pool
 *  is not in the track container, so a Song checkpoint does not carry it (the
 *  finding docs/TEMPO-MAP.md and docs/MODULATION.md record for the map and the
 *  layer). What is recorded is therefore the pool's own serialized element,
 *  captured BEFORE the edit, written back by the step. \a before is that
 *  capture; the redo half captures the state as it is now, so a redo is
 *  faithful rather than a dropped step.
 */
void recordGroovePoolRestore(const QString& before);

/*! The private "__transaction" payload of a pool edit.
 *
 *  \a before is the pool XML captured before the edit. The recorded inverse op
 *  is a named command the surface really implements (\a inverseOp with
 *  \a inverseArgs), so a reader can re-issue it by hand, and "applies" is
 *  "journal" because the action checkpoint on the engine's own undo stack is
 *  what control.undo unwinds.
 */
QJsonObject groovePoolInverse(const QString& before, const QString& inverseOp,
	const QJsonObject& inverseArgs);

// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------

/*! One template as the wire reports it: always the summary (name and
 *  geometry), plus every step when \a detailed. */
QJsonObject grooveJson(const GrooveTemplate& groove, bool detailed);

/*! A template as the ARGUMENTS that write it back verbatim: name,
 *  length_ticks, step_ticks and the step list. This is what groove.set accepts,
 *  so it is also the recorded inverse of an extract that REPLACED a groove (a
 *  reader can re-issue the call and get the previous template back exactly,
 *  where "remove the new one" would lose it). */
QJsonObject grooveWriteArgs(const GrooveTemplate& groove);

//! The summary of every template in the pool, in the pool's own order.
QJsonArray grooveListJson(const GroovePool& pool);

/*! The pool half of every read-back this group returns, so a caller never has
 *  to guess what the pool became: the count, the engine's own bounds (which
 *  are what a refusal has to be read against) and the summaries. */
QJsonObject groovePoolState(const GroovePool& pool);

//! groovePoolState()'s schema; \a extra adds the keys one command reports on
//! top of it.
QJsonObject grooveStateSchema(QJsonObject extra = QJsonObject());

//! The schema of the step list groove.set accepts.
QJsonObject grooveStepsProperty();
//! The schema of one template's detail object.
QJsonObject grooveDetailSchema();

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

/*! A strength argument in [0, 1]: absent reads as 1 (the full groove), a value
 *  outside the range is refused rather than clamped, because a caller that
 *  asked for 5 asked for something this engine cannot mean. */
bool readStrength(const QJsonObject& args, float* out, ControlResult* error);

/*! Reads a positive tick count argument (\a key), refusing anything outside
 *  \a minimum..\a maximum or not an integer. */
bool readTicks(const QJsonObject& args, const QString& key, tick_t minimum, tick_t maximum,
	tick_t* out, ControlResult* error);

//! Resolves the clip the arguments name and insists it carries notes.
MidiClip* resolveGrooveClip(const QJsonObject& args, ClipRef* ref, ControlResult* error);

/*! The position and velocity of every note of \a clip, taken BEFORE an edit so
 *  the result can report what really moved (a handler that reported "done"
 *  would be the shape the no-tautology rule exists against). */
struct NoteSnapshot
{
	std::vector<tick_t> positions;
	std::vector<int> velocities;
};

NoteSnapshot snapshotNotes(const MidiClip& clip);

/*! Adds "positions_moved", "velocities_moved" and "notes_moved" to \a out by
 *  comparing the snapshot with the notes as they now are. */
void addMoveCounts(const NoteSnapshot& before, const MidiClip& clip, QJsonObject* out);

//! The mechanism text every note-editing verb of this group records: the clip's
//! own journal checkpoint, which is what note.*'s note edits reverse with.
QString grooveClipMechanism();

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_GROOVE_SUPPORT_H
