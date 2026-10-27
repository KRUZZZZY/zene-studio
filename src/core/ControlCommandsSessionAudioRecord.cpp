/*
 * ControlCommandsSessionAudioRecord.cpp - session.record_slot / _status / _land: record AUDIO into a
 *                                         launcher cell (0.5.0 L02, WF-055)
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

/*! The MIDI half of WF-055 is session.capture_to_slot (what was played goes into a new pattern). This is the
 *  AUDIO half, and it records FORWARD, the way a launcher records: session.record_slot arms an empty cell on
 *  an audio (sample) track's column and launches it, so the take starts on the launch quantisation's grid
 *  line; it ends on the cell's quantised stop (session.stop_slot) or after 'bars'; session.record_slot_land
 *  writes the take to a 24-bit WAV and makes it the cell's audio clip - one undo step. Arming and the
 *  recording itself are not project edits (a launch is not one); landing is. The engine half is
 *  include/SessionAudioRecorder.h; docs/SESSION-AUDIO-RECORD.md is the design record. */

#include <memory>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RetroAudioCapture.h"
#include "SampleBuffer.h"
#include "SampleTrack.h"
#include "SessionAudioRecorder.h"
#include "SessionScheduler.h"
#include "Song.h"
#include "TakeRecorder.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

QString stateName(SessionAudioRecorder::State state)
{
	switch (state)
	{
		case SessionAudioRecorder::State::Armed: return QStringLiteral("armed");
		case SessionAudioRecorder::State::Recording: return QStringLiteral("recording");
		case SessionAudioRecorder::State::Finishing: return QStringLiteral("finishing");
		case SessionAudioRecorder::State::Done: return QStringLiteral("done");
		case SessionAudioRecorder::State::Idle: break;
	}
	return QStringLiteral("idle");
}

//! @a body run while no period renders: what cancel() needs (include/SessionAudioRecorder.h).
template <typename Body>
void withEngineHeld(Body body)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }
	body();
	if (engine != nullptr) { engine->doneChangeInModel(); }
}

QJsonObject recorderState(const SessionAudioRecorder& recorder)
{
	const double rate = recorder.sampleRate() > 0 ? static_cast<double>(recorder.sampleRate()) : 1.0;
	return {{QStringLiteral("state"), stateName(recorder.state())}, {QStringLiteral("track"), recorder.track()},
		{QStringLiteral("scene"), recorder.scene()}, {QStringLiteral("start_tick"), static_cast<double>(recorder.startTick())},
		{QStringLiteral("frames"), static_cast<double>(recorder.framesRecorded())},
		{QStringLiteral("target_frames"), static_cast<double>(recorder.targetFrames())},
		{QStringLiteral("seconds"), static_cast<double>(recorder.framesRecorded()) / rate},
		{QStringLiteral("dropped"), static_cast<double>(recorder.framesDropped())},
		{QStringLiteral("sample_rate"), static_cast<int>(recorder.sampleRate())},
		{QStringLiteral("latency_frames"), static_cast<int>(recorder.latencyFrames())}};
}

//! Why cell (@a track, @a scene) cannot take a recording, or an empty string.
QString recordableProblem(const SessionModel& model, Song& song, int track, int scene)
{
	if (track >= static_cast<int>(song.tracks().size()))
	{
		return QStringLiteral("column %1 has no song track: a cell records through the song track at its column's position, "
			"and this song has %2").arg(track).arg(song.tracks().size());
	}
	if (dynamic_cast<SampleTrack*>(song.tracks()[static_cast<std::size_t>(track)]) == nullptr)
	{
		return QStringLiteral("column %1's track is not an audio (sample) track: audio records into a sample track's "
			"cells (session.capture_to_slot records what an instrument played)").arg(track);
	}
	if (!model.slot(track, scene).isEmpty())
	{
		return QStringLiteral("cell (track %1, scene %2) already holds a clip: record into an empty cell, or clear this one "
			"first (session.clear_slot)").arg(track).arg(scene);
	}
	return QString();
}

//! Drops the take in hand, whichever cell it is for, and stops that cell if it was launched for it.
ControlResult cancelRecording(Song& song)
{
	SessionAudioRecorder& recorder = song.sessionScheduler().audioRecorder();
	const int track = recorder.track();
	const int scene = recorder.scene();
	const SessionAudioRecorder::State before = recorder.state();
	withEngineHeld([&recorder] { recorder.cancel(); });
	if (before != SessionAudioRecorder::State::Idle && before != SessionAudioRecorder::State::Done)
	{
		song.sessionScheduler().requestStop(track, scene, LaunchMode::Trigger, LaunchQuantisation::None);
	}
	QJsonObject result = recorderState(recorder);
	result.insert(QStringLiteral("cancelled"), before != SessionAudioRecorder::State::Idle);
	return ControlResult::success(result);
}

