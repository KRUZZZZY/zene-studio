/*
 * ControlCommandsVca.cpp - the `vca.*` command group's GROUP + ENTITY half:
 *                          vca.create, vca.remove, vca.list, vca.get_state and
 *                          vca.rename (SPEC A11-A16; the 0.3.0 ladder's
 *                          OWNER-31 item 11, "phase-locked multitrack edit
 *                          groups").
 *
 * The engine half is include/VcaGroup.h and src/core/VcaGroup.cpp (the mix
 * group task #622 landed: one fader over a set of member mixer channels,
 * relative rather than absolute, exactly reversible) plus the edit half this
 * lane added to the same entity (a set of tracks whose media edits the group
 * locks together). This file, ControlCommandsVcaMix.cpp and
 * ControlCommandsVcaEdit.cpp are what make any of it drivable, and they are the
 * ONLY way to reach it in 0.3.0 - there is no VCA strip, no group menu and no
 * phase-lock toggle in this release's interface, which is the UI-absence line
 * docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md carry, and
 * before this lane the only way to get a group at all was to hand-edit a
 * `<vcagroup>` element into the project file.
 *
 * The id form and the shared helpers are in ControlCommandsVcaShared.h; the
 * split into three translation units is the 500-line file ratchet's, and
 * follows the folder-track, session, warp, rack, comp and automation groups.
 * The three files are one group with one registration point
 * (registerVcaCommands, at the bottom of this file).
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsVcaShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

using namespace control;      // the shared vocabulary lives in ControlVocabulary.h
using namespace vcacontrol;   // this group's helpers

namespace
{

/*! Everything a group holds, captured as a value.
 *
 *  `vca.remove` is the one command here that destroys its object, and there is
 *  no live checkpoint that could bring a deleted VcaGroup back: the group is a
 *  QObject owned by the Mixer, not a JournallingObject with an id of its own on
 *  the journal's object map (the `<vcagroup>` element is part of the MIXER's
 *  serialized state, and a Mixer checkpoint would destroy and recreate every
 *  channel - see the note in src/core/ControlCommandsMixer.cpp). So the inverse
 *  is the OPERATION, the same shape mixer.add_channel and track.remove use, and
 *  what it needs is this: the whole of what the group was.
 */
struct GroupSnapshot
{
	int id = -1;
	QString name;
	float volume = 1.0f;
	bool muted = false;
	bool soloed = false;
	bool locked = true;
	std::vector<mix_ch_t> members;
	std::vector<int> tracks;
};

GroupSnapshot snapshotGroup(VcaGroup* group)
{
	GroupSnapshot snap;
	snap.id = group->id();
	snap.name = group->name();
	snap.volume = group->vcaModel()->value();
	snap.muted = group->muteModel()->value();
	snap.soloed = group->soloModel()->value();
	snap.locked = group->isPhaseLocked();
	snap.members = group->members();
	snap.tracks = group->editTracks();
	return snap;
}

/*! Re-create `snap` on `mixer`, through the SAME calls the write path uses
 *  (Mixer::createVcaGroup with the recorded id, VcaGroup::addMember,
 *  VcaGroup::addEditTrack), so a restore cannot diverge from a create.
 *
 *  The fader, the mute and the solo are written LAST and in that order: solo
 *  makes exactly the members audible, so it must run after the members exist,
 *  and mute is folded into the published gain, so it must not be the value the
 *  solo pass reads as "the pre-solo state".
 */
VcaGroup* restoreGroup(Mixer* mixer, const GroupSnapshot& snap)
{
	VcaGroup* group = mixer->createVcaGroup(snap.name, snap.id);
	if (group == nullptr)
	{
		return nullptr;
	}
	group->setPhaseLocked(snap.locked);
	for (mix_ch_t member : snap.members)
	{
		group->addMember(member);
	}
	for (int trackId : snap.tracks)
	{
		group->addEditTrack(trackId);
	}
	group->vcaModel()->setValue(snap.volume);
	group->muteModel()->setValue(snap.muted);
	group->soloModel()->setValue(snap.soloed);
	return group;
}

