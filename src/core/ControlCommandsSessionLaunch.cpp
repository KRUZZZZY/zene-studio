/*
 * ControlCommandsSessionLaunch.cpp - the session.* launch commands: launch a
 *                                    clip, launch a scene, stop a clip, stop
 *                                    the session (SPEC A11-A16,
 *                                    SPEC-zene-studio A2/A3).
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

/* The launch half of the group, and the reason it is a separate file: the model
 * half is already at the file-length ratchet and this half shares only the
 * vocabulary, the bounds check and the launch-selection rule (all in
 * ControlCommandsSessionShared.h). See ControlCommandsSession.cpp for why the
 * group exists at all.
 *
 * THESE COMMANDS ARE not_mutating and record no transaction. A launch writes no
 * project state: it queues a lock-free request into the audio thread's
 * scheduler (SessionScheduler::requestLaunch / requestStop / reset), exactly
 * like transport.play. Recording one would put a reversible:false step on top
 * of the undo stack every time an agent pressed play and would shadow the undo
 * of the real edit underneath it - the defect clip.select's row already
 * documents.
 *
 * WHAT A LAUNCH REPORTS is the SCHEDULED tick - the pure decision
 * (launchTickAt) for the position the song is at when the request is made, which
 * is what "at the next bar" means. When the audio thread reaches that line, the
 * engine records the start event; session.get_state reports it as
 * launch.start_line / start_line_starts, and how far past the line the audio
 * thread was in launch.start_observed. The three together are what makes the
 * synchronisation a measurement rather than a restatement of the request.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlCommandsSessionShared.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

/*! Queues one press and reports what it was scheduled for. The tick is the PURE
 *  decision for the song's position now; the audio thread re-decides from its
 *  own clock (it is the only thread that knows the sample-accurate position),
 *  so this is the line the launch was asked for, not a claim about when the
 *  audio thread saw it. */
QJsonObject launchOne(SessionScheduler& scheduler, const SessionClockContext& ctx,
	int track, int scene, const LaunchRequest& request)
{
	QJsonObject launched;
	launched.insert(QStringLiteral("track"), track);
	launched.insert(QStringLiteral("scene"), scene);
	launched.insert(QStringLiteral("mode"), modeName(request.mode));
	launched.insert(QStringLiteral("quantisation"), quantisationName(request.quantisation));
	launched.insert(QStringLiteral("scheduled_tick"),
		static_cast<int>(launchTickAt(request.quantisation, ctx)));
	scheduler.requestLaunch(track, scene, request.mode, request.quantisation);
	return launched;
}

//! The columns a scene launch can actually take over: the session grid may be
//! wider than the song's track list, and a column with no song track can never
//! take a track over (Song.cpp indexes the song's track list by column).
int launchableColumns(const SessionModel& model, Song& song)
{
	const int songTracks = static_cast<int>(song.tracks().size());
	return model.trackCount() < songTracks ? model.trackCount() : songTracks;
}

