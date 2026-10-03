/*
 * ControlCommandsSessionPerform.cpp - release, jump, capture a scene, scene playlist, folder launch, setlist
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
 *
 */
/*! 0.5.0 L02 (W3), the launcher in performance:
 *  - session.release_slot (WF-063): the release half of a Gate or Repeat cell, so the grid and the
 *    socket can hold a cell as a controller does;
 *  - session.jump_slot (WF-070): a playing cell continues from a point inside its clip;
 *  - session.capture_scene (WF-068): the cells playing now become a new scene row;
 *  - session.set_playlist (WF-067): scenes that play one after another - each scene's chain jumps to
 *    the next (the last stops, or goes back to the first);
 *  - session.launch_group (WF-064): every cell of a folder's tracks in one scene, launched together;
 *  - session.setlist / setlist_go (WF-071): an ordered list of projects to play through - a
 *    preference, not part of any project. */

#include <functional>
#include <QJsonArray>
#include <QJsonDocument>

#include "ConfigManager.h"
#include "ControlCommandsSessionShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "ControlCommandsTrackFolderShared.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;
using trackfoldercontrol::insideFolder;
using namespace sessioncontrol;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args) { return ControlRegistry::instance()->invoke(id, args); }

//! The model and the cell the arguments name, or a failure in @a error.
SessionModel* cellOf(const QJsonObject& args, int* track, int* scene, ControlResult* error)
{
	SessionModel* model = sessionModelOrNull(error);
	if (model == nullptr) { return nullptr; }
	*track = args.value(QStringLiteral("track")).toInt();
	*scene = args.value(QStringLiteral("scene")).toInt();
	return gridContains(*model, *track, *scene, error) ? model : nullptr;
}

ControlResult release(const QJsonObject& args)
{
	int track = 0;
	int scene = 0;
	ControlResult error;
	SessionModel* model = cellOf(args, &track, &scene, &error);
	if (model == nullptr) { return error; }
	const LaunchRequest request = launchRequestOf(*model, model->slot(track, scene), {});
	if (!Engine::getSong()->sessionScheduler().requestRelease(track, scene, request.mode, request.quantisation))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the session command queue is full"));
	}
	return ControlResult::success({{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}, {QStringLiteral("mode"), modeName(request.mode)}});
}

ControlResult jump(const QJsonObject& args)
{
	int track = 0;
	int scene = 0;
	ControlResult error;
	if (cellOf(args, &track, &scene, &error) == nullptr) { return error; }
	const tick_t position = args.value(QStringLiteral("position_ticks")).toInt();
	if (!Engine::getSong()->sessionScheduler().requestJump(track, scene, position))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the session command queue is full"));
	}
	return ControlResult::success({{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene},
		{QStringLiteral("position_ticks"), static_cast<int>(position)}});
}

//! Record an edit of the <session> block as one undo step (the block before and after it).
void recordBlock(const QString& before, const SessionModel& model, const std::function<void()>& rearm = {})
{
	const QString after = sessionBlockXml(model);
	addUndoStep([before, rearm]() { if (Song* song = Engine::getSong()) { restoreSessionBlock(song->sessionModel(), before); if (rearm) { rearm(); } } },
		[after, rearm]() { if (Song* song = Engine::getSong()) { restoreSessionBlock(song->sessionModel(), after); if (rearm) { rearm(); } } });
	Engine::getSong()->setModified();
}

//! Arm each of @a scenes' follow chain in the scheduler when the model has one there, disarm it when not.
void armScenes(const std::vector<int>& scenes)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	SessionModel& grid = song->sessionModel();
	for (const int scene : scenes)
	{
		const bool chained = scene < grid.sceneCount() && !grid.scene(scene).followActions().empty();
		invoke(QStringLiteral("session.scene_follow_set"), {{QStringLiteral("scene"), scene}, {QStringLiteral("enabled"), chained}});
	}
}