// ---------------------------------------------------------------------------
// vca.create
// ---------------------------------------------------------------------------
ControlResult createGroup(const QJsonObject& args)
{
	Mixer* mixer = Engine::mixer();
	const int groupsBefore = static_cast<int>(mixer->vcaGroups().size());
	const QString name = args.value(QStringLiteral("name")).toString();
	VcaGroup* group = mixer->createVcaGroup(name);
	if (group == nullptr)
	{
		// Mixer::createVcaGroup only fails when the id it picked is taken,
		// which its own "lowest unused id" scan makes unreachable - but a
		// command that quietly reported success for a group that was not made
		// would be the worse answer.
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mixer refused to create a VCA group"));
	}
	const int id = group->id();

	// SPEC A16 deliverable 5: a created group has no before-state - it did not
	// exist - so the inverse is the OPERATION that removes what this call made.
	// A fresh group carries a default fader, mute, solo and an empty edit set,
	// so removing it restores the mix exactly; the same shape (and the same
	// argument) as mixer.add_channel.
	auto mixerRef = mixer;
	control::addUndoStep(
		[mixerRef, id]() { mixerRef->deleteVcaGroup(id); },
		[mixerRef, id]()
		{
			if (mixerRef->vcaGroup(id) == nullptr) { mixerRef->createVcaGroup(QString(), id); }
		});

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("group"), vcaGroupId(id));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group_count"), groupsBefore}},
			QStringLiteral("vca.remove"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step deletes the group this "
				"command created, through the same Mixer::deleteVcaGroup path vca.remove uses. "
				"A created group carries only defaults - no members, no edit tracks, a unity "
				"fader - so removing it restores the mix exactly. The before-state records the "
				"group COUNT, because 'a group that did not exist' is the whole of what there "
				"was to restore")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.remove
// ---------------------------------------------------------------------------
ControlResult removeGroup(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	Mixer* mixer = Engine::mixer();
	const GroupSnapshot snap = snapshotGroup(group);
	const QString id = vcaGroupId(snap.id);
	// The members play at unity again the moment the group is gone
	// (Mixer::deleteVcaGroup calls refreshGroups), and the edit set goes with
	// the group - nothing else in the song refers to it.
	mixer->deleteVcaGroup(snap.id);

	// SPEC A16: a deleted group has no live object a checkpoint could restore,
	// so the inverse is the OPERATION, and it is EXACT rather than
	// best-effort - every field the group held is re-applied through the same
	// calls the write path uses. Contrast mixer.remove_channel, whose row is a
	// snapshot with reversible=false because a MixerChannel cannot be recreated
	// WITH state: a group holds nothing but scalars, flags and id lists.
	auto mixerRef = mixer;
	control::addUndoStep(
		[mixerRef, snap]() { restoreGroup(mixerRef, snap); },
		[mixerRef, snap]() { mixerRef->deleteVcaGroup(snap.id); });

	QJsonObject result;
	result.insert(QStringLiteral("removed"), id);
	result.insert(QStringLiteral("name"), snap.name);
	result.insert(QStringLiteral("member_count"), static_cast<int>(snap.members.size()));
	result.insert(QStringLiteral("track_count"), static_cast<int>(snap.tracks.size()));
	result.insert(QStringLiteral("group_count"), static_cast<int>(mixer->vcaGroups().size()));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), id},
				{QStringLiteral("name"), snap.name},
				{QStringLiteral("volume"), static_cast<double>(snap.volume)},
				{QStringLiteral("muted"), snap.muted},
				{QStringLiteral("soloed"), snap.soloed},
				{QStringLiteral("phase_locked"), snap.locked},
				{QStringLiteral("member_count"), static_cast<int>(snap.members.size())},
				{QStringLiteral("track_count"), static_cast<int>(snap.tracks.size())}},
			QStringLiteral("UNIMPLEMENTED: there is no vca.create call that restores "
				"id, name, fader, flags and both member lists together"),
			QJsonObject{{QStringLiteral("group"), id}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step re-creates the group with "
				"the id, name, fader, mute, solo, phase-lock flag, member channels and edit "
				"tracks it held, through the same Mixer::createVcaGroup + VcaGroup::addMember "
				"+ VcaGroup::addEditTrack calls the write path uses. The restore is EXACT: a "
				"group holds scalars, flags and two id lists, and nothing else in the mix "
				"refers to it, so one control.undo puts the whole group back - members keep "
				"the same channel indices (deleting a group deletes no channel)")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.list / vca.get_state - the two reads.
// ---------------------------------------------------------------------------
ControlResult listGroups(const QJsonObject&)
{
	QJsonArray groups;
	for (VcaGroup* group : Engine::mixer()->vcaGroups())
	{
		groups.append(groupState(group));
	}
	QJsonObject result;
	result.insert(QStringLiteral("groups"), groups);
	result.insert(QStringLiteral("count"), groups.size());
	return ControlResult::success(result);
}

ControlResult getGroupState(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	return ControlResult::success(groupState(group));
}

