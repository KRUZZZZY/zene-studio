/*
 * ControlCommandsSessionSceneFollow.cpp - R5.2: session.scene_follow_set, a SCENE's chain
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

/*! Its own file because ControlCommandsSessionFollow.cpp, the cells' half, is at the file-length
 *  ratchet. The shared parser (followChainFromJson) and state emitters live in
 *  ControlCommandsSessionShared.h; the engine half is src/core/SessionSceneFollow.cpp.
 */

#include <algorithm>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsSessionShared.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "SessionFollow.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

QJsonArray chainJson(const FollowPlan& plan)
{
	QJsonArray array;
	for (int i = 0; i < plan.count; ++i) { array.append(followActionState(plan.entries[i])); }
	return array;
}

/*! The plan scene_follow_set installs: the args' `actions` or the scene's own persisted
 *  chain, timed Linked by the row's longest cell loop. Empty on success, else the refusal. */
QString scenePlanFrom(const SessionModel& model, int scene, const QJsonObject& args, FollowPlan* plan,
	bool* fromArgs)
{
	plan->enabled = args.value(QStringLiteral("enabled")).toBool(true);
	plan->sceneCount = model.sceneCount();
	for (int track = 0; track < model.trackCount(); ++track)
	{
		plan->clipLengthTicks = std::max(plan->clipLengthTicks,
			static_cast<tick_t>(model.slot(track, scene).loopLength()));
	}
	std::vector<FollowAction> chain = model.scene(scene).followActions();
	*fromArgs = args.contains(QStringLiteral("actions"));
	QString reason;
	if (*fromArgs && !followChainFromJson(args.value(QStringLiteral("actions")), plan->sceneCount, &chain, &reason))
	{
		return reason;
	}
	if (plan->enabled && chain.empty())
	{
		return QStringLiteral("scene %1 has no Follow Action chain yet: session.set_scene "
			"follow_actions writes one, or pass 'actions' inline").arg(scene);
	}
	if (chain.size() > static_cast<std::size_t>(MaxFollowChainEntries))
	{
		return QStringLiteral("the scene's chain has %1 entries; the engine's table holds %2")
			.arg(chain.size()).arg(MaxFollowChainEntries);
	}
	plan->count = static_cast<int>(chain.size());
	std::copy(chain.begin(), chain.end(), plan->entries);
	return QString();
}

ControlResult sceneFollowSet(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	Song* song = Engine::getSong();
	const int scene = args.value(QStringLiteral("scene")).toInt();
	if (scene < 0 || scene >= model->sceneCount())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("scene %1 is outside the grid's %2 scenes; session.set_grid resizes it")
				.arg(scene).arg(model->sceneCount()));
	}
	FollowPlan plan;
	bool fromArgs = false;
	if (const QString refusal = scenePlanFrom(*model, scene, args, &plan, &fromArgs); !refusal.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, refusal);
	}
	SessionScheduler& scheduler = song->sessionScheduler();
	if (!scheduler.requestSceneFollowPlan(scene, plan))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the session command queue is full: the plan was dropped"));
	}
	QJsonObject result;
	result.insert(QStringLiteral("scene"), scene);
	result.insert(QStringLiteral("enabled"), plan.enabled);
	result.insert(QStringLiteral("queued"), true);
	result.insert(QStringLiteral("source"), fromArgs ? QStringLiteral("args") : QStringLiteral("scene"));
	result.insert(QStringLiteral("entries"), plan.count);
	result.insert(QStringLiteral("chain"), chainJson(plan));
	result.insert(QStringLiteral("step_ticks"),
		static_cast<int>(followActionTicks(plan, clockOf(*song).ticksPerBar)));
	result.insert(QStringLiteral("row_length_ticks"), static_cast<int>(plan.clipLengthTicks));
	result.insert(QStringLiteral("armed_scenes"), scheduler.armedFollowScenes());
	return ControlResult::success(result);
}

/*! R5.2 session.scene_follow_set - arm (or clear) a SCENE's chain, the row-level twin of
 *  follow_set. Linked timing follows the row's longest cell loop (one bar when no cell has
 *  one). Not a project edit, for follow_set's reason; the persisted chain is written by
 *  session.set_scene's `follow_actions`. */
void registerSceneFollowSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.scene_follow_set");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("scene_follow_set");
	cmd.description = QStringLiteral("Arm (or clear, with enabled: false) one SCENE's Follow Action "
		"chain. While that scene is the launched row (session.launch_scene), its chain fires at "
		"the action time and moves every column playing the row together - Stop, PlayAgain, or "
		"the row-addressing actions - and it takes precedence over the chains of the row's cells. "
		"Linked timing is the row's longest cell loop, one bar when none has one. Without "
		"'actions' the scene's own persisted chain (session.set_scene follow_actions) is used. "
		"Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("actions"), arrayProperty()},
	}, {QStringLiteral("scene")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("queued"), booleanProperty()},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("entries"), integerProperty()},
		{QStringLiteral("chain"), arrayProperty()},
		{QStringLiteral("step_ticks"), integerProperty()},
		{QStringLiteral("row_length_ticks"), integerProperty()},
		{QStringLiteral("armed_scenes"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return sceneFollowSet(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionSceneFollowCommands(ControlRegistry& registry)
{
	registerSceneFollowSet(registry);
}

} // namespace lmms
