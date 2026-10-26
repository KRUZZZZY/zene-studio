/*
 * ControlCommandsTrackFolderSets.cpp - the named visibility sets of the track.*
 *                                       group: track.visibility_set_save /
 *                                       _apply / _remove / _list
 *                                       (SPEC A11-A16; owner items 3+20+21).
 *
 * The engine half is TrackContainer's own visibility-set store
 * (include/TrackContainer.h, src/core/TrackContainer.cpp) driven by
 * Track::setVisible; this file is what makes it drivable, and it is the ONLY way
 * to reach it in 0.3.0 - there is no set switcher in the interface, which is the
 * UI-absence line the release notes and docs/KNOWN-LIMITATIONS.md carry.
 *
 * It is a separate translation unit from ControlCommandsTrackFolder.cpp because
 * this fork's file-length ratchet measures a file as a unit (Gate 7): the
 * folder's own verbs and these four are two halves of one group, the same split
 * the warp, rack, comp and automation groups follow.
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
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! One track's visible flag, as the recorded before-state keeps it.
using VisibleFlag = std::pair<int, bool>;

//! The current flags of every track of the song, for the A16 before-state of an
//! apply (a set writes EVERY track's flag, not only its members').
std::shared_ptr<std::vector<VisibleFlag>> captureVisibleFlags(TrackContainer* song)
{
	auto flags = std::make_shared<std::vector<VisibleFlag>>();
	for (Track* track : song->tracks())
	{
		flags->push_back(VisibleFlag{track->id(), track->isVisible()});
	}
	return flags;
}

//! \a ids as the trk-<n> strings every read of this group reports.
QJsonArray idList(const std::vector<int>& ids)
{
	QJsonArray listed;
	for (int id : ids) { listed.append(control::trackId(id)); }
	return listed;
}

//! One set, as the list and the mutating replies report it.
QJsonObject setState(const TrackContainer::VisibilitySet& set, const QString& active)
{
	QJsonObject state;
	state.insert(QStringLiteral("name"), set.name);
	state.insert(QStringLiteral("tracks"), idList(set.trackIds));
	state.insert(QStringLiteral("track_count"), static_cast<int>(set.trackIds.size()));
	state.insert(QStringLiteral("active"), set.name == active);
	return state;
}

//! Whether an existing definition names a track that a replayable save cannot resolve.
bool containsStaleTrack(TrackContainer* song, const std::vector<int>& ids)
{
	return std::any_of(ids.begin(), ids.end(), [song](int id) {
		return song->findTrackById(id) == nullptr;
	});
}

struct SaveSetInverse
{
	QString operation;
	QJsonObject arguments;
};

//! Choose a replayable command for the prior definition, or the recorded undo when it has stale IDs.
SaveSetInverse saveSetInverse(const QString& name, bool existed, bool hasStaleTrack,
	const std::vector<int>& previousIds)
{
	if (!existed)
	{
		return {QStringLiteral("track.visibility_set_remove"),
			QJsonObject{{QStringLiteral("name"), name}}};
	}
	if (hasStaleTrack) { return {QStringLiteral("control.undo"), QJsonObject{}}; }
	return {QStringLiteral("track.visibility_set_save"),
		QJsonObject{{QStringLiteral("name"), name},
			{QStringLiteral("tracks"), idList(previousIds)}}};
}

// ---------------------------------------------------------------------------
// track.visibility_set_save
// ---------------------------------------------------------------------------
ControlResult saveSet(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	if (name.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'name' must not be empty: a visibility set is named because "
				"track.visibility_set_apply addresses it by name"));
	}

	// Every named track must be LIVE: a set that carries an id nothing answers to
	// would apply as "hide everything" for that entry, which is a silently wrong
	// set rather than a refusal.
	std::vector<int> ids;
	QJsonArray listed;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray())
	{
		ControlResult error;
		Track* track = control::resolveTrack(value.toString(), &error);
		if (track == nullptr) { return error; }
		// A set is a SET, so naming one track twice still names it ONCE: the stored
		// definition, the `track_count` this reply reports and the shown/hidden counts
		// track.visibility_set_apply derives from it all count MEMBERS, and a repeated
		// id would make one track read as two of them.
		if (std::find(ids.begin(), ids.end(), track->id()) != ids.end()) { continue; }
		ids.push_back(track->id());
		listed.append(control::trackIdOf(track));
	}

	TrackContainer* song = Engine::getSong();
	const TrackContainer::VisibilitySet* previous = song->findVisibilitySet(name);
	const bool existed = previous != nullptr;
	const std::vector<int> previousIds = existed ? previous->trackIds : std::vector<int>();
	const bool previousContainsStaleTrack = existed && containsStaleTrack(song, previousIds);
	const SaveSetInverse inverse = saveSetInverse(name, existed, previousContainsStaleTrack, previousIds);

	// SPEC A16: a set is project state the Song's journal checkpoint does not
	// carry, so the inverse is a RECORDED ACTION - the captured definition
	// written back through the same call, or the set removed again when there was
	// none to restore.
	control::addUndoStep(
		[song, name, existed, previousIds]() {
			if (existed) { song->saveVisibilitySet(name, previousIds); }
			else { song->removeVisibilitySet(name); }
		},
		[song, name, ids]() { song->saveVisibilitySet(name, ids); });
	song->saveVisibilitySet(name, ids);

	QJsonObject result;
	result.insert(QStringLiteral("name"), name);
	result.insert(QStringLiteral("tracks"), listed);
	result.insert(QStringLiteral("track_count"), static_cast<int>(ids.size()));
	result.insert(QStringLiteral("replaced"), existed);
	// The inverse REPORTED to clients is one they can replay: the same call with
	// the definition captured before this write, when there was one - and
	// track.visibility_set_remove when this save CREATED the set, because writing
	// an empty definition back would leave an empty set rather than the absence
	// the call found.
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("existed"), existed},
				{QStringLiteral("tracks"), idList(previousIds)}},
			inverse.operation,
			inverse.arguments,
			true,
			QStringLiteral("action checkpoint: the recorded undo step restores the definition "
				"captured before the write, or removes the set when none existed. The reported "
				"inverse replays the saved definition when all its ids are live; otherwise "
				"control.undo restores stale ids without asking saveSet to resolve them")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.visibility_set_apply
// ---------------------------------------------------------------------------
ControlResult applySet(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	TrackContainer* song = Engine::getSong();
	if (song->findVisibilitySet(name) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no visibility set is named '%1' (track.visibility_set_list reports "
				"the sets this project holds)").arg(name));
	}

	const QString previousActive = song->activeVisibilitySet();
	const std::shared_ptr<std::vector<VisibleFlag>> flags = captureVisibleFlags(song);

	// SPEC A16: applying a set writes EVERY track's flag plus the active name, so
	// the step is a recorded action covering both - one agent command, one undo.
	control::addUndoStep(
		[song, flags, previousActive]() {
			for (const VisibleFlag& flag : *flags)
			{
				if (Track* track = song->findTrackById(flag.first))
				{
					track->setVisible(flag.second);
				}
			}
			song->setActiveVisibilitySet(previousActive);
			emit song->visibilityChanged();
		},
		[song, name]() {
			int shown = 0;
			int hidden = 0;
			song->applyVisibilitySet(name, &shown, &hidden);
		});
	int shown = 0;
	int hidden = 0;
	song->applyVisibilitySet(name, &shown, &hidden);

	QJsonObject result;
	result.insert(QStringLiteral("name"), name);
	result.insert(QStringLiteral("shown"), shown);
	result.insert(QStringLiteral("hidden"), hidden);
	result.insert(QStringLiteral("active"), song->activeVisibilitySet());
	result.insert(QStringLiteral("previous_active"), previousActive);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("active_before"), previousActive}},
			QStringLiteral("control.undo"),
			{},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes every track's "
				"captured visible flag and the previous active name back, because one apply "
				"changes the flag of EVERY track of the song and only the members' are in the "
				"set. The inverse REPORTED here is that step as control.undo: no single command "
				"puts many flags back. `visible` is a view flag: it mutes nothing and changes "
				"no render")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.visibility_show_all
// ---------------------------------------------------------------------------
ControlResult showAll(const QJsonObject&)
{
	TrackContainer* song = Engine::getSong();
	const QString previousActive = song->activeVisibilitySet();
	const std::shared_ptr<std::vector<VisibleFlag>> flags = captureVisibleFlags(song);
	// The apply shape: every track's flag and the active name are one recorded action.
	control::addUndoStep(
		[song, flags, previousActive]() {
			for (const VisibleFlag& flag : *flags)
			{
				if (Track* track = song->findTrackById(flag.first)) { track->setVisible(flag.second); }
			}
			song->setActiveVisibilitySet(previousActive);
			emit song->visibilityChanged();
		},
		[song]() { song->showAllTracks(nullptr); });
	int shown = 0;
	song->showAllTracks(&shown);

	QJsonObject result;
	result.insert(QStringLiteral("shown"), shown);
	result.insert(QStringLiteral("active"), song->activeVisibilitySet());
	result.insert(QStringLiteral("previous_active"), previousActive);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("active_before"), previousActive}},
			QStringLiteral("control.undo"),
			{},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes every track's captured "
				"visible flag and the previous active name back, as for track.visibility_set_apply. "
				"The inverse REPORTED here is that step as control.undo: showing every track is "
				"not undone by showing every track again")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.visibility_set_remove
// ---------------------------------------------------------------------------
ControlResult removeSet(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	TrackContainer* song = Engine::getSong();
	const TrackContainer::VisibilitySet* previous = song->findVisibilitySet(name);
	if (previous == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no visibility set is named '%1'").arg(name));
	}
	const std::vector<int> ids = previous->trackIds;
	const bool wasActive = song->activeVisibilitySet() == name;
	const bool inverseNeedsUndo = wasActive || containsStaleTrack(song, ids);
	control::addUndoStep(
		[song, name, ids, wasActive]() {
			song->saveVisibilitySet(name, ids);
			if (wasActive) { song->setActiveVisibilitySet(name); }
		},
		[song, name]() { song->removeVisibilitySet(name); });
	song->removeVisibilitySet(name);

	QJsonObject result;
	result.insert(QStringLiteral("name"), name);
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("tracks"), idList(ids));
	result.insert(QStringLiteral("track_count"), static_cast<int>(ids.size()));
	// The tracks the removed set had hidden stay hidden: dropping the set does
	// not change a flag, because the flags are the project's own state and a set
	// is a saved SELECTION over it. Said here so a caller cannot read the flag as
	// the set's shadow.
	result.insert(QStringLiteral("flags_unchanged"), true);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("tracks"), idList(ids)}},
			// The inverse REPORTED to clients is one they can replay. It is NOT this
			// command: the set is already gone, so replaying it refuses on the missing
			// name and restores nothing. An inactive set with live members can be
			// restored through saveSet; stale members or an active name need the exact
			// recorded step.
		inverseNeedsUndo ? QStringLiteral("control.undo") : QStringLiteral("track.visibility_set_save"),
			inverseNeedsUndo ? QJsonObject{} : QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("tracks"), idList(ids)}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes the captured "
				"definition back through the same call, so one control.undo restores the set "
				"and, when it was the active one, that fact too. The tracks' own visible flags "
				"are deliberately NOT touched in either direction: a set is a saved selection "
				"over them. The reported inverse is track.visibility_set_save when its ids are "
				"live and the set was inactive; otherwise control.undo restores the captured "
				"definition exactly, including stale ids or its active name")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// the registrations
// ---------------------------------------------------------------------------
void registerVisibilitySetSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.visibility_set_save");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("visibility_set_save");
	cmd.description = QStringLiteral("Create or replace a named visibility set: a named, "
		"id-based list of tracks that track.visibility_set_apply makes visible. Every named "
		"track must be live (a well-formed id naming none is not_found). Saved with the "
		"project, so a set survives project.save / project.open.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("tracks"), QJsonObject{
			{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), control::stringProperty()}}},
	}, {QStringLiteral("name"), QStringLiteral("tracks")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("replaced"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return saveSet(args); };
	registry.registerCommand(cmd);
}

void registerVisibilitySetApply(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.visibility_set_apply");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("visibility_set_apply");
	cmd.description = QStringLiteral("Make exactly the members of a named visibility set "
		"visible and hide every other track of the song. A visibility flag is a VIEW flag: it "
		"mutes nothing and changes no render (the registered transcript proves that with a "
		"negative control). An unknown name is not_found, typed.");
	cmd.argsSchema = control::objectSchema(
		{{QStringLiteral("name"), control::stringProperty()}}, {QStringLiteral("name")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("shown"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("hidden"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("active"), control::stringProperty()},
		{QStringLiteral("previous_active"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return applySet(args); };
	registry.registerCommand(cmd);
}

void registerVisibilityShowAll(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.visibility_show_all");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("visibility_show_all");
	cmd.description = QStringLiteral("Make every track of the song visible again and leave no set "
		"active - the way back from track.visibility_set_apply. A view flag only: it mutes nothing "
		"and changes no render. One control.undo puts the previous flags and active set back.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("shown"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("active"), control::stringProperty()},
		{QStringLiteral("previous_active"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return showAll(args); };
	registry.registerCommand(cmd);
}

void registerVisibilitySetRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.visibility_set_remove");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("visibility_set_remove");
	cmd.description = QStringLiteral("Delete a named visibility set. The tracks' own visible "
		"flags are NOT changed: a set is a saved selection over them, and the flags are the "
		"project's own state. An unknown name is not_found, typed.");
	cmd.argsSchema = control::objectSchema(
		{{QStringLiteral("name"), control::stringProperty()}}, {QStringLiteral("name")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("removed"), control::booleanProperty()},
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		// Measured (R6.3): the reply always carried it; the schema never declared it.
		{QStringLiteral("flags_unchanged"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return removeSet(args); };
	registry.registerCommand(cmd);
}

void registerVisibilitySetList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.visibility_set_list");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("visibility_set_list");
	cmd.description = QStringLiteral("Every named visibility set this project holds, with its "
		"members and which one is active. Writes nothing. Each track's own `visible` flag is "
		"reported by track.list, track.get_state and arrangement.get_state.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("sets"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("active"), control::stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		TrackContainer* song = Engine::getSong();
		const QString active = song->activeVisibilitySet();
		QJsonArray sets;
		for (const TrackContainer::VisibilitySet& set : song->visibilitySets())
		{
			sets.append(setState(set, active));
		}
		QJsonObject result;
		result.insert(QStringLiteral("sets"), sets);
		result.insert(QStringLiteral("count"), sets.size());
		result.insert(QStringLiteral("active"), active);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerTrackFolderSetCommands(ControlRegistry& registry)
{
	registerVisibilitySetSave(registry);
	registerVisibilitySetApply(registry);
	registerVisibilityShowAll(registry);
	registerVisibilitySetRemove(registry);
	registerVisibilitySetList(registry);
}

} // namespace lmms