ControlResult armAndLaunch(SessionModel& model, Song& song, int track, int scene, const QJsonObject& args)
{
	SessionScheduler& scheduler = song.sessionScheduler();
	SessionAudioRecorder& recorder = scheduler.audioRecorder();
	if (recorder.state() != SessionAudioRecorder::State::Idle)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("a take is already in hand (%1, cell track %2 scene "
			"%3): land it (session.record_slot_land) or cancel it (session.record_slot cancel:true)")
			.arg(stateName(recorder.state())).arg(recorder.track()).arg(recorder.scene()));
	}
	AudioEngine* engine = Engine::audioEngine();
	const tick_t length = static_cast<tick_t>(args.value(QStringLiteral("bars")).toInt(0)) * song.ticksPerBar();
	recorder.arm(track, scene, length, engine->inputSampleRate(), engine->recordingLatencyFrames());
	const LaunchRequest request = launchRequestOf(model, model.slot(track, scene), args);
	const SessionClockContext ctx = clockOf(song);
	publishSessionSlotContent(scheduler, model);  // what launch_slot does first: a cleared cell renders nothing stale
	if (!scheduler.requestLaunch(track, scene, LaunchMode::Trigger, request.quantisation))
	{
		withEngineHeld([&recorder] { recorder.cancel(); });
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("the launch queue is full; nothing was armed"));
	}
	QJsonObject result = recorderState(recorder);
	result.insert(QStringLiteral("quantisation"), quantisationName(request.quantisation));
	result.insert(QStringLiteral("scheduled_tick"), static_cast<int>(launchTickAt(request.quantisation, ctx)));
	result.insert(QStringLiteral("length_ticks"), static_cast<int>(length));
	return ControlResult::success(result);
}

ControlResult recordSlot(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	Song& song = *Engine::getSong();
	const int track = args.value(QStringLiteral("track")).toInt();
	const int scene = args.value(QStringLiteral("scene")).toInt();
	if (!gridContains(*model, track, scene, &error)) { return error; }
	if (args.value(QStringLiteral("cancel")).toBool()) { return cancelRecording(song); }
	const QString problem = recordableProblem(*model, song, track, scene);
	if (!problem.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, problem); }
	return armAndLaunch(*model, song, track, scene, args);
}

//! The take's path: @a file (absolute) or a new take in the user's Recordings folder; empty with @a error.
QString takePathOf(const QJsonObject& args, ControlResult* error)
{
	const QString file = args.value(QStringLiteral("file")).toString();
	if (!file.isEmpty() && !QFileInfo(file).isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'file' must be absolute: the cell names the take by this path in the project"));
		return QString();
	}
	const QString path = file.isEmpty() ? TakeRecorder::defaultTakePath() : file;
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the take's folder %1 cannot be made").arg(QFileInfo(path).absolutePath()));
		return QString();
	}
	return path;
}

//! What one landing put into the cell, and what its undo puts back.
struct Landed
{
	int track = 0;
	int scene = 0;
	QString path;
	QString name;
	QString session;  //!< the <session> block before
};

void applyLanded(const Landed& landed)
{
	const SessionModel before = Engine::getSong()->sessionModel();
	ClipSlot& slot = Engine::getSong()->sessionModel().slot(landed.track, landed.scene);
	slot.setAudioReference(landed.path);
	slot.setName(landed.name);
	syncLauncherFrom(before);  // Q050-489: the launcher hears the landed take (and its undo stops it)
}

ControlResult writeAndLand(SessionModel& model, SessionAudioRecorder& recorder, const QString& path, const QString& name)
{
	auto landed = std::make_shared<Landed>(Landed{recorder.track(), recorder.scene(), path, name, sessionBlockXml(model)});
	const int rate = static_cast<int>(recorder.sampleRate());
	const tick_t start = recorder.startTick();
	const std::shared_ptr<const SampleBuffer> take = recorder.takeFinished();
	if (take == nullptr || take->size() == 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the take holds no audio (the cell stopped "
			"on the line it started on): nothing was landed"));
	}
	const std::vector<SampleFrame> frames(take->data(), take->data() + take->size());
	std::uint64_t written = 0;
	QString why;
	if (!writeRetroAudioTake(path, frames, rate, &written, &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the take could not be written: %1").arg(why));
	}
	applyLanded(*landed);
	addUndoStep([landed] { restoreSessionBlockAndSync(*Engine::getSong(), landed->session); },
		[landed] { applyLanded(*landed); });
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("slot"), clipSlotState(landed->track, landed->scene, model.slot(landed->track, landed->scene))},
		{QStringLiteral("file"), path}, {QStringLiteral("frames"), static_cast<double>(written)},
		{QStringLiteral("seconds"), static_cast<double>(written) / std::max(1, rate)},
		{QStringLiteral("sample_rate"), rate}, {QStringLiteral("start_tick"), static_cast<double>(start)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), landed->track},
		{QStringLiteral("scene"), landed->scene}}, QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: "
		"the recorded undo step restores the <session> block (the take's WAV stays on disk); its redo points the cell at it again")));
	return ControlResult::success(result);
}

