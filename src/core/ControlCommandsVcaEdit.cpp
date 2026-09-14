/*
 * ControlCommandsVcaEdit.cpp - the `vca.*` group's EDIT half, which is the
 *                              phase-locked multitrack edit itself:
 *                              vca.set_phase_lock, vca.track_add,
 *                              vca.track_remove and vca.edit_move (SPEC
 *                              A11-A16; the 0.3.0 ladder's OWNER-31 item 11).
 *
 * What this file makes true. A group that carries tracks
 * (VcaGroup::editTracks()) is an EDIT group: a media edit made on one member is
 * applied to every member at the same source position, which is what
 * "phase-locked" means - the members stay locked to ONE timeline, so a take
 * recorded across eight inputs is slid as one object and stays sample-aligned.
 * BACKLOG.md item 11 is the wording this implements: "an edit group distinct
 * from the landed mix group, membership by track, where a media edit on one
 * member applies to all members at the same source position".
 *
 * The correspondence rule is the design decision worth reading, and it is
 * stated once, in ControlCommandsVcaShared.h beside the two helpers that apply
 * it: the named clip is the ANCHOR and goes to exactly the position asked for,
 * and every other member track's clips that OVERLAP the anchor's pre-command
 * span move by the SAME DELTA. A member with nothing in that span is not an
 * error and is reported as unlocked; a member whose track no longer exists is
 * reported as skipped. The delta is applied as a DELTA rather than by moving
 * every member onto the anchor's new position, so two members deliberately
 * offset by a few ticks stay offset - which is what makes this a lock and not a
 * "snap every member onto one grid line".
 *
 * SCOPE, stated because it is a limit rather than an accident: the ONE media
 * edit this release propagates is a clip MOVE. Trim, slip, split and fades on a
 * locked group are not propagated - the entity carries the membership and the
 * lock, and the propagation rule is implemented for the edit a multitrack take
 * actually needs first (sliding a whole take against the rest of the song). The
 * sentence is in docs/VCA-EDIT-GROUPS.md, in docs/KNOWN-LIMITATIONS.md and in
 * docs/RELEASE-NOTES-v0.3.0-alpha.md.
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

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>

#include "Clip.h"
#include "ControlCommandsVcaShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

using namespace control;      // the shared vocabulary lives in ControlVocabulary.h
using namespace vcacontrol;   // this group's helpers

namespace
{

//! How many of the edit set's tracks still exist. A lock needs at least two:
//! with one track there is nothing to lock it to, and the command's whole claim
//! is cross-track.
int liveEditTracks(VcaGroup* group)
{
	int live = 0;
	for (int trackId : group->editTracks())
	{
		ControlResult ignored;
		if (control::resolveTrack(control::trackId(trackId), &ignored) != nullptr)
		{
			++live;
		}
	}
	return live;
}

/*! Every reason `vca.edit_move` cannot lock, checked BEFORE anything is written
 *  (so a refusal writes nothing at all), each as its own typed refusal naming
 *  the command that fixes it.
 */
bool checkLockable(VcaGroup* group, const control::ClipRef& anchor, ControlResult* error)
{
	if (!group->isPhaseLocked())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has its phase lock OFF, so a media edit is not propagated: "
				"switch it on with vca.set_phase_lock")
				.arg(vcaGroupId(group->id())));
		return false;
	}
	if (std::find(group->editTracks().begin(), group->editTracks().end(),
			anchor.track->id()) == group->editTracks().end())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not in %2's edit set, so there is nothing to lock it to: "
				"add it with vca.track_add")
				.arg(control::trackIdOf(anchor.track), vcaGroupId(group->id())));
		return false;
	}
	if (liveEditTracks(group) < 2)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has %2 live track(s) in its edit set; a phase lock needs at "
				"least two, or there is no other member to lock the edit to - add one with "
				"vca.track_add")
				.arg(vcaGroupId(group->id())).arg(liveEditTracks(group)));
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// vca.set_phase_lock
// ---------------------------------------------------------------------------
ControlResult setPhaseLock(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	const bool locked = args.value(QStringLiteral("locked")).toBool();
	const bool previous = group->isPhaseLocked();
	const int id = group->id();
	auto mixerRef = Engine::mixer();
	// The lock is a plain bool on the group, not a model: no checkpoint carries
	// it, so the recorded undo step writes the previous value back through the
	// same call. The group is resolved by ID at undo time, never by pointer -
	// a later vca.remove (undone first, in LIFO order) destroys this object and
	// the restore creates a different one with the same id.
	control::addUndoStep(
		[mixerRef, id, previous]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->setPhaseLocked(previous); }
		},
		[mixerRef, id, locked]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->setPhaseLocked(locked); }
		});
	group->setPhaseLocked(locked);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("previous_phase_locked"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("locked"), previous}},
			QStringLiteral("vca.set_phase_lock"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("locked"), previous}},
			true,
			QStringLiteral("action checkpoint: the lock is a plain flag on the group rather "
				"than an AutomatableModel, so there is no checkpoint that carries it. The "
				"recorded undo step writes the previous value back through the same "
				"VcaGroup::setPhaseLocked call the write uses. The membership is deliberately "
				"NOT touched - switching the lock off is 'edit the takes independently again', "
				"not 'forget which takes they are'")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.track_add / vca.track_remove - the edit membership.
