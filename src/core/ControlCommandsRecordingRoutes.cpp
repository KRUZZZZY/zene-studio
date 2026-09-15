/*
 * ControlCommandsRecordingRoutes.cpp - the RECORD ROUTE verbs of the `record.`
 *                                      group: what the multi-track recorder is
 *                                      doing, and how a route is armed and
 *                                      stopped (0.3.0, feature rows 14 and 64).
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/*
 * WHY THESE VERBS ARE THEIR OWN TRANSLATION UNIT. FEATURE-LIST-0.3.0.md row 14
 * records that "a real 2-track recorder is in the tree with tests ... but no
 * `record.*` group drives the recorder". These four ids are that gap closed, and
 * the input path's own verbs (record.input_get_state / record.input_set) are the
 * sibling unit ControlCommandsRecordingInput.cpp. The split is the fork's
 * file-length ratchet: the two halves together carry the group's schemas, prose
 * and refusals, and one file of them was over the limit the moment it was
 * written. The seam is real - one half answers "what may this instance capture",
 * the other "what is it capturing" - and the helpers both report through are in
 * ControlRecordingSupport.h, so a route cannot be described two ways.
 *
 * The route verbs drive include/MultiTrackRecorder.h: N routes (fixed at
 * construction, bound MultiTrackRecorder::MaxRoutes), each able to select any
 * input channel in [0, input_channel_capacity), each writing one 24-bit WAV with
 * a take journal beside it (include/RecordingJournal.h) so a crash mid-take is
 * offered to the next start by record.recovery_get_state.
 */

#include <algorithm>
#include <cstdint>

#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "TrackRecorder.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The live recorder, or nullptr when this instance has no audio engine at all
//! (a headless render, or an instance that came up without a device).
MultiTrackRecorder* liveRecorder()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? &engine->recorder() : nullptr;
}

//! The line every refusal about a missing engine shares.
ControlResult noEngine()
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this instance has no audio engine: the recorder is a member of it "
			"(include/AudioEngine.h), and a render-only or failed-start instance has none"));
}

//! The route \a route must be addressable: the recorder's own count, with the
//! bound named in the refusal so a caller knows what to expect.
bool routeAddressable(const MultiTrackRecorder& recorder, int route, ControlResult* error)
{
	if (route >= 0 && route < recorder.trackCount()) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("route %1 is outside [0, %2): the engine prepares exactly that many "
			"record routes (MultiTrackRecorder::MaxRoutes), fixed at start")
			.arg(route).arg(recorder.trackCount()));
	return false;
}

//! The input channel a route would select, given the caller's argument (or none).
int channelArg(const QJsonObject& args, int route, const TrackRecorder& target)
{
	const int channels = target.inputChannelCapacity();
	if (!args.contains(QStringLiteral("input_channel")))
	{
		// The prototype's mapping, kept as the default: route k starts on
		// channel k, or on the last selectable channel when the width is
		// narrower than the route count.
		return std::min(route, channels - 1);
	}
	return args.value(QStringLiteral("input_channel")).toInt();
}

//! The reply for a route that is armed (or was just stopped): the route's own
//! state, plus what the caller needs to find the take.
QJsonObject armedReply(const TrackRecorder& target, int route)
{
	QJsonObject result = routeJson(target, route);
	result.insert(QStringLiteral("sample_rate"), engineSampleRate());
	return result;
}