ControlResult landSlot(const QJsonObject& args)
{
	ControlResult error;
	SessionModel* model = sessionModelOrNull(&error);
	if (model == nullptr) { return error; }
	SessionAudioRecorder& recorder = Engine::getSong()->sessionScheduler().audioRecorder();
	if (recorder.state() != SessionAudioRecorder::State::Done)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("there is no finished take to land (the "
			"recorder is %1): session.record_slot records one, and it is finished once its cell has stopped")
			.arg(stateName(recorder.state())));
	}
	if (!gridContains(*model, recorder.track(), recorder.scene(), &error)) { return error; }
	if (!model->slot(recorder.track(), recorder.scene()).isEmpty())
	{
		// Checked before writeAndLand: takeFinished() gives the take away, so a refusal after it would lose the take.
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cell (track %1, scene %2) holds a clip now: it was "
			"filled while the take recorded, and landing would overwrite it. Nothing was landed and the take stays in hand: clear "
			"the cell (session.clear_slot) and land again, or drop the take (session.record_slot 'cancel': true)")
			.arg(recorder.track()).arg(recorder.scene()));
	}
	const QString path = takePathOf(args, &error);
	if (path.isEmpty()) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString(QFileInfo(path).completeBaseName());
	return writeAndLand(*model, recorder, path, name);
}

void registerRecordSlot(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.record_slot");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("record_slot");
	cmd.description = QStringLiteral("Record audio into an empty launcher cell ('track', 'scene') of an audio (sample) track's "
		"column: the cell is armed and launched, so the take starts on the launch quantisation's grid line ('quantisation', "
		"else the cell's own, else the session's). It takes the engine's stereo input, shifted by the recording round trip, "
		"until the cell stops - session.stop_slot's quantised stop, another cell on the column, session.stop_all - or for "
		"'bars' bars. session.record_slot_status follows it; session.record_slot_land makes it the cell's clip. "
		"'cancel': true drops the take in hand (whichever cell it is for) and stops its cell. Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), integerProperty(0, 255)}, {QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("bars"), integerProperty(1, 64)}, {QStringLiteral("quantisation"), enumProperty(perClipQuantisationNames())},
		{QStringLiteral("cancel"), booleanProperty()}}, {QStringLiteral("track"), QStringLiteral("scene")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = recordSlot;
	registry.registerCommand(cmd);
}

void registerRecordSlotStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.record_slot_status");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("record_slot_status");
	cmd.description = QStringLiteral("Read the launcher cell's audio recording back: 'state' (idle, armed - waiting for its "
		"launch, recording, finishing - stopped and running on to its length, done - ready for session.record_slot_land), "
		"the cell, the grid line it started on, the frames taken and the length it will have, the frames a starved writer "
		"dropped, the sample rate and the round trip it was shifted by. Read-only.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		if (sessionModelOrNull(&error) == nullptr) { return error; }
		return ControlResult::success(recorderState(Engine::getSong()->sessionScheduler().audioRecorder()));
	};
	registry.registerCommand(cmd);
}

void registerRecordSlotLand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.record_slot_land");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("record_slot_land");
	cmd.description = QStringLiteral("Land a finished launcher-cell take: write it as a 24-bit WAV ('file', absolute; a new "
		"take in the user's Recordings folder when left out) and make it the cell's audio clip, named 'name' (the file's "
		"name when left out). Refused until session.record_slot_status says done. One undo step: it puts the cell back; "
		"the WAV stays on disk.");
	cmd.argsSchema = objectSchema({{QStringLiteral("file"), stringProperty()}, {QStringLiteral("name"), stringProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = landSlot;
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionAudioRecordCommands(ControlRegistry& registry)
{
	registerRecordSlot(registry);
	registerRecordSlotStatus(registry);
	registerRecordSlotLand(registry);
}

} // namespace lmms
