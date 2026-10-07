/*
 * ControlCommandsSessionSubScenes.cpp - session.subscene_* (0.5.0 L02, WF-064)
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

/*! 0.5.0 L02 (WF-064), sub-scenes - a named subset of one scene's cells, launched on its own:
 *  - session.subscene_create / subscene_rename / subscene_delete edit SessionModel::subScenes(), each ONE undo step
 *    (the <session> block before and after, as every session.* model edit records);
 *  - session.subscene_launch launches the sub-scene's cells, each exactly as session.launch_scene would launch it
 *    (the scene's quantisation: the cell's own, Global resolved against the session's), and records nothing.
 *  session.get_state lists them under `subscenes`; a project file keeps them in <z:subscenes> (SessionSubScenes.h). */

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlCommandsSessionShared.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "SessionModel.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

ControlResult invalid(const QString& message) { return ControlResult::failure(ControlErrorKind::InvalidArgs, message); }

//! The sub-scene 'subscene' names, as an index into the model's list, or -1 (with @a error).
int subSceneOf(const SessionModel& model, const QJsonObject& args, ControlResult* error)
{
	const int index = args.value(QStringLiteral("subscene")).toInt(-1);
	if (index >= 0 && index < static_cast<int>(model.subScenes().size())) { return index; }
	*error = invalid(QStringLiteral("no sub-scene %1: there are %2 (session.get_state 'subscenes')")
		.arg(index).arg(model.subScenes().size()));
	return -1;
}

//! 'tracks' as the sub-scene's columns, each inside the grid; empty (with @a error) when one is not.
std::vector<int> columnsOf(const SessionModel& model, const QJsonObject& args, ControlResult* error)
{
	std::vector<int> columns;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray())
	{
		const int track = value.toInt(-1);
		if (track < 0 || track >= model.trackCount())
		{
			*error = invalid(QStringLiteral("column %1 is outside the grid's %2 tracks").arg(value.toVariant().toString()).arg(model.trackCount()));
			return {};
		}
		columns.push_back(track);
	}
	if (columns.empty()) { *error = invalid(QStringLiteral("'tracks' names the sub-scene's columns, at least one")); }
	return SubScene::normalised(std::move(columns));
}

ControlResult createSubScene(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const int scene = args.value(QStringLiteral("scene")).toInt(-1);
	if (scene < 0 || scene >= model->sceneCount()) { return invalid(QStringLiteral("scene %1 is not in the grid").arg(scene)); }
	SubScene made;
	made.tracks = columnsOf(*model, args, &error);
	if (made.tracks.empty()) { return error; }
	if (static_cast<int>(model->subScenes().size()) >= SubScene::MaxCount)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a project holds at most %1 sub-scenes").arg(SubScene::MaxCount));
	}
	const int index = static_cast<int>(model->subScenes().size());
	made.scene = scene;
	made.name = args.value(QStringLiteral("name")).toString().trimmed();
	if (made.name.isEmpty()) { made.name = QStringLiteral("Sub-scene %1").arg(index + 1); }
	const QString before = sessionBlockXml(*model);
	std::vector<SubScene> list = model->subScenes();
	list.push_back(made);
	model->setSubScenes(std::move(list));
	recordSessionEdit(before, *model);
	return ControlResult::success(subSceneState(index, made));
}

ControlResult renameSubScene(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const int index = subSceneOf(*model, args, &error);
	if (index < 0) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return invalid(QStringLiteral("'name' is the sub-scene's new name, not empty")); }
	const QString before = sessionBlockXml(*model);
	std::vector<SubScene> list = model->subScenes();
	list[static_cast<std::size_t>(index)].name = name;
	model->setSubScenes(std::move(list));
	recordSessionEdit(before, *model);
	return ControlResult::success(subSceneState(index, model->subScenes()[static_cast<std::size_t>(index)]));
}

ControlResult deleteSubScene(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const int index = subSceneOf(*model, args, &error);
	if (index < 0) { return error; }
	const QString before = sessionBlockXml(*model);
	std::vector<SubScene> list = model->subScenes();
	const QJsonObject removed = subSceneState(index, list[static_cast<std::size_t>(index)]);
	list.erase(list.begin() + index);
	model->setSubScenes(std::move(list));
	recordSessionEdit(before, *model);
	return ControlResult::success({{QStringLiteral("removed"), removed}, {QStringLiteral("subscenes"), subSceneStates(*model)}});
}