ControlResult captureScene(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
	std::vector<std::pair<int, int>> playing;  // column, the scene it plays
	for (int track = 0; track < std::min(model->trackCount(), SessionScheduler::PublishedColumns); ++track)
	{
		const SessionScheduler::ColumnState state = scheduler.columnState(track);
		const bool sounding = state.phase == SlotPhase::Playing || state.phase == SlotPhase::StopPending;
		if (sounding && state.scene >= 0 && state.scene < model->sceneCount()) { playing.emplace_back(track, state.scene); }
	}
	if (playing.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no cell is playing")); }
	const QString before = sessionBlockXml(*model);
	const int row = model->sceneCount();
	model->setSceneCount(row + 1);
	// A full grid does not grow: refuse before anything lands in a scratch slot (review W3-batchA A3).
	if (model->sceneCount() <= row) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the grid holds no more scenes")); }
	for (const auto& [track, scene] : playing) { model->slot(track, row) = model->slot(track, scene); }
	model->scene(row).setName(args.value(QStringLiteral("name")).toString(QStringLiteral("Captured")));
	recordBlock(before, *model);
	return ControlResult::success({{QStringLiteral("scene"), row}, {QStringLiteral("captured"), static_cast<int>(playing.size())}});
}

//! The chain that moves scene list position @a i on: jump to the next, at the end stop or go back.
FollowAction playlistStep(const std::vector<int>& scenes, std::size_t i, bool loop, double bars)
{
	FollowAction step;
	const bool last = i + 1 == scenes.size();
	step.type = last && !loop ? FollowAction::Type::Stop : FollowAction::Type::Jump;
	step.jumpTo = last ? scenes.front() : scenes[i + 1];
	step.linked = bars <= 0.0;
	step.timeBars = bars > 0.0 ? bars : 1.0;
	return step;
}

ControlResult setPlaylist(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	std::vector<int> scenes;
	for (const QJsonValue& value : args.value(QStringLiteral("scenes")).toArray())
	{
		const int scene = value.toInt(-1);
		if (scene < 0 || scene >= model->sceneCount()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("scene %1 is not in the grid").arg(scene)); }
		scenes.push_back(scene);
	}
	if (scenes.empty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the 'scenes', in the order they play")); }
	const bool clear = args.value(QStringLiteral("clear")).toBool(false);
	const QString before = sessionBlockXml(*model);
	for (std::size_t i = 0; i < scenes.size(); ++i)
	{
		model->scene(scenes[i]).setFollowActions(clear ? std::vector<FollowAction>{}
			: std::vector<FollowAction>{playlistStep(scenes, i, args.value(QStringLiteral("loop")).toBool(false),
				args.value(QStringLiteral("bars")).toDouble(0.0))});
	}
	// Armed (or, cleared, disarmed) now, and again from whatever model an undo or redo puts back (review W3-batchA #5).
	const auto arm = [scenes]() { armScenes(scenes); };
	recordBlock(before, *model, arm);
	arm();
	return ControlResult::success({{QStringLiteral("scenes"), args.value(QStringLiteral("scenes"))}, {QStringLiteral("cleared"), clear}});
}

//! The folder track in arrangement position @a index, or nullptr.
const TrackFolder* folderColumn(int index)
{
	const auto& tracks = Engine::getSong()->tracks();
	return index >= 0 && index < static_cast<int>(tracks.size()) ? dynamic_cast<const TrackFolder*>(tracks[index]) : nullptr;
}

ControlResult launchGroup(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const auto& tracks = Engine::getSong()->tracks();
	const TrackFolder* folder = folderColumn(args.value(QStringLiteral("track")).toInt(-1));
	if (folder == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'track' is the column of a folder track")); }
	const int scene = args.value(QStringLiteral("scene")).toInt();
	if (scene < 0 || scene >= model->sceneCount()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("scene %1 is not in the grid").arg(scene)); }
	QJsonArray launched;
	for (int column = 0; column < std::min(model->trackCount(), static_cast<int>(tracks.size())); ++column)
	{
		if (!insideFolder(tracks[column], folder) || model->slot(column, scene).isEmpty()) { continue; }
		const ControlResult one = invoke(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), column}, {QStringLiteral("scene"), scene}});
		if (one.ok) { launched.append(column); }
	}
	return ControlResult::success({{QStringLiteral("launched"), launched}, {QStringLiteral("scene"), scene}});
}

QJsonObject setlistState()
{
	const QJsonObject stored = QJsonDocument::fromJson(ConfigManager::inst()->value(QStringLiteral("session"), QStringLiteral("setlist")).toUtf8()).object();
	return {{QStringLiteral("items"), stored.value(QStringLiteral("items")).toArray()}, {QStringLiteral("index"), stored.value(QStringLiteral("index")).toInt(-1)}};
}

