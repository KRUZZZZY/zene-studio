/*
 * ControlCommandsSessionFollow.cpp - the session.* Follow Action commands:
 *                                    arm a cell's chain and read the engine's
 *                                    fires back (SPEC-zene-studio A3 and §4.1;
 *                                    board task #641, the #596 half).
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

/* WHAT THIS GROUP IS. The `FollowAction` chain has been part of the session
 * data layer since #594 and is persisted per slot (SessionClip.cpp's
 * `followactions` element), but nothing evaluated it. The evaluation is the
 * engine half (include/SessionFollow.h, src/core/SessionFollow.cpp); these two
 * ids are how an agent ARMS a chain and READS BACK what it did, and they are the
 * ONLY way to reach the evaluation - there is no Follow Action editor in this
 * release (docs/KNOWN-LIMITATIONS.md).
 *
 * WHY `follow_set` RECORDS NO TRANSACTION. It writes no project state. The
 * chain it installs lives in the audio thread's fixed plan table
 * (SessionScheduler::requestFollowPlan -> LaunchCommandType::Follow, a POD on
 * the existing lock-free command queue), exactly as a launch request does; the
 * PERSISTED chain is a ClipSlot attribute, and the command that writes THAT is
 * session.set_slot, whose own row carries the true_inverse class. Recording a
 * transaction here would put a reversible:false step on top of the undo stack
 * for an arm - the defect clip.select's row already documents.
 *
 * WHAT THE TWO HALVES OF THE REPLY ARE. `queued` is the model thread's answer
 * (the plan is in the queue). `armed_cells` / `armed_cells_mask` / `fires` /
 * `last_fire` are the engine's published reading and lag by up to one audio
 * period. The distinction is the same one session.launch_slot makes between the
 * tick it scheduled and the tick the audio thread observed.
 */

#include <algorithm>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

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

//! The chain entry an agent sent, or the slot's own as a fallback. `ok` is
//! false when the args name an action the vocabulary does not carry, when an
//! entry's numbers are outside their range, or when the chain is longer than the
//! engine's fixed table.
bool followActionFromJson(const QJsonObject& entry, int sceneCount, FollowAction* action,
	QString* error)
{
	const QJsonValue typeValue = entry.value(QStringLiteral("type"));
	if (!typeValue.isString())
	{
		*error = QStringLiteral("a Follow Action entry needs a 'type' string of %1")
			.arg(followActionNames().join(QStringLiteral(", ")));
		return false;
	}
	const int type = followActionNames().indexOf(typeValue.toString());
	if (type < 0)
	{
		*error = QStringLiteral("'%1' is not a Follow Action type; the vocabulary is %2")
			.arg(typeValue.toString()).arg(followActionNames().join(QStringLiteral(", ")));
		return false;
	}
	action->type = static_cast<FollowAction::Type>(type);

	if (entry.contains(QStringLiteral("chance")))
	{
		const double chance = entry.value(QStringLiteral("chance")).toDouble(1.0);
		if (chance < 0.0 || chance > 1.0)
		{
			*error = QStringLiteral("'chance' is a weight in 0..1, not %1").arg(chance);
			return false;
		}
		action->chance = chance;
	}
	if (entry.contains(QStringLiteral("linked")))
	{
		action->linked = entry.value(QStringLiteral("linked")).toBool(true);
	}
	if (entry.contains(QStringLiteral("time_bars")))
	{
		const double bars = entry.value(QStringLiteral("time_bars")).toDouble(1.0);
		if (bars < 0.0)
		{
			*error = QStringLiteral("'time_bars' is an Unlinked action time in bars and cannot "
				"be negative (%1)").arg(bars);
			return false;
		}
		action->timeBars = bars;
	}
	if (entry.contains(QStringLiteral("jump_to")))
	{
		const int jumpTo = entry.value(QStringLiteral("jump_to")).toInt();
		// An out-of-grid row is refused HERE rather than only at fire time: the
		// engine refuses it too (FollowOutcome::Refused, which is a measurement
		// rather than a silent retry), but a client that names a scene the grid
		// does not have has made an argument error, and the reply should say so
		// while the client can still see the grid's real height.
		if (jumpTo < 0 || (action->type == FollowAction::Type::Jump && jumpTo >= sceneCount))
		{
			*error = QStringLiteral("'jump_to' %1 is outside the grid's %2 scenes; "
				"session.set_grid resizes it").arg(jumpTo).arg(sceneCount);
			return false;
		}
		action->jumpTo = jumpTo;
	}
	return true;
}

} // namespace

