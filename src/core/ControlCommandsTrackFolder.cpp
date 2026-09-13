/*
 * ControlCommandsTrackFolder.cpp - the folder half of the track.* group:
 *                                   track.set_folder, track.folder_set_collapsed,
 *                                   track.set_routing, track.set_pinned and
 *                                   track.folder_get_state
 *                                   (SPEC A11-A16; owner items 3+20+21).
 *
 * The engine half is include/TrackFolder.h and src/tracks/TrackFolder.cpp; this
 * file is what makes it drivable, and it is the ONLY way to reach it in 0.3.0 -
 * there is no interface for folders in this release, which is the UI-absence
 * line docs/RELEASE-NOTES-v0.3.0-alpha.md and docs/KNOWN-LIMITATIONS.md carry.
 * The named visibility sets are the group's other half and live in
 * ControlCommandsTrackFolderSets.cpp (the read/edit split the warp, rack, comp
 * and automation groups follow, for the file-length ratchet).
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

#include "AutomatableModel.h"
#include "ControlEdit.h"
#include "ControlCommandsTrackFolderShared.h"  // the group's shared helpers
#include "ControlRegistry.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h
using namespace trackfoldercontrol;  // this group's helpers

namespace
{


//! Resolve the `folder` argument of track.set_folder into a destination:
//! nullptr means the container root, and a target that is not a folder (or would
//! close a cycle) is a typed Refused rather than a silent no-op.
ControlResult destinationFolder(const QJsonObject& args, Track* child, TrackFolder** outFolder)
{
	const QString folderId = args.value(QStringLiteral("folder")).toString();
	if (folderId.isEmpty()) { *outFolder = nullptr; return ControlResult::success(); }
	ControlResult error;
	Track* named = control::resolveTrack(folderId, &error);
	if (named == nullptr) { return error; }
	if (named->type() != Track::Type::Folder)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("trk-%1 is a %2 track, not a folder")
				.arg(named->id()).arg(control::trackTypeNameOf(named->type())));
	}
	TrackFolder* folder = static_cast<TrackFolder*>(named);
	QString reason;
	if (!folder->canHold(child, &reason))
	{
		return ControlResult::failure(ControlErrorKind::Refused, reason);
	}
	*outFolder = folder;
	return ControlResult::success();
}

// ---------------------------------------------------------------------------
// track.set_folder - the reparenting operation.
// ---------------------------------------------------------------------------
ControlResult setFolder(const QJsonObject& args)
{
	ControlResult error;
	Track* child = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (child == nullptr) { return error; }
	TrackFolder* folder = nullptr;
	const ControlResult destination = destinationFolder(args, child, &folder);
	if (!destination.ok) { return destination; }

	TrackFolder* previous = child->parentFolder();
	// SPEC A16: the before-state is the parent the child was in, and the inverse
	// is a RECORDED ACTION - the relation lives on the child's own element, but
	// re-loading that element cannot re-link it without a post-load resolution
	// pass, so the step re-parents through the same call the write uses. The
	// container index is deliberately NOT touched: a child keeps its row in the
	// flat list, which is what makes this inverse exact (there is no position to
	// restore) - docs/TRACK-FOLDER-DESIGN.md section 8.3.
	auto childRef = std::make_shared<Track*>(child);
	auto previousRef = std::make_shared<TrackFolder*>(previous);
	auto folderRef = std::make_shared<TrackFolder*>(folder);
	control::addUndoStep(
		[childRef, previousRef]() {
			if (*childRef != nullptr) { (*childRef)->setParentFolder(*previousRef); }
		},
		[childRef, folderRef]() {
			if (*childRef != nullptr) { (*childRef)->setParentFolder(*folderRef); }
		});
	child->setParentFolder(folder);

	QJsonObject before;
	before.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	before.insert(QStringLiteral("folder"), previous != nullptr
		? control::trackIdOf(previous) : QString());

	// The reply reports the folder the track ended up in, or the one it left when
	// it ended up at the container ROOT - and when it was already at the root
	// there is no folder to report at all, which must be a valid answer rather
	// than a dereference of nullptr (measured: taking an already-parentless track
	// out of "no folder" crashed the engine).
	QJsonObject result;
	if (folder != nullptr) { result = folderState(folder); }
	else if (previous != nullptr) { result = folderState(previous); }
	result.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	result.insert(QStringLiteral("index"), trackIndexInSong(child));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("track.set_folder"),
			QJsonObject{{QStringLiteral("track"), args.value(QStringLiteral("track")).toString()},
				{QStringLiteral("folder"), previous != nullptr
					? control::trackIdOf(previous) : QString()}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step re-parents the track to the "
				"folder it was in (or to the container root when it was in none) through the same "
				"call the write uses. The track keeps its row in the flat container list, so "
				"membership is the whole of the state this command changes")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.folder_set_collapsed / track.set_pinned - the two persisted flags.
// Both write only the folder's own <trackfolder> element, so a live Track
// checkpoint is a genuine inverse: Track::saveState captures that element before
// the write and restoreState re-loads it, and loadTrackSpecificSettings RESETS
// each field on absence (which is what lets one control.undo take the flag back
// off the first time it is set).
// ---------------------------------------------------------------------------
ControlResult setCollapsed(const QJsonObject& args)
{
	ControlResult error;
	TrackFolder* folder = resolveFolder(args, &error);
	if (folder == nullptr) { return error; }
	const bool value = args.value(QStringLiteral("collapsed")).toBool();
	const bool previous = folder->isCollapsed();
	folder->addJournalCheckPoint();
	folder->setCollapsed(value);

	QJsonObject result = folderState(folder);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("collapsed"), previous}},
			QStringLiteral("track.folder_set_collapsed"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("collapsed"), previous}},
			true, QStringLiteral("ProjectJournal (Track checkpoint: the folder's own "
				"<trackfolder> element carries collapsed)")));
	return ControlResult::success(result);
}

ControlResult setPinned(const QJsonObject& args)
{
	ControlResult error;
	TrackFolder* folder = resolveFolder(args, &error);
	if (folder == nullptr) { return error; }
	const bool value = args.value(QStringLiteral("pinned")).toBool();
	const bool previous = folder->isPinned();
	folder->addJournalCheckPoint();
	folder->setPinned(value);

	QJsonObject result = folderState(folder);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("pinned"), previous}},
			QStringLiteral("track.set_pinned"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("pinned"), previous}},
			true, QStringLiteral("ProjectJournal (Track checkpoint: the folder's own "
				"<trackfolder> element carries pinned)")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.set_routing - the mode switch.
// ---------------------------------------------------------------------------
ControlResult setRouting(const QJsonObject& args)
{
	ControlResult error;
	TrackFolder* folder = resolveFolder(args, &error);
	if (folder == nullptr) { return error; }
	const bool routing = args.value(QStringLiteral("routing")).toBool();
	const bool previous = folder->isRouting();
	if (previous == routing)
	{
		// A no-op still records an honest transaction: an undo of a call that
		// changed nothing must change nothing, so the recorded inverse is the
		// state the folder is already in rather than nothing at all.
		QJsonObject unchanged = folderState(folder);
		unchanged.insert(QStringLiteral("__transaction"),
			control::transactionPayload(
				QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
					{QStringLiteral("routing"), previous}},
				QStringLiteral("track.set_routing"),
				QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
					{QStringLiteral("routing"), previous}},
				true, QStringLiteral("nothing changed: the folder was already in the requested "
					"mode, so the recorded inverse is the state it is already in")));
		return ControlResult::success(unchanged);
	}

	// SPEC A16: the mode is in the folder's own XML, but the CHILDREN's channel
	// bindings are not, so a folder checkpoint would restore half of the action -
	// one agent command would cost more than one Ctrl+Z. The step is a recorded
	// action that switches the mode back through the same call, which restores
	// the mode AND every child's channel as one undo.
	auto folderRef = std::make_shared<TrackFolder*>(folder);
	control::addUndoStep(
		[folderRef, previous]() {
			QString ignored;
			(*folderRef)->setMode(previous ? TrackFolder::Mode::Routing : TrackFolder::Mode::Group,
				&ignored);
		},
		[folderRef, routing]() {
			QString ignored;
			(*folderRef)->setMode(routing ? TrackFolder::Mode::Routing : TrackFolder::Mode::Group,
				&ignored);
		});

	QString refusal;
	const QJsonArray before = childChannels(folder);
	if (!folder->setMode(routing ? TrackFolder::Mode::Routing : TrackFolder::Mode::Group, &refusal))
	{
		return ControlResult::failure(ControlErrorKind::Refused, refusal);
	}

	QJsonObject result = folderState(folder);
	result.insert(QStringLiteral("previous_routing"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("routing"), previous},
				{QStringLiteral("tracks"), before}},
			QStringLiteral("track.set_routing"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(folder)},
				{QStringLiteral("routing"), previous}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step switches the mode back "
				"through the same call, which restores the mode AND every child's own mixer "
				"channel as ONE step (the bindings are captured in the folder's `prevch` "
				"attribute before the write). LIMIT: a channel released by the switch is "
				"deleted; switching back creates a fresh one, so a child can come back on a "
				"different channel INDEX while still being routed through the folder "
				"(Mixer::deleteChannel renumbers), exactly as track.add's re-add takes a "
				"fresh trk-<n>")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// track.folder_get_state - the read.
// ---------------------------------------------------------------------------
void registerTrackSetFolder(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_folder");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_folder");
	cmd.description = QStringLiteral("Put a track into a folder, or take it out of one with an "
		"empty 'folder' (the container root). The folder relation is an attribute on the child's "
		"own <track> element and the child keeps its row in the flat track list, so this changes "
		"membership only. A folder is a track of type folder (track.add type=folder); a target "
		"that is not one is refused, typed, and a relation that would close a cycle is refused "
		"too.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("folder"), control::stringProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("index"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("children"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("child_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setFolder(args); };
	registry.registerCommand(cmd);
}

void registerTrackFolderSetCollapsed(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.folder_set_collapsed");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("folder_set_collapsed");
	cmd.description = QStringLiteral("Collapse or expand a folder. This is the flag the track "
		"list's collapse would read; 0.3.0 persists it and drives it from here only (no chevron "
		"is drawn). Reversible through the ProjectJournal (the folder's own Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("collapsed"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("collapsed")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("collapsed"), control::booleanProperty()},
		{QStringLiteral("child_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setCollapsed(args); };
	registry.registerCommand(cmd);
}

void registerTrackSetRouting(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_routing");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_routing");
	cmd.description = QStringLiteral("Switch a folder between its two modes: false is GROUP - "
		"the organisational mode and the default - where each child keeps its own mixer channel, "
		"and true is ROUTING, where the folder takes a mixer channel of its own and every child's "
		"output is summed through it. Refused, typed, when the track is not a folder or when a "
		"routing folder has no children to sum.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("routing"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("routing")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("mode"), control::stringProperty()},
		{QStringLiteral("routing"), control::booleanProperty()},
		{QStringLiteral("previous_routing"), control::booleanProperty()},
		{QStringLiteral("mixer_channel"), control::integerProperty()},
		{QStringLiteral("children"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setRouting(args); };
	registry.registerCommand(cmd);
}

void registerTrackSetPinned(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_pinned");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_pinned");
	cmd.description = QStringLiteral("Pin or unpin a folder. This is the flag a non-scrolling "
		"pinned strip would read; 0.3.0 persists it and drives it from here only. Reversible "
		"through the ProjectJournal (the folder's own Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("pinned"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("pinned")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("pinned"), control::booleanProperty()},
		{QStringLiteral("child_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setPinned(args); };
	registry.registerCommand(cmd);
}

//! One folder's whole state: the mode, the two flags, the channel it owns and
//! every child with the channel it is on.
void registerTrackFolderGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.folder_get_state");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("folder_get_state");
	cmd.description = QStringLiteral("One folder addressed by its trk-<n> id: its mode (group or "
		"routing), the pinned and collapsed flags, the mixer channel it owns in routing mode, and "
		"every child with the channel that child is on. track.list, track.get_state and "
		"arrangement.get_state also carry each track's `folder`, so the relation is readable from "
		"the arrangement view of the song as well. Writes nothing.");
	cmd.argsSchema = control::objectSchema(
		{{QStringLiteral("track"), control::stringProperty()}}, {QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("mode"), control::stringProperty()},
		{QStringLiteral("routing"), control::booleanProperty()},
		{QStringLiteral("collapsed"), control::booleanProperty()},
		{QStringLiteral("pinned"), control::booleanProperty()},
		{QStringLiteral("mixer_channel"), control::integerProperty()},
		{QStringLiteral("child_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("children"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		TrackFolder* folder = resolveFolder(args, &error);
		if (folder == nullptr) { return error; }
		return ControlResult::success(folderState(folder));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerTrackFolderCommands(ControlRegistry& registry)
{
	registerTrackSetFolder(registry);
	registerTrackFolderSetCollapsed(registry);
	registerTrackSetRouting(registry);
	registerTrackSetPinned(registry);
	registerTrackFolderGetState(registry);
	// The named visibility sets are the group's other half, in their own
	// translation unit (the read/edit split the warp, rack, comp and automation
	// groups follow, for the file-length ratchet).
	registerTrackFolderSetCommands(registry);
}

} // namespace lmms