// --------------------------------------------------------------------------
// record.get_state
// --------------------------------------------------------------------------
ControlResult getState(const QJsonObject&)
{
	MultiTrackRecorder* recorder = liveRecorder();
	if (recorder == nullptr) { return noEngine(); }

	QJsonObject result = recorderJson(*recorder);
	result.insert(QStringLiteral("input"), inputPathJson());
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// record.arm_track
// --------------------------------------------------------------------------
ControlResult armTrack(const QJsonObject& args)
{
	MultiTrackRecorder* recorder = liveRecorder();
	if (recorder == nullptr) { return noEngine(); }

	const int route = routeArg(args);
	ControlResult error;
	if (!routeAddressable(*recorder, route, &error)) { return error; }

	TrackRecorder& target = recorder->track(route);
	if (target.isArmed())
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("route %1 is already armed and writing %2; disarm it first "
				"(record.disarm_track) - an armed route is one take, not a take per call")
				.arg(route).arg(QString::fromStdString(target.filePath())));
	}

	const QString file = takePathArg(args, route, &error);
	if (!error.ok) { return error; }

	const int channel = channelArg(args, route, target);
	if (!target.inputChannelSelectable(channel))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("input_channel %1 is outside [0, %2): this instance's capture path "
				"delivers %2 channel(s) (record.input_get_state reports whether a capture device "
				"is open and how many channels the configuration asks for, and record.input_set "
				"raises the count for the next start)").arg(channel)
				.arg(target.inputChannelCapacity()));
	}

	const int sampleRate = engineSampleRate();
	const QJsonObject before = routeJson(target, route);
	if (!recorder->armTrack(route, file.toStdString(), sampleRate, channel))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("route %1 could not be armed: the take file %2 could not be opened "
				"for writing, or the route is already armed").arg(route).arg(file));
	}

	QJsonObject result = armedReply(target, route);
	result.insert(QStringLiteral("armed_now"), true);
	result.insert(QStringLiteral("input_frames_staged_at_arm"),
		static_cast<qint64>(Engine::audioEngine() != nullptr
			? Engine::audioEngine()->inputFramesStaged() : 0u));
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("route"), route);
	result.insert(QStringLiteral("__transaction"),
		commandTransaction(before, QStringLiteral("record.disarm_track"), inverseArgs));
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// record.disarm_track
// --------------------------------------------------------------------------
ControlResult disarmTrack(const QJsonObject& args)
{
	MultiTrackRecorder* recorder = liveRecorder();
	if (recorder == nullptr) { return noEngine(); }

	const int route = routeArg(args);
	ControlResult error;
	if (!routeAddressable(*recorder, route, &error)) { return error; }

	TrackRecorder& target = recorder->track(route);
	const bool wasArmed = target.isArmed();
	target.disarm();

	QJsonObject result = armedReply(target, route);
	result.insert(QStringLiteral("armed_before"), wasArmed);
	result.insert(QStringLiteral("journal_retired"), wasArmed);
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// record.disarm_all
// --------------------------------------------------------------------------
ControlResult disarmAll(const QJsonObject&)
{
	MultiTrackRecorder* recorder = liveRecorder();
	if (recorder == nullptr) { return noEngine(); }

	int armedBefore = 0;
	for (int i = 0; i < recorder->trackCount(); ++i)
	{
		if (recorder->track(i).isArmed()) { ++armedBefore; }
	}
	recorder->disarmAll();

	QJsonObject result = recorderJson(*recorder);
	result.insert(QStringLiteral("armed_before"), armedBefore);
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// schemas
// --------------------------------------------------------------------------
QJsonObject routeSchema()
{
	return objectSchema({
		{QStringLiteral("route"), integerProperty(0, 1024)},
	});
}

QJsonObject routeResultSchema()
{
	return objectSchema({
		{QStringLiteral("route"), integerProperty()},
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("input_channel"), integerProperty()},
		{QStringLiteral("input_channel_capacity"), integerProperty()},
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("frames_pushed"), integerProperty()},
		{QStringLiteral("frames_recorded"), integerProperty()},
		{QStringLiteral("frames_journalled"), integerProperty()},
		{QStringLiteral("overflow_frames"), integerProperty()},
		{QStringLiteral("write_errors"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
	});
}

QJsonObject recorderStateSchema()
{
	return objectSchema({
		{QStringLiteral("route_count"), integerProperty()},
		{QStringLiteral("input_channel_capacity"), integerProperty()},
		{QStringLiteral("routes"), arrayProperty()},
		{QStringLiteral("total_overflow_frames"), integerProperty()},
		{QStringLiteral("armed_before"), integerProperty()},
	});
}

// --------------------------------------------------------------------------
// registration
// --------------------------------------------------------------------------
void registerGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.get_state");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Every record route the engine prepared, and the input path "
		"that feeds them. A route is one capture stream: an arm flag, the interleaved input "
		"channel it reads, the take file it writes, the take JOURNAL beside it, and the frame "
		"counters (pushed / recorded / journalled / overflow / write errors). The engine prepares "
		"%1 routes, and each may select ANY of the configured input channels - that pair of numbers "
		"(route_count and input_channel_capacity) is the 'arbitrary input count / multiple "
		"simultaneous inputs' feature row 64 asks for. `input` is record.input_get_state's own "
		"report, including `input_frames_staged` and `wide_frames`: both are 0 under a backend with "
		"no capture path, which is what 'a record route takes no inputs' measured. Read-only.")
		.arg(MultiTrackRecorder::MaxRoutes);
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("route_count"), integerProperty()},
		{QStringLiteral("input_channel_capacity"), integerProperty()},
		{QStringLiteral("routes"), arrayProperty()},
		{QStringLiteral("total_overflow_frames"), integerProperty()},
		{QStringLiteral("input"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return getState(args); };
	registry.registerCommand(cmd);
}