namespace sessioncontrol
{

bool followChainFromJson(const QJsonValue& value, int sceneCount, std::vector<FollowAction>* chain,
	QString* error)
{
	if (!value.isArray())
	{
		*error = QStringLiteral("'follow_actions' is an array of Follow Action entries");
		return false;
	}
	const QJsonArray entries = value.toArray();
	// Refused, never truncated - the engine's rule (followPlanFromArgs below).
	if (entries.size() > MaxFollowChainEntries)
	{
		*error = QStringLiteral("the chain has %1 entries; the engine's table holds %2")
			.arg(entries.size()).arg(MaxFollowChainEntries);
		return false;
	}
	chain->clear();
	for (int i = 0; i < entries.size(); ++i)
	{
		FollowAction action;
		if (!entries.at(i).isObject()
			|| !followActionFromJson(entries.at(i).toObject(), sceneCount, &action, error))
		{
			*error = QStringLiteral("Follow Action entry %1: %2").arg(i)
				.arg(entries.at(i).isObject() ? *error : QStringLiteral("not an object"));
			return false;
		}
		chain->push_back(action);
	}
	return true;
}

} // namespace sessioncontrol

namespace
{

//! Fills `plan` from the args' `actions` array, or from the slot's own persisted
//! chain when the array is absent.
bool followPlanFromArgs(const SessionModel& model, const ClipSlot& slot,
	const QJsonObject& args, FollowPlan* plan, bool* fromArgs, QString* error)
{
	plan->enabled = args.value(QStringLiteral("enabled")).toBool(true);
	plan->clipLengthTicks = static_cast<tick_t>(slot.loopLength());
	plan->sceneCount = model.sceneCount();
	plan->launchMode = slot.launchMode();
	plan->quantisation = resolveQuantisation(slot.launchQuantisation(),
		model.globalLaunchQuantisation());

	const QJsonValue actions = args.value(QStringLiteral("actions"));
	*fromArgs = actions.isArray();
	if (!*fromArgs)
	{
		const std::vector<FollowAction>& chain = slot.followActions();
		if (plan->enabled && chain.empty())
		{
			*error = QStringLiteral("the cell at this address has no Follow Action chain yet: "
				"session.set_slot writes one, or pass 'actions' inline");
			return false;
		}
		if (chain.size() > static_cast<std::size_t>(MaxFollowChainEntries))
		{
			*error = QStringLiteral("the cell's chain has %1 entries; the engine's table holds %2")
				.arg(chain.size()).arg(MaxFollowChainEntries);
			return false;
		}
		plan->count = static_cast<int>(chain.size());
		for (int i = 0; i < plan->count; ++i) { plan->entries[i] = chain[static_cast<std::size_t>(i)]; }
		return true;
	}

	const QJsonArray entries = actions.toArray();
	// Refused, never truncated: a chain silently cut to the engine's bound would
	// weight the actions differently from the one the client wrote.
	if (entries.size() > MaxFollowChainEntries)
	{
		*error = QStringLiteral("the chain has %1 entries; the engine's table holds %2 and a "
			"longer chain is refused rather than truncated").arg(entries.size())
			.arg(MaxFollowChainEntries);
		return false;
	}
	plan->count = entries.size();
	for (int i = 0; i < plan->count; ++i)
	{
		if (!entries.at(i).isObject())
		{
			*error = QStringLiteral("Follow Action entry %1 is not an object").arg(i);
			return false;
		}
		if (!followActionFromJson(entries.at(i).toObject(), plan->sceneCount,
				&plan->entries[i], error))
		{
			*error = QStringLiteral("Follow Action entry %1: %2").arg(i).arg(*error);
			return false;
		}
	}
	return true;
}

QJsonArray followPlanJson(const FollowPlan& plan)
{
	QJsonArray array;
	for (int i = 0; i < plan.count; ++i)
	{
		array.append(followActionState(plan.entries[i]));
	}
	return array;
}

//! The engine's published reading - the half of the state a client cannot see in
//! the model: which cells are armed, how many actions have fired and what the
//! newest one did. Every field is an atomic (SessionScheduler.h); the audio
//! thread's plan table is deliberately NOT read from here.
QJsonObject followEngineState(const SessionScheduler& scheduler)
{
	QJsonObject state;
	state.insert(QStringLiteral("armed_cells"), scheduler.armedFollowCells());
	// A 64-bit mask does not survive a JSON double, so it travels as a decimal
	// string. The bit rule is bit (track * 8 + scene), for track < 8 and
	// scene < 8 - armedFollowCells() counts the cells the mask cannot address.
	state.insert(QStringLiteral("armed_cells_mask"),
		QString::number(static_cast<qulonglong>(scheduler.armedFollowCellsMask())));
	state.insert(QStringLiteral("fires"), static_cast<double>(scheduler.followFires()));
	const std::uint64_t packed = scheduler.lastFollowFire();
	QJsonObject fire;
	fire.insert(QStringLiteral("outcome"), QString::fromLatin1(followOutcomeName(
		followFireOutcome(packed))));
	fire.insert(QStringLiteral("index"), followFireIndex(packed));
	fire.insert(QStringLiteral("target_scene"), followFireTargetScene(packed));
	fire.insert(QStringLiteral("tick"), static_cast<int>(followFireTick(packed)));
	state.insert(QStringLiteral("last_fire"), fire);
	state.insert(QStringLiteral("max_plans"), MaxFollowPlans);
	// R5.2: the row whose chain is running (-1: none), and the scene chains' own count.
	state.insert(QStringLiteral("active_scene"), scheduler.activeScene());
	state.insert(QStringLiteral("armed_scenes"), scheduler.armedFollowScenes());
	state.insert(QStringLiteral("scene_fires"), static_cast<double>(scheduler.sceneFollowFires()));
	return state;
}

void registerFollowSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.follow_set");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("follow_set");
	cmd.description = QStringLiteral("Arm (or clear, with enabled: false) one cell's Follow Action "
		"chain: the engine stores the chain and evaluates it while the cell plays, firing at the "
		"chain's action time. Without 'actions' the cell's own persisted chain is used, which is "
		"what session.set_slot wrote. Refused rather than truncated when the chain is longer than "
		"the engine's table. 'queued' is the model thread's answer; armed_cells and the rest are "
		"the engine's published reading, up to one audio period later. Not a project edit: no "
		"transaction is recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("actions"), arrayProperty()},
	}, {QStringLiteral("track"), QStringLiteral("scene")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), integerProperty()},
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("queued"), booleanProperty()},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("entries"), integerProperty()},
		{QStringLiteral("chain"), arrayProperty()},
		{QStringLiteral("step_ticks"), integerProperty()},
		{QStringLiteral("clip_length_ticks"), integerProperty()},
		{QStringLiteral("scene_count"), integerProperty()},
		{QStringLiteral("armed_cells"), integerProperty()},
		{QStringLiteral("armed_cells_mask"), stringProperty()},
		{QStringLiteral("fires"), integerProperty()},
		{QStringLiteral("max_plans"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		Song* song = Engine::getSong();
		const int track = args.value(QStringLiteral("track")).toInt();
		const int scene = args.value(QStringLiteral("scene")).toInt();
		if (!gridContains(*model, track, scene, &error)) { return error; }

		FollowPlan plan;
		bool fromArgs = false;
		QString reason;
		if (!followPlanFromArgs(*model, model->slot(track, scene), args, &plan, &fromArgs, &reason))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		}
		SessionScheduler& scheduler = song->sessionScheduler();
		const bool queued = scheduler.requestFollowPlan(track, scene, plan);
		if (!queued)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the session command queue is full: the plan was dropped, and "
					"session.get_state reports how many commands have been dropped"));
		}

		QJsonObject result;
		result.insert(QStringLiteral("track"), track);
		result.insert(QStringLiteral("scene"), scene);
		result.insert(QStringLiteral("enabled"), plan.enabled);
		result.insert(QStringLiteral("queued"), true);
		result.insert(QStringLiteral("source"),
			fromArgs ? QStringLiteral("args") : QStringLiteral("slot"));
		result.insert(QStringLiteral("entries"), plan.count);
		result.insert(QStringLiteral("chain"), followPlanJson(plan));
		// The chain's TIMING is its first entry's (SessionFollow.h states the
		// rule): Linked follows the cell's own loop length, falling back to one
		// bar when the cell has no length the model knows.
		const SessionClockContext ctx = clockOf(*song);
		result.insert(QStringLiteral("step_ticks"),
			static_cast<int>(followActionTicks(plan, ctx.ticksPerBar)));
		result.insert(QStringLiteral("clip_length_ticks"),
			static_cast<int>(plan.clipLengthTicks));
		result.insert(QStringLiteral("scene_count"), plan.sceneCount);
		// The engine's half of the reply is the published reading BEFORE this
		// install is drained; it is a reading, not a promise (see the id's
		// description).
		const QJsonObject engine = followEngineState(scheduler);
		result.insert(QStringLiteral("armed_cells"),
			engine.value(QStringLiteral("armed_cells")));
		result.insert(QStringLiteral("armed_cells_mask"),
			engine.value(QStringLiteral("armed_cells_mask")));
		result.insert(QStringLiteral("fires"), engine.value(QStringLiteral("fires")));
		result.insert(QStringLiteral("max_plans"), engine.value(QStringLiteral("max_plans")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
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
	cmd.handler = [](const QJsonObject& args) {
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
		plan.enabled = args.value(QStringLiteral("enabled")).toBool(true);
		plan.sceneCount = model->sceneCount();
		for (int track = 0; track < model->trackCount(); ++track)
		{
			plan.clipLengthTicks = std::max(plan.clipLengthTicks,
				static_cast<tick_t>(model->slot(track, scene).loopLength()));
		}
		std::vector<FollowAction> chain = model->scene(scene).followActions();
		const bool fromArgs = args.contains(QStringLiteral("actions"));
		QString reason;
		if (fromArgs && !followChainFromJson(args.value(QStringLiteral("actions")), plan.sceneCount,
				&chain, &reason))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, reason);
		}
		if (plan.enabled && chain.empty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("scene %1 has no Follow Action chain yet: session.set_scene "
					"follow_actions writes one, or pass 'actions' inline").arg(scene));
		}
		if (chain.size() > static_cast<std::size_t>(MaxFollowChainEntries))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("the scene's chain has %1 entries; the engine's table holds %2")
					.arg(chain.size()).arg(MaxFollowChainEntries));
		}
		plan.count = static_cast<int>(chain.size());
		for (int i = 0; i < plan.count; ++i) { plan.entries[i] = chain[static_cast<std::size_t>(i)]; }
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
		result.insert(QStringLiteral("chain"), followPlanJson(plan));
		result.insert(QStringLiteral("step_ticks"),
			static_cast<int>(followActionTicks(plan, clockOf(*song).ticksPerBar)));
		result.insert(QStringLiteral("row_length_ticks"), static_cast<int>(plan.clipLengthTicks));
		result.insert(QStringLiteral("armed_scenes"), scheduler.armedFollowScenes());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

