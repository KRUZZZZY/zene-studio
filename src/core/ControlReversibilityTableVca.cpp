/*
 * ControlReversibilityTableVca.cpp - the `vca.*` command group's rows of THE
 *                                     SPEC A16 classification table: the whole
 *                                     twelve-row mutating surface of the
 *                                     phase-locked multitrack edit groups
 *                                     (OWNER-31 item 11; the mix half is task
 *                                     #622's entity).
 *
 * This file is data, like the halves it is joined with. It exists for one
 * reason: adding this group's rows pushed the true_inverse block past the
 * 500-line file ratchet, and that ratchet is not moved for a new feature. The
 * block is joined into the true_inverse half by reversibilityRowTable()
 * (ControlReversibilityTable.cpp), so every caller - ReversibilityTable's
 * constructor, control.transactions and tests/…/ReversibilityContractTest -
 * still reads ONE block with ONE row count, and each row's class still comes
 * from the row itself, never from the file it happens to live in.
 *
 * WHY EVERY MUTATING ROW HERE IS TRUE_INVERSE, stated once for the file.
 *
 * A VcaGroup is a QObject owned by the Mixer, not a JournallingObject with its
 * own id on the journal's object map: its <vcagroup> element is part of the
 * MIXER's serialized state, and a Mixer checkpoint would destroy and recreate
 * every mixer channel (the reason ControlCommandsMixer.cpp records for not
 * taking one - a MixerView holds those pointers). So no command in this group
 * can lean on a live checkpoint OF THE GROUP. What each one leans on instead is
 * named per row and is one of exactly two things:
 *
 *   * a live checkpoint on a MODEL the group owns or writes - the fader, the
 *     mute, the solo flag and every mixer channel's mute are
 *     AutomatableModels, and therefore JournallingObjects with ids of their
 *     own (the mechanism mixer.set_volume and track.set_solo already use); or
 *   * a recorded ACTION step for state that is not a model at all - a name, a
 *     membership list, a lock flag, a group's existence - pushed through
 *     control::addUndoStep, which is a true_inverse checkpoint by the table's
 *     own definition ("the plain, the composite and the action checkpoint").
 *
 * Two of these rows carry a LIMIT in their own text rather than a claim
 * (vca.set_solo's transient m_muteBeforeSolo, vca.edit_move's index-derived
 * clip ids); the same limits are in docs/VCA-EDIT-GROUPS.md and in the release
 * notes, and they are the reason the rows are not all one sentence.
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

#include "ControlReversibility.h"

/*!
 * The group's twelve mutating rows. The two reads - vca.list and
 * vca.get_state - are `not_mutating` and live with the other passive rows in
 * ControlReversibilityTablePassive.cpp, because the table's blocks are split by
 * WHAT THE INVERSE IS and not by command group.
 */

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kVcaRows[] = {
	R("vca.create", RC::TrueInverse, true,
		"a created group has no before-state: it did not exist. The group is "
		"not a JournallingObject (its element is part of the MIXER's state), so "
		"the inverse is the OPERATION, the shape mixer.add_channel uses",
		"action checkpoint: the recorded undo step deletes the group this command "
		"created, through the same Mixer::deleteVcaGroup path vca.remove uses. A "
		"created group carries only defaults - no member channels, no edit tracks, "
		"a unity fader - so removing it restores the mix exactly",
		""),
	R("vca.remove", RC::TrueInverse, true,
		"a deleted group has no live object a checkpoint could restore, and a "
		"Mixer checkpoint is unavailable (it would destroy and recreate every "
		"channel). What a group holds is a name, four scalars and two id lists, "
		"so unlike mixer.remove_channel the delete is exactly reconstructible",
		"action checkpoint: the recorded undo step re-creates the group with its "
		"id, name, fader, mute, solo, phase-lock flag, member channels and edit "
		"tracks, through the same Mixer::createVcaGroup + VcaGroup::addMember + "
		"VcaGroup::addEditTrack calls the write path uses. Deleting a group "
		"deletes no channel, so the members come back on the same indices",
		""),
	R("vca.rename", RC::TrueInverse, true,
		"the name is a plain QString on the group rather than an AutomatableModel, "
		"so there is no checkpoint that carries it",
		"action checkpoint: the recorded undo step writes the previous name back "
		"through the same VcaGroup::setName call the write uses, resolving the "
		"group by ID at undo time (a later vca.remove, undone first, destroys the "
		"object and the restore creates a different one with the same id)",
		""),
	R("vca.set_gain", RC::TrueInverse, true,
		"the fader IS an AutomatableModel (FloatModel m_vcaModel) and therefore a "
		"JournallingObject, so a checkpoint taken before the write restores it "
		"exactly - the mechanism mixer.set_volume uses for a channel fader",
		"ProjectJournal (VcaGroup fader checkpoint: the group's own <vcagroup> "
		"element carries `vca`). Nothing else is written: the members' own faders "
		"are never touched, because the group publishes a FACTOR "
		"(MixerChannel::m_vcaGain) rather than assigning a value",
		""),
	R("vca.set_mute", RC::TrueInverse, true,
		"the mute flag is an AutomatableModel (BoolModel m_muteModel) and mute is "
		"folded into the published gain rather than written into any member's mute "
		"model, so one checkpoint covers the whole change",
		"ProjectJournal (VcaGroup mute checkpoint: the group's own <vcagroup> "
		"element carries `muted`). A member's own mute state is untouched and "
		"therefore survives a mute/unmute pair unchanged",
		""),
	R("vca.set_solo", RC::TrueInverse, true,
		"soloing a group is NOT one write: Mixer::applyGroupSolo clears any other "
		"group's solo flag, saves and mutes every channel, then unmutes this "
		"group's members and their send neighbours. A single-object checkpoint "
		"would make one agent command cost N Ctrl+Z presses",
		"composite checkpoint: the group's own solo flag, every other group's solo "
		"and mute flags and every mixer channel's mute model are recorded as ONE "
		"step (control::addUndoStep with the object list), so one control.undo "
		"restores the whole exclusive-solo action - the same shape track.set_solo "
		"uses for the cross-track mute. LIMIT: MixerChannel::m_muteBeforeSolo is "
		"transient, is not part of the project file, and is therefore not "
		"restored; it is re-derived on the next solo action, exactly as "
		"track.set_solo states for Track::mutedBeforeSolo",
		""),
	R("vca.assign", RC::TrueInverse, true,
		"membership is a list of channel indices ON THE GROUP; a MixerChannel "
		"keeps no back-reference to its group, so there is no object whose "
		"checkpoint carries the relation",
		"action checkpoint: the recorded undo step removes the member this command "
		"added, through the same VcaGroup::removeMember call the write uses - "
		"which also republishes that channel's vca gain back to unity, so the "
		"audio path is restored and not only the list",
		""),
	R("vca.unassign", RC::TrueInverse, true,
		"the same list and the same absence of a back-reference as vca.assign. The "
		"channel keeps its own fader value throughout - a group scales and never "
		"assigns - so membership is the whole of what there is to restore",
		"action checkpoint: the recorded undo step puts the channel back through "
		"the same VcaGroup::addMember call the write path uses, which republishes "
		"the group's gain into it",
		""),
	R("vca.set_phase_lock", RC::TrueInverse, true,
		"the lock is a plain bool on the group rather than an AutomatableModel, so "
		"no checkpoint carries it. The edit membership is deliberately untouched "
		"in either direction - switching the lock off means 'edit the takes "
		"independently again', not 'forget which takes they are'",
		"action checkpoint: the recorded undo step writes the previous value back "
		"through the same VcaGroup::setPhaseLocked call the write uses, resolving "
		"the group by ID at undo time",
		""),
	R("vca.track_add", RC::TrueInverse, true,
		"edit membership is a list of STABLE TRACK IDS on the group - not a "
		"relation on the track's own element and not a mixer channel - so there is "
		"no live object a checkpoint could restore",
		"action checkpoint: the recorded undo step removes the id this command "
		"added, through the same VcaGroup::removeEditTrack call the write uses. "
		"No clip is touched by either direction: membership is not an edit",
		""),
	R("vca.track_remove", RC::TrueInverse, true,
		"the same list as vca.track_add; the record holds the id this command "
		"drops, so the inverse is exact rather than a re-derivation",
		"action checkpoint: the recorded undo step puts the id back through the "
		"same VcaGroup::addEditTrack call the write uses. The id survives the "
		"round trip because it is the track's STABLE id, which the track's own "
		"element carries and which no sibling's creation or removal changes",
		""),
	R("vca.edit_move", RC::TrueInverse, true,
		"the command moves one anchor clip and, by the same delta, every clip of "
		"the other member tracks that overlaps the anchor's pre-command span. Each "
		"of those clips is a Clip, and therefore a JournallingObject whose own "
		"serialized state carries its position - so every move has a real inverse, "
		"and the action is not one object",
		"composite checkpoint: a LIVE Clip checkpoint per moved clip, and the "
		"registry merges every checkpoint one command pushes into ONE undo step "
		"(ControlRegistry::runHandler -> ProjectJournal::mergeCheckpointsFrom), so "
		"one control.undo puts every member back rather than one undo per member. "
		"LIMIT: clip ids are index-derived (clip-<n> is the clip's ordinal in "
		"arrangement order, and a move re-sorts a track's clips), so the inverse "
		"is the checkpoint and not a replayed clip-<n> id",
		""),
};

constexpr int kVcaRowCount = static_cast<int>(sizeof(kVcaRows) / sizeof(kVcaRows[0]));

} // namespace

const ReversibilityRow* reversibilityVcaRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kVcaRowCount; }
	return kVcaRows;
}

} // namespace control
} // namespace lmms
