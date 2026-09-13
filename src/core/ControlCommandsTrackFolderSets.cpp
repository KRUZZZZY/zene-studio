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
		ids.push_back(track->id());
		listed.append(control::trackIdOf(track));
	}

	TrackContainer* song = Engine::getSong();
	const TrackContainer::VisibilitySet* previous = song->findVisibilitySet(name);
	const bool existed = previous != nullptr;
	const std::vector<int> previousIds = existed ? previous->trackIds : std::vector<int>();

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
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("existed"), existed},
				{QStringLiteral("tracks"), idList(previousIds)}},
			QStringLiteral("track.visibility_set_save"),
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("tracks"), idList(previousIds)}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes the definition "
				"captured before the write back through the same call (and removes the set "
				"again when there was none), so one control.undo restores the named set "
				"exactly")));
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
			QStringLiteral("track.visibility_set_apply"),
			QJsonObject{{QStringLiteral("active_before"), previousActive}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes every track's "
				"captured visible flag and the previous active name back, because one apply "
				"changes the flag of EVERY track of the song and only the members' are in the "
				"set. `visible` is a view flag: it mutes nothing and changes no render")));
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
			QStringLiteral("track.visibility_set_remove"),
			QJsonObject{{QStringLiteral("name"), name},
				{QStringLiteral("tracks"), idList(ids)}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step writes the captured "
				"definition back through the same call, so one control.undo restores the set "
				"and, when it was the active one, that fact too. The tracks' own visible flags "
				"are deliberately NOT touched in either direction: a set is a saved selection "
				"over them")));
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
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	}, {QStringLiteral("name")});
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
	registerVisibilitySetRemove(registry);
	registerVisibilitySetList(registry);
}

} // namespace lmms