/*! session.set_follow_actions - Live's "Enable Follow Actions Globally".
 *
 *  With it OFF every chain is INERT rather than cleared: the plans stay
 *  installed, the armed-cell count still reports them, and switching it back on
 *  resumes the next period with the chains exactly as they were. That is what
 *  makes it a performance control rather than an edit - which is also why it is
 *  engine state, not project state: nothing is persisted and control.undo has
 *  nothing to reverse (the midi.learn_toggle rule).
 */
void registerSetFollowActions(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.set_follow_actions");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("set_follow_actions");
	cmd.description = QStringLiteral("Turn Follow Actions on or off for the WHOLE session: with "
		"'enabled' false every cell's chain is inert - nothing fires - while the chains stay "
		"installed, so turning it back on resumes them where they were rather than restarting "
		"them. Engine state, not project state: not saved with the project and not journalled, so "
		"there is no undo step to reverse. session.follow_get_state reports it back.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
	}, {QStringLiteral("enabled")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("armed_cells"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		if (song == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::Requires,
				QStringLiteral("no song: there is no session to toggle"));
		}
		const bool enabled = args.value(QStringLiteral("enabled")).toBool();
		song->sessionScheduler().setFollowActionsEnabled(enabled);
		QJsonObject result;
		result.insert(QStringLiteral("enabled"), enabled);
		result.insert(QStringLiteral("armed_cells"),
			song->sessionScheduler().armedFollowCells());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


void registerFollowGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.follow_get_state");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("follow_get_state");
	cmd.description = QStringLiteral("Read the Follow Action engine back: `follow_actions_enabled` is Live's global toggle (with it "
		"off every chain is inert, not cleared), which cells are armed "
		"(the count and the bit(track * 8 + scene) mask), how many actions have fired, what the "
		"newest fire did (its outcome, the chain entry that produced it, the scene it addressed "
		"and the tick it was scheduled for), and - when track and scene are given - that cell's "
		"own state and the chain's action time. The mask is a decimal string because a 64-bit "
		"mask does not survive a JSON number. Read-only.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("follow_actions_enabled"), booleanProperty()},
		{QStringLiteral("armed_cells"), integerProperty()},
		{QStringLiteral("armed_cells_mask"), stringProperty()},
		{QStringLiteral("fires"), integerProperty()},
		{QStringLiteral("last_fire"), objectProperty()},
		{QStringLiteral("max_plans"), integerProperty()},
		{QStringLiteral("ticks_per_bar"), integerProperty()},
		{QStringLiteral("track"), integerProperty()},
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("armed_mask_covers_cell"), booleanProperty()},
		{QStringLiteral("cell"), objectProperty()},
		{QStringLiteral("chain"), arrayProperty()},
		{QStringLiteral("chain_step_ticks"), integerProperty()},
		{QStringLiteral("active_scene"), integerProperty()},
		{QStringLiteral("armed_scenes"), integerProperty()},
		{QStringLiteral("scene_fires"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		Song* song = Engine::getSong();
		const SessionScheduler& scheduler = song->sessionScheduler();
		const SessionClockContext ctx = clockOf(*song);

		QJsonObject result = followEngineState(scheduler);
		result.insert(QStringLiteral("follow_actions_enabled"),
			song->sessionScheduler().followActionsEnabled());
		result.insert(QStringLiteral("ticks_per_bar"), static_cast<int>(ctx.ticksPerBar));

		// Addressing one cell is optional: the engine reading above answers for
		// the whole grid, and the cell reading adds what only the model knows -
		// the persisted chain and the action time it implies.
		if (!args.contains(QStringLiteral("track")) && !args.contains(QStringLiteral("scene")))
		{
			return ControlResult::success(result);
		}
		const int track = args.value(QStringLiteral("track")).toInt();
		const int scene = args.value(QStringLiteral("scene")).toInt();
		if (!gridContains(*model, track, scene, &error)) { return error; }
		const ClipSlot& slot = model->slot(track, scene);
		result.insert(QStringLiteral("track"), track);
		result.insert(QStringLiteral("scene"), scene);

		const bool covered = track >= 0 && track < 8 && scene >= 0 && scene < 8;
		const std::uint64_t mask = scheduler.armedFollowCellsMask();
		result.insert(QStringLiteral("armed_mask_covers_cell"), covered);
		result.insert(QStringLiteral("armed"),
			covered && (mask & (std::uint64_t{ 1 } << (track * 8 + scene))) != 0);
		result.insert(QStringLiteral("cell"), clipSlotState(track, scene, slot));

		// The action time the cell's OWN persisted chain implies, computed with
		// the same pure function the audio thread uses (SessionFollow.h), so the
		// reply is the engine's arithmetic rather than a second implementation.
		FollowPlan plan;
		plan.enabled = !slot.followActions().empty();
		plan.count = slot.followActions().size() > static_cast<std::size_t>(MaxFollowChainEntries)
			? MaxFollowChainEntries : static_cast<int>(slot.followActions().size());
		for (int i = 0; i < plan.count; ++i)
		{
			plan.entries[i] = slot.followActions()[static_cast<std::size_t>(i)];
		}
		plan.clipLengthTicks = static_cast<tick_t>(slot.loopLength());
		plan.sceneCount = model->sceneCount();
		result.insert(QStringLiteral("chain"), followPlanJson(plan));
		result.insert(QStringLiteral("chain_step_ticks"),
			static_cast<int>(followActionTicks(plan, ctx.ticksPerBar)));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionFollowCommands(ControlRegistry& registry)
{
	registerFollowSet(registry);
	registerSceneFollowSet(registry);
	registerSetFollowActions(registry);
	registerFollowGetState(registry);
}

} // namespace lmms