void storeSetlist(const QJsonObject& state)
{
	ConfigManager::inst()->setValue(QStringLiteral("session"), QStringLiteral("setlist"),
		QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
}

ControlResult setlist(const QJsonObject& args)
{
	QJsonObject state = setlistState();
	if (args.contains(QStringLiteral("items")))
	{
		const QJsonObject before = state;
		state.insert(QStringLiteral("items"), args.value(QStringLiteral("items")).toArray());
		state.insert(QStringLiteral("index"), -1);
		storeSetlist(state);
		addUndoStep([before]() { storeSetlist(before); }, [state]() { storeSetlist(state); });
	}
	return ControlResult::success(state);
}

ControlResult setlistGo(const QJsonObject& args)
{
	QJsonObject state = setlistState();
	const QJsonArray items = state.value(QStringLiteral("items")).toArray();
	if (items.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the setlist is empty (session.setlist)")); }
	const QString to = args.value(QStringLiteral("to")).toString(QStringLiteral("next"));
	const int current = state.value(QStringLiteral("index")).toInt(-1);
	bool numeric = false;
	const int named = to.toInt(&numeric);
	const int index = to == QLatin1String("previous") ? std::max(0, current - 1)
		: to == QLatin1String("next") ? std::min(static_cast<int>(items.size()) - 1, current + 1) : numeric ? named : -1;
	if (index < 0 || index >= items.size()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("no setlist entry %1").arg(to)); }
	// project.open's own handler: this call is the one irreversible step, not two records of it.
	const ControlCommand* open = ControlRegistry::instance()->command(QStringLiteral("project.open"));
	const ControlResult opened = open->handler({{QStringLiteral("path"), items.at(index).toString()}});
	if (!opened.ok) { return opened; }
	state.insert(QStringLiteral("index"), index);
	storeSetlist(state);
	return ControlResult::success(state);
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray::fromStringList(required));
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionPerformCommands(ControlRegistry& registry)
{
	const QJsonObject cell{{QStringLiteral("track"), integerProperty(0, 255)}, {QStringLiteral("scene"), integerProperty(0, 511)}};
	const QStringList both{QStringLiteral("track"), QStringLiteral("scene")};
	add(registry, "session.release_slot", QStringLiteral("Let go of a cell: the release half of a press, which stops a Gate cell and "
		"ends a Repeat cell's retriggering (Trigger and Toggle cells ignore it). Not a project edit."), cell, both, false, release);
	QJsonObject jumpArgs = cell;
	jumpArgs.insert(QStringLiteral("position_ticks"), integerProperty(0, 192 * 1024));
	add(registry, "session.jump_slot", QStringLiteral("Move a playing cell: it continues from 'position_ticks' into its clip "
		"(192 a bar in 4/4), at once. A cell that is not playing is left alone. Not a project edit."), jumpArgs,
		{QStringLiteral("track"), QStringLiteral("scene"), QStringLiteral("position_ticks")}, false, jump);
	add(registry, "session.capture_scene", QStringLiteral("Make a new scene row from the cells playing now: each playing column's "
		"cell is copied into it ('name', Captured when left out). One undo step."),
		{{QStringLiteral("name"), stringProperty()}}, {}, true, captureScene);
	add(registry, "session.set_playlist", QStringLiteral("Play 'scenes' one after another: each one's chain jumps to the next after "
		"'bars' (the row's length when left out); the last stops, or with 'loop' goes back to the first. 'clear' takes the "
		"chains off those scenes instead. The chains are the scenes' own (session.set_scene follow_actions), armed at once. "
		"One undo step."),
		{{QStringLiteral("scenes"), arrayProperty()}, {QStringLiteral("bars"), numberProperty()}, {QStringLiteral("loop"), booleanProperty()},
			{QStringLiteral("clear"), booleanProperty()}}, {QStringLiteral("scenes")}, true, setPlaylist);
	add(registry, "session.launch_group", QStringLiteral("Launch, in 'scene', every cell of the tracks inside the folder track at "
		"column 'track' (its sub-folders too) - a group's cells as one. Not a project edit."), cell, both, false, launchGroup);
	add(registry, "session.setlist", QStringLiteral("The setlist: an ordered list of project files to play through ('items' replaces "
		"it), and the entry open now. A preference, kept in the config file, not in any project; a change is one undo step."),
		{{QStringLiteral("items"), arrayProperty()}}, {}, true, setlist);
	add(registry, "session.setlist_go", QStringLiteral("Open the setlist's 'to' entry: next (the default), previous, or an index. "
		"The project is opened as project.open opens it."), {{QStringLiteral("to"), stringProperty()}}, {}, true, setlistGo);
}

} // namespace lmms
