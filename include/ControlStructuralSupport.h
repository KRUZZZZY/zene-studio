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

#include <functional>
#include <optional>

#include <QPointer>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class Effect;
class EffectChain;
class Track;
class TrackContainer;
struct ControlDeviceEntry;

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
 *  False when nothing could be recorded (no journal, a journal that is not
 *  journalling, or a track too large to capture); the caller then reports the
 *  deletion as NOT reversible instead of claiming it can be undone, because a
 *  caller that undoes a step that was never recorded would take back the WRONG
 *  edit.
 */
LMMS_EXPORT bool journalTrackRemoval(Track* track);

/*! Records the inverse of deleting \a owner TOGETHER WITH its child automation
 *  lanes (every AutomationTrack whose followsTrack() is `owner`'s id) as ONE
 *  structural step: each track's own XML is captured while it is alive, the undo
 *  recreates them all (by its own insert order) in ONE step, and the redo
 *  removes them again. This is what keeps lane ownership consistent through a
 *  delete - a lane cannot outlive its owner claiming to follow a track that is
 *  gone - and it costs one Ctrl+Z, not one per track (Q050-022).
 *
 *  Falls back to journalTrackRemoval() when \a owner has no lanes. False when
 *  nothing could be recorded (no journal, a journal that is not journalling, or
 *  any captured track too large).
 */
LMMS_EXPORT bool journalTrackRemovalWithLanes(Track* owner);

/*! Records the inverse of a reorder of \a track from \a fromIndex to
 *  \a toIndex: one structural step whose undo puts the track back at
 *  \a fromIndex and whose redo sends it forward again. The order of a container
 *  is not a serialized property of any track, so there is no object checkpoint
 *  for it - the recorded pair IS the mechanism.
 *
 *  False when no step was recorded (no journal, or a journal that is not
 *  journalling): a reorder nobody recorded has no inverse.
 */
LMMS_EXPORT bool journalTrackMove(Track* track, int fromIndex, int toIndex);

/*! Records the inverse of the removal of \a effect (which sat at \a index in
 *  \a chain): the device's state document is captured, and ONE structural step
 *  is pushed whose undo RE-INSTANTIATES the same device through the chain's own
 *  instantiate path and puts the captured settings back, at the index it was
 *  removed from. False when the state could not be captured, when the captured
 *  document cannot rebuild this device, or when the journal refuses the step.
 */
LMMS_EXPORT bool journalEffectRemoval(EffectChain* chain, Effect* effect, int index);

/*! journalEffectRemoval() with the device document given by the caller: the
 *  undo rebuilds the device from \a xml, and the step is recorded only when
 *  \a xml is a usable document. journalEffectRemoval passes the device's own
 *  document; a caller that passes another one records exactly that document.
 *  A usable document must PARSE, its root must be the <zenepluginstate> element,
 *  it must name a plugin, carry the removed device's own state element and
 *  hosted identity, and its wrapper key must be the device's - because a
 *  document that fails this cannot be turned back into that device (new-p14-82).
 */
LMMS_EXPORT bool journalRemovalDocument(EffectChain* chain, Effect* effect, int index, const QString& xml);

/*! TEST SEAM. While set, journalRemovalDocument() (and so journalEffectRemoval()) reports that the device's
 *  state could not be captured, exactly as it does for an empty document or one over StructuralSnapshotLimit -
 *  the failure no device loadable in a test build can produce on its own. A production path never sets it.
 *  Callers that must not remove a device they cannot restore (a script's removeEffect) refuse on that result. */
LMMS_EXPORT void setRemovalCaptureFailsForTest(bool fails);

/*! Takes \a effect off \a chain. The removal pushes no checkpoint of its own,
 *  whether or not journalEffectRemoval recorded it: a recorded removal's one
 *  structural step restores the chain's enabled flag, and a removal that records
 *  nothing must not leave an orphan flag step behind. Callers delete the effect. */
LMMS_EXPORT void detachEffect(EffectChain* chain, Effect* effect);

/*! Records the inverse of loading \a effect, a fresh instance of \a entry that
 *  sits at \a slot of \a chain: ONE action step whose undo takes the device out
 *  and whose redo instantiates \a entry at \a slot again (plugin.load,
 *  plugin.replace, a script's loadEffect).
 *
 *  Both this step and journalEffectRemoval()'s hold no chain pointer: they find
 *  the chain again by its owner's id (the track or mixer channel, and the rack
 *  chain) every time they run, and the device by its slot. A track.remove and its
 *  undo rebuild the track - its chain and its devices too - as NEW objects under
 *  the same id, so a kept EffectChain* or Effect* would name freed memory. A step
 *  whose chain is gone does nothing.
 */
LMMS_EXPORT void journalEffectLoad(EffectChain* chain, Effect* effect, const ControlDeviceEntry& entry, int slot);

//! Find a recorded chain again by its owner's address; an unaddressable chain is guarded until deleted.
LMMS_EXPORT std::function<EffectChain*()> recordedChainFinder(EffectChain* chain);

//! Remove the recorded device if still on the chain, or its rebuilt counterpart at the same slot and plugin type.
LMMS_EXPORT void removeRecordedEffect(EffectChain* chain, QPointer<Effect>& made, int slot, const QString& plugin);

/*! Recreates the effect described by \a stateXml (a document written by
 *  controlEffectStateXml()) inside \a chain, at \a index, through
 *  Effect::instantiate() with the sub-plugin key the document carries - the
 *  same call EffectChain::loadSettings makes when the project is re-opened.
 *  nullptr when the document is not a device document or the engine refuses the
 *  device. The new instance carries the captured settings.
 */
LMMS_EXPORT Effect* recreateEffectFromState(EffectChain* chain, const QString& stateXml, int index);

/*! The first half of recreateEffectFromState(): validates \a stateXml, loads the
 *  device it names and restores its recorded settings, WITHOUT attaching it to
 *  \a chain. None of that takes the audio change lock, so an undo does the work
 *  here and takes the lock only for the attach. nullptr, with the reason recorded
 *  for control.undo, when the engine refuses the device or its settings.
 */
LMMS_EXPORT Effect* instantiateRecordedDevice(EffectChain* chain, const QString& stateXml);

/*! The failure of the LAST device restore a structural step attempted, or
 *  nullopt when the last one succeeded (or none has run) - BUG-CTL-1, SPEC A16.
 *
 *  WHY A SIDE CHANNEL AND NOT A RETURN VALUE: the journal's structural callbacks
 *  are `void` and `ProjectJournal::undo()` returns `void`, so a recreate that
 *  could not put the captured settings back has no way to hand a failure to the
 *  command that asked for the undo. Without this record the undo of
 *  `plugin.unload` answered SUCCESS over a recreated device sitting at its
 *  DEFAULTS - an inverse that silently half-works, which is the contract
 *  violation A16 exists to remove. `recreateEffectFromState()` records the
 *  failure here; `control.undo` clears the record before it unwinds a step and
 *  reads it after, so the answer describes exactly the step that just ran.
 *
 *  CONSUMING: the read clears the record, so one failure is reported once and
 *  cannot leak into a later undo. UI thread only, like every helper here.
 */
LMMS_EXPORT std::optional<ControlResult> takeStructuralRestoreFailure();

//! Drops any recorded restore failure without reporting it. Called before an
//! undo unwinds a step, so a failure recorded by an earlier operation (a GUI
//! Ctrl+Z, say) can never be attributed to this one.
LMMS_EXPORT void clearStructuralRestoreFailure();

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_STRUCTURAL_SUPPORT_H
