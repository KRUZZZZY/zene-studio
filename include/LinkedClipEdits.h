/*
 * LinkedClipEdits.h - trim and split of a linked clip apply to the whole group
 *                     (owner decision 2026-10-08, docs/LINKED-CLIPS.md section 2).
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

#ifndef LMMS_LINKED_CLIP_EDITS_H
#define LMMS_LINKED_CLIP_EDITS_H

#include <QString>
#include <QVector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;
class MidiClip;

/*! A trim or a split made on one member of a link group is made on every
 *  member, as one undo step, so the members never diverge inside a group.
 *
 *  - TRIM and RESIZE (clip.trim, clip.resize, the song editor's left- and right-edge drags): every peer's start and
 *    end move by the same deltas as the edited clip's, and its source offset
 *    moves against its start, which is clip.trim's head-trim rule. Each peer
 *    keeps its own position; only the edge moves.
 *  - SPLIT (clip.split, the knife): every member is cut at the same
 *    CLIP-RELATIVE position. The left halves keep the group; the right halves
 *    form a second group of their own. A member too short to be cut there is
 *    left whole and stays in the left group. A lone right half is unlinked.
 *    (A member too short to be cut there is left whole. That only happens in a group whose members already
 *    differ in length - a legacy project, or members resized one by one before trim and resize applied to the
 *    group; a group made and edited as a group has equal-length members, so every member is cut.)
 *    The cut is non-destructive (both halves keep the whole note list and the
 *    window moves), so the content of each resulting group stays identical.
 *  - TRIM OF CONTENT (the arranger's "clear notes out of bounds"): the trimmed
 *    note list is written onto every peer, and each peer's start offset moves by
 *    the amount the notes were re-based by, so a peer plays the same notes at
 *    the same song positions.
 */
namespace ClipLinks
{

//! Every other member of \p clip's link group (empty when unlinked).
LMMS_EXPORT QVector<Clip*> peersOf(const Clip* clip);

//! Empty when \p source's peers can take the edge edit; otherwise why not.
//! Nothing is changed either way.
LMMS_EXPORT QString planEdgeEdit(const Clip* source, tick_t startDelta, tick_t endDelta);

/*! Moves every peer's start by \p startDelta and its end by \p endDelta (its
 *  source offset against the start). With \p checkpoint the peers are captured
 *  first as ONE journal step. Returns how many peers were edited; run
 *  planEdgeEdit() first, a peer the edit would empty is skipped here. */
LMMS_EXPORT int applyEdgeEdit(Clip* source, tick_t startDelta, tick_t endDelta, bool checkpoint);

//! planEdgeEdit / applyEdgeEdit for several edited clips at once (a VCA lock's clips): every linked clip
//! that follows one of \p edited and is not itself in it, each once.
LMMS_EXPORT QString planEdgeEditFor(const QVector<Clip*>& edited, tick_t startDelta, tick_t endDelta);
LMMS_EXPORT int applyEdgeEditFor(const QVector<Clip*>& edited, tick_t startDelta, tick_t endDelta, bool checkpoint);

//! Captures \p source's peers as ONE journal step (nothing when it has none).
LMMS_EXPORT void checkpointPeers(const Clip* source);

//! Captures the tracks of \p source and of every peer as ONE journal step: the step a
//! destructive edit takes, because it replaces a clip (a track restore brings it back).
LMMS_EXPORT void checkpointGroupTracks(const Clip* source);

struct GroupSplit
{
	Clip* right = nullptr;  //!< the right half of \p source's own cut (null: the position is not inside it)
	int members = 0;        //!< members cut, source included
	int rightGroup = 0;     //!< the link id of the right halves (0: they are not a group)
	QVector<Clip*> lefts;   //!< the members cut (their left halves), for a caller that must not cut one twice
};

/*! Cuts \p source and every other member of its group at \p position (absolute
 *  ticks inside \p source). One journal step covers the tracks of every member
 *  cut. \p source may be unlinked: then this is a plain cut of one clip. */
LMMS_EXPORT GroupSplit splitGroupAt(Clip* source, tick_t position);

/*! The arranger's trim of content: \p trimmed (already in the group, with its
 *  note list trimmed and re-based) writes its notes onto every other member and
 *  shifts each one's start offset by \p offsetShift. Mirroring must already be
 *  suspended by the caller. Returns the members written. */
LMMS_EXPORT int adoptTrimmedContent(MidiClip* trimmed, tick_t offsetShift);

} // namespace ClipLinks

} // namespace lmms

#endif // LMMS_LINKED_CLIP_EDITS_H