void registerLaunchSlot(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.launch_slot");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("launch_slot");
	cmd.description = QStringLiteral("Trigger one clip slot: queue a launch, quantised to the "
		"slot's own (or the supplied) boundary, and report the tick it was scheduled for. The "
		"engine records the start when the clock reaches that line. Not a project edit: no "
		"transaction is recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("mode"), enumProperty(modeNames())},
		{QStringLiteral("quantisation"), enumProperty(perClipQuantisationNames())},
	}, {QStringLiteral("track"), QStringLiteral("scene")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), integerProperty()},
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("mode"), stringProperty()},
		{QStringLiteral("quantisation"), stringProperty()},
		{QStringLiteral("scheduled_tick"), integerProperty()},
		{QStringLiteral("next_bar"), integerProperty()},
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
		if (model->slot(track, scene).isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("slot (track %1, scene %2) is empty: there is no clip to launch; "
					"session.set_slot defines one").arg(track).arg(scene));
		}
		if (track >= static_cast<int>(song->tracks().size()))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("column %1 has no song track: the engine takes a track over by its "
					"position in the song's track list, and this song has %2")
					.arg(track).arg(song->tracks().size()));
		}
		const LaunchRequest request = launchRequestOf(*model, model->slot(track, scene), args);
		const SessionClockContext ctx = clockOf(*song);
		QJsonObject result = launchOne(song->sessionScheduler(), ctx, track, scene, request);
		result.insert(QStringLiteral("clip"), clipSlotState(track, scene, model->slot(track, scene)));
		result.insert(QStringLiteral("next_bar"),
			static_cast<int>(launchTickAt(LaunchQuantisation::Bar, ctx)));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerLaunchScene(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.launch_scene");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("launch_scene");
	cmd.description = QStringLiteral("Trigger every non-empty clip in one scene row at once and "
		"report the tick each was scheduled for. `in_sync` is true when every launched clip "
		"resolved to the SAME grid line, which is what a scene launch means; `sync_tick` names "
		"that line. Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("mode"), enumProperty(modeNames())},
		{QStringLiteral("quantisation"), enumProperty(perClipQuantisationNames())},
	}, {QStringLiteral("scene")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("clips"), integerProperty()},
		{QStringLiteral("launched"), arrayProperty()},
		{QStringLiteral("in_sync"), booleanProperty()},
		{QStringLiteral("sync_tick"), integerProperty()},
		{QStringLiteral("skipped_columns"), integerProperty()},
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
		const int columns = launchableColumns(*model, *song);
		const SessionClockContext ctx = clockOf(*song);
		QJsonArray launched;
		int syncTick = 0;
		bool inSync = true;
		for (int track = 0; track < columns; ++track)
		{
			const ClipSlot& slot = model->slot(track, scene);
			if (slot.isEmpty()) { continue; }
			const QJsonObject entry = launchOne(song->sessionScheduler(), ctx, track, scene,
				launchRequestOf(*model, slot, args));
			const int at = entry.value(QStringLiteral("scheduled_tick")).toInt();
			if (launched.isEmpty()) { syncTick = at; }
			else if (at != syncTick) { inSync = false; }
			launched.append(entry);
		}
		QJsonObject result;
		result.insert(QStringLiteral("scene"), scene);
		result.insert(QStringLiteral("clips"), launched.size());
		result.insert(QStringLiteral("launched"), launched);
		result.insert(QStringLiteral("in_sync"), !launched.isEmpty() && inSync);
		result.insert(QStringLiteral("sync_tick"), launched.isEmpty() ? 0 : syncTick);
		result.insert(QStringLiteral("skipped_columns"), model->trackCount() - columns);
		result.insert(QStringLiteral("next_bar"),
			static_cast<int>(launchTickAt(LaunchQuantisation::Bar, ctx)));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerStopSlot(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.stop_slot");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("stop_slot");
	cmd.description = QStringLiteral("Stop one clip slot at its next quantisation boundary. A "
		"slot that never started stays idle, so this is safe to call unguarded. Not a project "
		"edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
	}, {QStringLiteral("track"), QStringLiteral("scene")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), integerProperty()},
		{QStringLiteral("scene"), integerProperty()},
		{QStringLiteral("stop_requested"), booleanProperty()},
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
		const LaunchRequest request = launchRequestOf(*model, model->slot(track, scene), args);
		song->sessionScheduler().requestStop(track, scene, request.mode, request.quantisation);
		QJsonObject result;
		result.insert(QStringLiteral("track"), track);
		result.insert(QStringLiteral("scene"), scene);
		result.insert(QStringLiteral("stop_requested"), true);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerStopAll(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.stop_all");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("stop_all");
	cmd.description = QStringLiteral("Drop every launched slot on the audio thread's next period, "
		"whichever scene or clip it came from. It is one atomic request, so it is safe while the "
		"transport runs. Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("reset_requested"), booleanProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		Engine::getSong()->sessionScheduler().reset();
		QJsonObject result;
		result.insert(QStringLiteral("reset_requested"), true);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionLaunchCommands(ControlRegistry& registry)
{
	registerLaunchSlot(registry);
	registerLaunchScene(registry);
	registerStopSlot(registry);
	registerStopAll(registry);
}

} // namespace lmms