void registerArmTrack(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.arm_track");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("arm_track");
	cmd.description = QStringLiteral("Arm one record route: start writing the interleaved input "
		"channel it selects into a 24-bit WAV, and journal the take beside it (the take journal is "
		"what makes a crashed capture recoverable - record.journal_begin's side file, written here "
		"by the recorder's own arm()). 'route' is the route index record.get_state reports; "
		"'input_channel' defaults to the route's own index and must be inside "
		"[0, input_channel_capacity) - the FRAMES the route records come from the engine's input "
		"path, so with no capture device open and no frames staged the take is silent however the "
		"route is armed (record.input_get_state says which of those is true). 'file' must be "
		"absolute and defaults to zene-take-route<N>.wav beside this instance's recovery file. "
		"Reversible: the recorded inverse is record.disarm_track, which stops the capture and "
		"retires the journal; the take file itself is left on disk, like any recording you keep.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("route"), integerProperty(0, 1024)},
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("input_channel"), integerProperty(0, AudioInputPath::MaxChannels - 1)},
		{QStringLiteral("sample_rate"), integerProperty()},
	}, {QStringLiteral("route")});
	cmd.resultSchema = routeResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return armTrack(args); };
	registry.registerCommand(cmd);
}

void registerDisarmTrack(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.disarm_track");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("disarm_track");
	cmd.description = QStringLiteral("Stop one record route: the disk-writer is drained and "
		"joined, the WAV is closed, and the take journal is RETIRED - a clean stop leaves no "
		"journal, which is what makes 'there is a journal' and 'the capture died' the same fact "
		"(include/RecordingJournal.h). The take file stays on disk. This is record.arm_track's "
		"recorded inverse, so control.undo after an arm dispatches it. Not reversible itself: "
		"re-arming writes a NEW take rather than restoring this one, and there is nothing the "
		"engine could hand back.");
	cmd.argsSchema = routeSchema();
	cmd.resultSchema = routeResultSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return disarmTrack(args); };
	registry.registerCommand(cmd);
}

void registerDisarmAll(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.disarm_all");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("disarm_all");
	cmd.description = QStringLiteral("Stop every armed record route in one call and report the "
		"whole recorder afterwards. Each route's take is flushed and closed and its journal "
		"retired, exactly as record.disarm_track does for one. Not reversible: every route's take "
		"is a file that stays on disk, and re-arming starts a new one.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = recorderStateSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return disarmAll(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordingRouteCommands(ControlRegistry& registry)
{
	registerGetState(registry);
	registerArmTrack(registry);
	registerDisarmTrack(registry);
	registerDisarmAll(registry);
}

} // namespace lmms