// ---------------------------------------------------------------------------
ControlResult addEditTrack(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr)
	{
		return error;
	}
	if (group->hasEditTrack(track->id()))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already in %2's edit set")
				.arg(control::trackIdOf(track), vcaGroupId(group->id())));
	}

	const int id = group->id();
	const int trackId = track->id();
	auto mixerRef = Engine::mixer();
	control::addUndoStep(
		[mixerRef, id, trackId]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->removeEditTrack(trackId); }
		},
		[mixerRef, id, trackId]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addEditTrack(trackId); }
		});
	group->addEditTrack(trackId);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("added"), control::trackIdOf(track));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("track"), control::trackIdOf(track)},
				{QStringLiteral("in_edit_set"), false}},
			QStringLiteral("vca.track_remove"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("track"), control::trackIdOf(track)}},
			true,
			QStringLiteral("action checkpoint: edit membership is a list of STABLE TRACK IDS "
				"on the group (not a relation on the track, and not a mixer channel), so there "
				"is no live object a checkpoint could restore. The recorded undo step removes "
				"the id this command added, through the same VcaGroup::removeEditTrack call the "
				"write uses")));
	return ControlResult::success(result);
}

ControlResult removeEditTrack(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr)
	{
		return error;
	}
	if (!group->hasEditTrack(track->id()))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not in %2's edit set")
				.arg(control::trackIdOf(track), vcaGroupId(group->id())));
	}

	const int id = group->id();
	const int trackId = track->id();
	auto mixerRef = Engine::mixer();
	control::addUndoStep(
		[mixerRef, id, trackId]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addEditTrack(trackId); }
		},
		[mixerRef, id, trackId]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->removeEditTrack(trackId); }
		});
	group->removeEditTrack(trackId);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("removed"), control::trackIdOf(track));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("track"), control::trackIdOf(track)},
				{QStringLiteral("in_edit_set"), true}},
			QStringLiteral("vca.track_add"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("track"), control::trackIdOf(track)}},
			true,
			QStringLiteral("action checkpoint: the record holds the id this command drops and "
				"the recorded undo step puts it back through the same VcaGroup::addEditTrack "
				"call the write uses. Nothing else is touched: removing a track from the edit "
				"set moves no clip")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.edit_move - the phase-locked multitrack edit.
// ---------------------------------------------------------------------------
ControlResult editMove(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	control::ClipRef anchor;
	if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &anchor, &error))
	{
		return error;
	}
	if (!checkLockable(group, anchor, &error))
	{
		return error;
	}

	// The anchor's span is captured BEFORE anything moves: it is the
	// correspondence test for every other member, and after a move the anchor's
	// own end has changed (the two members a lock exists for). A clip of zero
	// length is degenerate but legal, so the span is at least one tick wide -
	// otherwise a zero-length anchor would lock to nothing at all and the
	// command would silently do a single-track edit.
	const tick_t from = anchor.clip->startPosition().getTicks();
	const tick_t spanEnd = std::max(anchor.clip->endPosition().getTicks(), from + 1);
	const tick_t to = static_cast<tick_t>(
		args.value(QStringLiteral("position")).toDouble());
	const tick_t delta = to - from;

	LockedEditResult out;
	for (int trackId : group->editTracks())
	{
		Track* member = control::resolveTrack(control::trackId(trackId), &error);
		if (member == nullptr)
		{
			// A member whose track is gone: reported, not an error. The lock
			// locks what is there (VcaGroup's own note on why the id stays).
			out.skippedTracks.append(control::trackId(trackId));
			continue;
		}
		if (member == anchor.track)
		{
			moveLockedClip(&out, member, anchor.clip, delta);
			continue;
		}
		const std::vector<Clip*> inSpan = clipsInSpan(member, from, spanEnd);
		if (inSpan.empty())
		{
			out.unlockedTracks.append(control::trackIdOf(member));
			continue;
		}
		for (Clip* clip : inSpan)
		{
			moveLockedClip(&out, member, clip, delta);
		}
	}

	QJsonObject result;
	result.insert(QStringLiteral("group"), vcaGroupId(group->id()));
	result.insert(QStringLiteral("clip"), args.value(QStringLiteral("clip")).toString());
	result.insert(QStringLiteral("anchor"), control::trackIdOf(anchor.track));
	result.insert(QStringLiteral("position"), anchor.clip->startPosition().getTicks());
	result.insert(QStringLiteral("delta"), delta);
	result.insert(QStringLiteral("moves"), out.moves);
	result.insert(QStringLiteral("moved_count"), out.moves.size());
	result.insert(QStringLiteral("unlocked_tracks"), out.unlockedTracks);
	result.insert(QStringLiteral("unlocked_count"), out.unlockedTracks.size());
	result.insert(QStringLiteral("skipped_tracks"), out.skippedTracks);
	result.insert(QStringLiteral("skipped_count"), out.skippedTracks.size());

	QJsonObject before;
	before.insert(QStringLiteral("group"), vcaGroupId(group->id()));
	before.insert(QStringLiteral("anchor"), control::trackIdOf(anchor.track));
	before.insert(QStringLiteral("position"), from);
	before.insert(QStringLiteral("span_end"), spanEnd);
	before.insert(QStringLiteral("moved_count"), out.moves.size());
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("group"), vcaGroupId(group->id()));
	inverseArgs.insert(QStringLiteral("clip"), args.value(QStringLiteral("clip")).toString());
	inverseArgs.insert(QStringLiteral("position"), from);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before,
			// There is no single call that moves every locked member back, so the
			// operation is named as the manual one (the shape clip.split and
			// clip.crossfade use) while `mechanism` names what control.undo
			// actually replays.
			QStringLiteral("UNIMPLEMENTED: one call that moves every locked clip back"),
			inverseArgs, true,
			QStringLiteral("composite checkpoint: a LIVE Clip checkpoint was taken for every "
				"clip the lock moved (the anchor plus one or more per member track that had "
				"clips overlapping the anchor's pre-command span), and the registry merges "
				"every checkpoint one command pushes into ONE undo step "
				"(ControlRegistry::runHandler -> ProjectJournal::mergeCheckpointsFrom), so one "
				"control.undo - or one Ctrl+Z - puts every member back where it was, rather "
				"than one undo per member. LIMIT: clip ids are index-derived (clip-<n> is the "
				"clip's ordinal in arrangement order, and a move re-sorts a track's clips), so "
				"the inverse is the checkpoint and NOT a replayed clip-<n> id - the recorded "
				"inverseArgs address the anchor by the id the caller used and are reported for "
				"audit only")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// The registrations.