//! The sub-scene's columns no launch can reach: outside the grid, past the song's tracks, or an empty cell.
QJsonArray skippedColumns(const SessionModel& model, const SubScene& subScene, int launchable)
{
	QJsonArray skipped;
	for (const int track : subScene.tracks)
	{
		if (track >= launchable || model.slot(track, subScene.scene).isEmpty()) { skipped.append(track); }
	}
	return skipped;
}

ControlResult launchSubScene(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	const int index = subSceneOf(*model, args, &error);
	if (index < 0) { return error; }
	const SubScene& subScene = model->subScenes()[static_cast<std::size_t>(index)];
	if (subScene.scene >= model->sceneCount())
	{
		return invalid(QStringLiteral("sub-scene %1's scene %2 is outside the grid's %3 scenes").arg(index).arg(subScene.scene).arg(model->sceneCount()));
	}
	Song& song = *Engine::getSong();
	const int launchable = launchableColumnCount(*model, song);
	int syncTick = 0;
	bool inSync = true;
	LaunchQuantisation quantisation = LaunchQuantisation::None;
	const QJsonArray launched = launchCells(*model, song, subScene.scene, subScene.tracks, launchable, args, &syncTick, &inSync, &quantisation);
	return ControlResult::success({{QStringLiteral("subscene"), index}, {QStringLiteral("name"), subScene.name},
		{QStringLiteral("scene"), subScene.scene}, {QStringLiteral("clips"), launched.size()}, {QStringLiteral("launched"), launched},
		{QStringLiteral("in_sync"), !launched.isEmpty() && inSync}, {QStringLiteral("sync_tick"), launched.isEmpty() ? 0 : syncTick},
		{QStringLiteral("skipped"), skippedColumns(*model, subScene, launchable)}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QStringLiteral("session.") + QLatin1String(verb), description,
		objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerSessionSubSceneCommands(ControlRegistry& registry)
{
	const QJsonObject which{{QStringLiteral("subscene"), integerProperty(0, SubScene::MaxCount - 1)}};
	add(registry, "subscene_create", QStringLiteral("Make a sub-scene: a named subset of one scene's cells - the cells of "
		"'scene' in the columns 'tracks' - that launches on its own (session.subscene_launch). 'name' defaults to "
		"\"Sub-scene N\". Its number is its place in session.get_state's 'subscenes'. One undo step."),
		{{QStringLiteral("scene"), integerProperty(0, 511)}, {QStringLiteral("tracks"), arrayProperty()}, {QStringLiteral("name"), stringProperty()}},
		{QStringLiteral("scene"), QStringLiteral("tracks")}, true, createSubScene);
	QJsonObject renamed = which;
	renamed.insert(QStringLiteral("name"), stringProperty());
	add(registry, "subscene_rename", QStringLiteral("Rename sub-scene 'subscene' to 'name'. One undo step."), renamed,
		{QStringLiteral("subscene"), QStringLiteral("name")}, true, renameSubScene);
	add(registry, "subscene_delete", QStringLiteral("Delete sub-scene 'subscene' (the later ones move up one place); its "
		"cells are untouched. One undo step."), which, {QStringLiteral("subscene")}, true, deleteSubScene);
	QJsonObject launched = which;
	launched.insert(QStringLiteral("mode"), enumProperty(modeNames()));
	launched.insert(QStringLiteral("quantisation"), enumProperty(perClipQuantisationNames()));
	add(registry, "subscene_launch", QStringLiteral("Launch sub-scene 'subscene': each of its cells exactly as "
		"session.launch_scene launches it - with the scene's quantisation (the cell's own, Global meaning the session's), "
		"or 'quantisation' / 'mode' for all of them - so they land on the lines the whole scene would. The other columns "
		"play on, and the row is not marked as the launched scene (its scene chain does not fire). 'skipped' names the "
		"columns with nothing to launch. Not a project edit."), launched, {QStringLiteral("subscene")}, false, launchSubScene);
}

} // namespace lmms