// ---------------------------------------------------------------------------
// vca.rename
// ---------------------------------------------------------------------------
ControlResult renameGroup(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	const QString name = args.value(QStringLiteral("name")).toString();
	if (name.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a group's name cannot be empty: use the name you want, or remove "
				"the group with vca.remove"));
	}

	const QString previous = group->name();
	const int id = group->id();
	auto mixerRef = Engine::mixer();
	// The name is a plain QString on the group, not an AutomatableModel, so
	// there is no checkpoint that carries it - the recorded undo step writes the
	// previous name back through the same setName every other writer uses. It
	// resolves the group by ID at undo time rather than capturing the pointer:
	// a later vca.remove (undone first, in LIFO order) destroys this object and
	// the restore creates a DIFFERENT one with the same id.
	control::addUndoStep(
		[mixerRef, id, previous]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->setName(previous); }
		},
		[mixerRef, id, name]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->setName(name); }
		});
	group->setName(name);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("previous_name"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("name"), previous}},
			QStringLiteral("vca.rename"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("name"), previous}},
			true,
			QStringLiteral("action checkpoint: the name is a plain field on the group rather "
				"than an AutomatableModel, so there is no checkpoint that carries it. The "
				"recorded undo step writes the previous name back through the same "
				"VcaGroup::setName the write uses, resolving the group by ID at undo time")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// The registrations.
// ---------------------------------------------------------------------------
void registerVcaCreate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.create");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("create");
	cmd.description = QStringLiteral("Create a VCA / mix-and-edit group and return its vca-<n> "
		"id. A group owns one fader that scales every member mixer channel RELATIVE to the "
		"channel's own fader (moving it never writes a member's fader, which is what makes it "
		"exactly reversible), a mute, a solo, and - once vca.track_add has named them - a set "
		"of tracks whose media edits it locks together. A group holds no members at birth; add "
		"mixer channels with vca.assign and tracks with vca.track_add. Reversible through the "
		"ProjectJournal (the recorded inverse removes the group it created).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("name"), control::stringProperty()},
	});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("gain"), control::numberProperty()},
		{QStringLiteral("volume"), control::numberProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
		{QStringLiteral("phase_locked"), control::booleanProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
		{QStringLiteral("member_count"), control::integerProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
		{QStringLiteral("missing_tracks"), control::arrayProperty()},
		{QStringLiteral("missing_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return createGroup(args); };
	registry.registerCommand(cmd);
}

void registerVcaRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.remove");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete a group. Its member channels play at their own "
		"faders again (the published gain is reset), and the edit set goes with the group. "
		"The delete is EXACTLY reversible: one control.undo re-creates the group with its id, "
		"name, fader, mute, solo, phase-lock flag, member channels and edit tracks.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
	}, {QStringLiteral("group")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("removed"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("member_count"), control::integerProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
		{QStringLiteral("group_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return removeGroup(args); };
	registry.registerCommand(cmd);
}

void registerVcaList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.list");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("list");
	cmd.description = QStringLiteral("Every group of the mix with its whole state: the fader "
		"and the gain published from it, the mute and solo flags, the phase lock, the member "
		"mixer channels with the gain each one is being scaled by right now, and the edit set "
		"as trk-<n> ids (with the ids that no longer name a live track separated out). "
		"Writes nothing.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("groups"), control::arrayProperty()},
		{QStringLiteral("count"), control::integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return listGroups(args); };
	registry.registerCommand(cmd);
}

void registerVcaGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.get_state");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("One group addressed by its vca-<n> id: the name, the "
		"fader value, the gain actually published to the members (0 when the group is muted), "
		"the mute and solo flags, the phase lock, every member channel with the gain it is "
		"scaled by, and the edit set with the ids whose track no longer exists reported "
		"separately as missing_tracks. Writes nothing.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
	}, {QStringLiteral("group")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("gain"), control::numberProperty()},
		{QStringLiteral("volume"), control::numberProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
		{QStringLiteral("phase_locked"), control::booleanProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
		{QStringLiteral("member_count"), control::integerProperty()},
		{QStringLiteral("tracks"), control::arrayProperty()},
		{QStringLiteral("track_count"), control::integerProperty()},
		{QStringLiteral("missing_tracks"), control::arrayProperty()},
		{QStringLiteral("missing_count"), control::integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return getGroupState(args); };
	registry.registerCommand(cmd);
}

void registerVcaRename(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.rename");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("rename");
	cmd.description = QStringLiteral("Rename a group. The name is what a group is called in "
		"the project file's <vcagroup> element and what its fader, mute and solo display names "
		"are built from; it is not an address, so vca-<n> ids are unaffected. An empty name is "
		"refused, typed. Reversible through the ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
	}, {QStringLiteral("group"), QStringLiteral("name")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("previous_name"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return renameGroup(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerVcaCommands(ControlRegistry& registry)
{
	registerVcaCreate(registry);
	registerVcaRemove(registry);
	registerVcaList(registry);
	registerVcaGetState(registry);
	registerVcaRename(registry);
	// The two halves of the feature, each in its own translation unit (the
	// read/edit split the folder-track, warp, rack, comp and automation groups
	// follow, for the file-length ratchet): the fader/mute/solo/membership half,
	// then the phase-locked edit half.
	registerVcaMixCommands(registry);
	registerVcaEditCommands(registry);
}

} // namespace lmms