// ---------------------------------------------------------------------------
void registerVcaSetPhaseLock(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.set_phase_lock");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("set_phase_lock");
	cmd.description = QStringLiteral("Switch a group's phase lock on or off. With the lock ON, "
		"a media edit made on one of the group's tracks is applied to every other member at the "
		"same source position (see vca.edit_move); with it OFF the membership is kept but the "
		"takes can be edited independently again. The default for a new group is ON. Reversible "
		"through the ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("locked"), control::booleanProperty()},
	}, {QStringLiteral("group"), QStringLiteral("locked")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("phase_locked"), control::booleanProperty()},
		{QStringLiteral("previous_phase_locked"), control::booleanProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setPhaseLock(args); };
	registry.registerCommand(cmd);
}

void registerVcaTrackAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.track_add");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("track_add");
	cmd.description = QStringLiteral("Add a track to a group's edit set, so media edits on it "
		"are locked to every other member's. Membership is by the track's STABLE trk-<n> id and "
		"is recorded on the GROUP, not on the track; the group does not have to be a mixer group "
		"of anything (a group can be an edit group alone). Survives save/reload (the group's "
		"<vcagroup> element carries an <edittrack track=\"n\"/> child per member). Reversible "
		"through the ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("track"), control::stringProperty()},
	}, {QStringLiteral("group"), QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("added"), control::stringProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return addEditTrack(args); };
	registry.registerCommand(cmd);
}

void registerVcaTrackRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.track_remove");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("track_remove");
	cmd.description = QStringLiteral("Take a track out of a group's edit set. No clip moves: "
		"the track simply stops being locked to the others. A track that is not in the set is "
		"REFUSED rather than silently accepted. Reversible through the ProjectJournal (a "
		"recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("track"), control::stringProperty()},
	}, {QStringLiteral("group"), QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("removed"), control::stringProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return removeEditTrack(args); };
	registry.registerCommand(cmd);
}

void registerVcaEditMove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.edit_move");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("edit_move");
	cmd.description = QStringLiteral("Move a clip to an absolute tick position AND move every "
		"other member of the group's edit set by the SAME delta, so the members stay locked to "
		"one timeline (this is the phase-locked multitrack edit). The named clip is the anchor; "
		"a member track whose clips overlap the anchor's pre-command span is moved by the delta, "
		"a member with nothing there is reported in unlocked_tracks, and a member whose track is "
		"gone in skipped_tracks. Refused, typed, when the group's phase lock is off, when the "
		"anchor's track is not in the edit set, or when the set has fewer than two live tracks. "
		"One control.undo returns every moved clip. Reversible through the ProjectJournal (a "
		"composite Clip checkpoint covering every clip the lock moved).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("group"), QStringLiteral("clip"), QStringLiteral("position")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("anchor"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("delta"), control::integerProperty()},
		{QStringLiteral("moves"), control::arrayProperty()},
		{QStringLiteral("moved_count"), control::integerProperty()},
		{QStringLiteral("unlocked_tracks"), control::arrayProperty()},
		{QStringLiteral("unlocked_count"), control::integerProperty()},
		{QStringLiteral("skipped_tracks"), control::arrayProperty()},
		{QStringLiteral("skipped_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return editMove(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerVcaEditCommands(ControlRegistry& registry)
{
	registerVcaSetPhaseLock(registry);
	registerVcaTrackAdd(registry);
	registerVcaTrackRemove(registry);
	registerVcaEditMove(registry);
}

} // namespace lmms
