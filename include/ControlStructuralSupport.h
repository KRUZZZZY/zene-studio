/*
 * ControlStructuralSupport.h - the inverse of a STRUCTURAL operation (Zene
 *                              Studio; SPEC A16 deliverable 5, task #664).
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

#ifndef LMMS_CONTROL_STRUCTURAL_SUPPORT_H
#define LMMS_CONTROL_STRUCTURAL_SUPPORT_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{

class Effect;
class EffectChain;
class Track;
class TrackContainer;

namespace control
{

// ---------------------------------------------------------------------------
// Structural operations, and why their inverse is not a checkpoint
//
// A checkpoint restores an object that still EXISTS: `ProjectJournal` keeps the
// object's journal id, captures its serialized state, and puts that state back
// with `restoreState()`. A STRUCTURAL operation destroys or creates the object
// itself, so there is nothing to put back:
//
//   - deleting a track: the destruction path (`~Track`) deletes the track's
//     clips BEFORE the container forgets the pointer, so by the time
//     `TrackContainer::removeTrack` runs, the clips are already gone. The
//     captured XML has to be taken while the track is still alive;
//   - moving a track: the state is the ORDER of the container, which is not a
//     serialized property of any track;
//   - unloading an effect: `EffectChain::removeEffect` + `deleteLater` destroys
//     the instance, and its settings live only in it;
//   - creating anything: there is no before-state at all.
//
// So the inverse is the OPERATION: a captured document plus a recorded pair of
// actions. These helpers build that pair, in ONE place, so the socket verbs and
// the product's own interface record the SAME inverse for the SAME operation -
// a GUI delete that could not be undone beside a socket delete that could is
// exactly the two-implementations defect SPEC A16 exists to remove.
//
// Every helper here is UI-thread only: it may run an undo action but never
// touches an audio-thread path (the deletions go through the engine's own
// requestChangeInModel()/doneChangeInModel() pair, because that is what the
// product's own code paths do).
// ---------------------------------------------------------------------------

//! Bounded size of a captured structural document. A track's own XML is tens of
//! KB at the top end (its clips carry their notes); a capture above this is
//! REFUSED rather than recorded truncated, because a truncated track is a
//! corrupt track. The same figure the device-state snapshot uses.
constexpr int StructuralSnapshotLimit = 65536;

//! Index of \a track in \a container's list, or -1 when it is not in it.
LMMS_EXPORT int trackIndexIn(const TrackContainer* container, const Track* track);

/*! True while a recorded structural step is APPLYING a reorder.
 *
 *  The replay itself must not be recorded: `TrackContainer::moveTrack` journals
 *  every reorder it performs, and an undo that performs one would otherwise
 *  leave a fresh step on the stack for every unwind, so the history would GROW
 *  while it unwinds and the next undo would re-apply what was just undone.
 *  \sa moveTrackToIndex, which sets it for the duration of its own call.
 */
LMMS_EXPORT bool structuralReplayInProgress();

/*! Moves \a track so that, when the call returns, it sits at \a index in its
 *  container - the same "erase then insert" the product's own reorder performs
 *  (`TrackContainer::moveTrack`), with the bounds the socket needs: a negative
 *  or past-the-end index, or a track that is not in the container at all, is
 *  refused (false) rather than silently inserting at an unspecified place.
 */
LMMS_EXPORT bool moveTrackToIndex(Track* track, int index);

/*! Records the inverse of the deletion of \a track: the track's own XML is
 *  captured while it is still alive, and ONE structural step is pushed whose
 *  undo recreates the track - WITH ITS CLIPS - through the same
 *  Track::create(element, container) the project loader uses, at the index it
 *  was removed from.
 *
 *  False when nothing could be recorded (no journal, or a track too large to
 *  capture); the caller then reports the deletion as NOT reversible instead of
 *  claiming it can be undone, because a caller that undoes a step that was
 *  never recorded would take back the WRONG edit.
 */
LMMS_EXPORT bool journalTrackRemoval(Track* track);

/*! Records the inverse of a reorder of \a track from \a fromIndex to
 *  \a toIndex: one structural step whose undo puts the track back at
 *  \a fromIndex and whose redo sends it forward again. The order of a container
 *  is not a serialized property of any track, so there is no object checkpoint
 *  for it - the recorded pair IS the mechanism.
 */
LMMS_EXPORT bool journalTrackMove(Track* track, int fromIndex, int toIndex);

/*! Records the inverse of the removal of \a effect (which sat at \a index in
 *  \a chain): the device's state document is captured, and ONE structural step
 *  is pushed whose undo RE-INSTANTIATES the same device through the chain's own
 *  instantiate path and puts the captured settings back, at the index it was
 *  removed from. False when the state could not be captured.
 */
LMMS_EXPORT bool journalEffectRemoval(EffectChain* chain, Effect* effect, int index);

/*! Recreates the effect described by \a stateXml (a document written by
 *  controlEffectStateXml()) inside \a chain, at \a index, through
 *  Effect::instantiate() with the sub-plugin key the document carries - the
 *  same call EffectChain::loadSettings makes when the project is re-opened.
 *  nullptr when the document is not a device document or the engine refuses the
 *  device. The new instance carries the captured settings.
 */
LMMS_EXPORT Effect* recreateEffectFromState(EffectChain* chain, const QString& stateXml, int index);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_STRUCTURAL_SUPPORT_H
