/*
 * ControlCommandsVcaEditSet.cpp - the `vca.*` group's EDIT-SET half:
 *                                 vca.set_phase_lock, vca.track_add and
 *                                 vca.track_remove (SPEC A11-A16; the 0.3.0
 *                                 ladder's OWNER-31 item 11).
 *
 * This file is the membership and the switch. A group that carries tracks is an
 * EDIT group - membership by track, and the phase lock is what makes an edit on
 * one member apply to every member at the same source position (BACKLOG.md item
 * 11). What the lock then DOES lives in ControlCommandsVcaEdit.cpp
 * (vca.edit_move); this half is what it acts on.
 *
 * It is a fourth translation unit for the same reason the group already had
 * three: the 500-line file ratchet. ControlCommandsVcaEdit.cpp reached 521 lines
 * once this half and the phase-locked move were written together, and a new file
 * over that limit fails Gate 7 - which is a gate this lane may not re-anchor for
 * a new feature. The seam is the one the group's own three-way split already
 * uses (entity / mix / edit) pushed one step further, and the split is a MOVE:
 * every handler and registration below is byte-identical to the text that was in
 * ControlCommandsVcaEdit.cpp.
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

} // namespace

void registerVcaEditSetCommands(ControlRegistry& registry)
{
	registerVcaSetPhaseLock(registry);
	registerVcaTrackAdd(registry);
	registerVcaTrackRemove(registry);
}

} // namespace lmms
