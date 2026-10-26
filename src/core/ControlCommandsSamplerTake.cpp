/*
 * ControlCommandsSamplerTake.cpp - record an input take straight into a sampler (0.5.0 L10, Q050-156)
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

/*! Q050-156: the agent surface of a bounded input take recorded directly into an
 *  AudioFileProcessor or SlicerT.
 *
 *  - record.sampler_capture arms a take for one instrument track and one device. The take is
 *    bounded by 'seconds' (default 2, at most SamplerTakeRecorder::MaxSeconds); the capture
 *    starts at the next rendered period and runs until that many frames have been taken.
 *  - record.sampler_capture_stop ends it early; the frames taken so far are the take.
 *  - record.sampler_capture_status reads the state, the target and the frames taken.
 *  - record.sampler_capture_land writes the take as a 24-bit stereo WAV ('file' may be left
 *    out for a new take in the user's Recordings folder) and installs it in the device through
 *    the instrument's own loadFile() - so AudioFileProcessor shows and plays the waveform and
 *    SlicerT recalculates its slices. One undo step: the track's previous sample and settings
 *    come back, while the WAV stays on disk (the session.record_slot_land precedent).
 *
 *  Capture, stop and status change no project state (mutating: false). Landing is a real edit
 *  (true_inverse; ProjectJournal Track checkpoint). */

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "RetroAudioCapture.h"
#include "SamplerTakeRecorder.h"
#include "SampleBuffer.h"
#include "Song.h"
#include "TakeRecorder.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr double DefaultTakeSeconds = 2.0;

QString stateName(SamplerTakeRecorder::State state)
{
	switch (state)
	{
		case SamplerTakeRecorder::State::Recording: return QStringLiteral("recording");
		case SamplerTakeRecorder::State::Done: return QStringLiteral("done");
		case SamplerTakeRecorder::State::Idle: break;
	}
	return QStringLiteral("idle");
}

//! @a body run while no period renders: what arm(), stop() and cancel() need
//! (include/SamplerTakeRecorder.h).
template <typename Body>
void withEngineHeld(Body body)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }
	body();
	if (engine != nullptr) { engine->doneChangeInModel(); }
}

QJsonObject recorderState(const SamplerTakeRecorder& recorder)
{
	const double rate = recorder.sampleRate() > 0 ? static_cast<double>(recorder.sampleRate()) : 1.0;
	const auto frames = recorder.framesRecorded();
	const auto target = recorder.targetFrames();
	return {{QStringLiteral("state"), stateName(recorder.state())},
		{QStringLiteral("track"), recorder.trackId()},
		{QStringLiteral("device"), recorder.device()},
		{QStringLiteral("frames"), static_cast<double>(frames)},
		{QStringLiteral("target_frames"), static_cast<double>(target)},
		{QStringLiteral("seconds"), static_cast<double>(frames) / rate},
		{QStringLiteral("target_seconds"), static_cast<double>(target) / rate},
		{QStringLiteral("dropped"), static_cast<double>(recorder.framesDropped())},
		{QStringLiteral("sample_rate"), static_cast<int>(recorder.sampleRate())},
		{QStringLiteral("latency_frames"), static_cast<int>(recorder.latencyFrames())},
		{QStringLiteral("max_seconds"), SamplerTakeRecorder::MaxSeconds}};
}

//! Whether @a instrument is a device this unit can load a take into.
bool supportedDevice(const Instrument* instrument)
{
	if (instrument == nullptr) { return false; }
	const QString name = instrument->nodeName().toLower();
	return name == QLatin1String("audiofileprocessor") || name == QLatin1String("slicert");
}

//! The instrument track @a args names and its sampler device, or nullptr with @a error set.
InstrumentTrack* samplerTarget(const QJsonObject& args, Instrument** instrument, ControlResult* error)
{
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track);
	if (instrumentTrack == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'track' must be an instrument track"));
		return nullptr;
	}
	*instrument = instrumentTrack->instrument();
	if (*instrument == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("track %1 carries no instrument: load AudioFileProcessor or SlicerT first")
				.arg(trackIdOf(track)));
		return nullptr;
	}
	if (!supportedDevice(*instrument))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("track %1 holds %2: a take can be recorded into AudioFileProcessor or SlicerT only")
				.arg(trackIdOf(track), (*instrument)->nodeName()));
		return nullptr;
	}
	return instrumentTrack;
}

ControlResult capture(const QJsonObject& args)
{
	ControlResult error;
	Instrument* instrument = nullptr;
	if (samplerTarget(args, &instrument, &error) == nullptr) { return error; }

	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine is not available: there is no input to record"));
	}
	SamplerTakeRecorder& recorder = engine->samplerTakeRecorder();

	if (args.value(QStringLiteral("cancel")).toBool())
	{
		const bool cancelled = recorder.state() != SamplerTakeRecorder::State::Idle;
		withEngineHeld([&recorder] { recorder.cancel(); });
		QJsonObject result = recorderState(recorder);
		result.insert(QStringLiteral("cancelled"), cancelled);
		return ControlResult::success(result);
	}

	if (recorder.state() != SamplerTakeRecorder::State::Idle)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("a take is already in hand (track %1, %2, %3): land it "
				"(record.sampler_capture_land) or cancel it (record.sampler_capture cancel:true)")
				.arg(recorder.trackId(), recorder.device(), stateName(recorder.state())));
	}

	const double seconds = args.value(QStringLiteral("seconds")).toDouble(DefaultTakeSeconds);
	if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > SamplerTakeRecorder::MaxSeconds)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'seconds' must be more than 0 and at most %1")
				.arg(SamplerTakeRecorder::MaxSeconds));
	}
	const sample_rate_t rate = engine->inputSampleRate();
	if (rate <= 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine reports no input sample rate: there is nothing to record"));
	}
	const auto target = static_cast<std::uint64_t>(std::llround(seconds * static_cast<double>(rate)));
	const QString id = trackIdOf(instrument->instrumentTrack());
	const QString device = instrument->nodeName();
	bool armed = false;
	withEngineHeld([&] {
		armed = recorder.arm(id, device, rate, engine->recordingLatencyFrames(), std::max<std::uint64_t>(1, target));
	});
	if (!armed)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the take could not be armed: another take is in hand"));
	}
	QJsonObject result = recorderState(recorder);
	result.insert(QStringLiteral("seconds_requested"), seconds);
	return ControlResult::success(result);
}

ControlResult stopCapture(const QJsonObject&)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the engine is not available"));
	}
	SamplerTakeRecorder& recorder = engine->samplerTakeRecorder();
	if (recorder.state() == SamplerTakeRecorder::State::Idle)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("there is no take to stop: record.sampler_capture starts one"));
	}
	const bool stopped = recorder.state() == SamplerTakeRecorder::State::Recording;
	withEngineHeld([&recorder] { recorder.stop(); });
	QJsonObject result = recorderState(recorder);
	result.insert(QStringLiteral("stopped"), stopped);
	return ControlResult::success(result);
}

ControlResult captureStatus(const QJsonObject&)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the engine is not available"));
	}
	return ControlResult::success(recorderState(engine->samplerTakeRecorder()));
}

//! The take's path: @a file (absolute) or a new take in the user's Recordings folder.
QString takePathOf(const QJsonObject& args, ControlResult* error)
{
	const QString file = args.value(QStringLiteral("file")).toString();
	if (!file.isEmpty() && !QFileInfo(file).isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'file' must be absolute: it is the sample the project points at"));
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

//! The track and device a finished take was recorded for, or nullptr/false with @a error set.
bool landTarget(const SamplerTakeRecorder& recorder, Track** track, Instrument** instrument, ControlResult* error)
{
	*track = resolveTrack(recorder.trackId(), error);
	if (*track == nullptr) { return false; }
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(*track);
	*instrument = instrumentTrack != nullptr ? instrumentTrack->instrument() : nullptr;
	if (*instrument == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("track %1 no longer carries an instrument: the take was not landed")
				.arg(recorder.trackId()));
		return false;
	}
	if ((*instrument)->nodeName() != recorder.device())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("track %1 now holds %2, not %3: the take was not landed")
				.arg(recorder.trackId(), (*instrument)->nodeName(), recorder.device()));
		return false;
	}
	return true;
}

ControlResult land(const QJsonObject& args)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the engine is not available"));
	}
	SamplerTakeRecorder& recorder = engine->samplerTakeRecorder();
	if (recorder.state() != SamplerTakeRecorder::State::Done)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("there is no finished take to land (the recorder is %1): record.sampler_capture "
				"records one, and it is finished once its length is reached or it is stopped")
				.arg(stateName(recorder.state())));
	}

	ControlResult error;
	Track* track = nullptr;
	Instrument* instrument = nullptr;
	if (!landTarget(recorder, &track, &instrument, &error)) { return error; }
	const QString path = takePathOf(args, &error);
	if (path.isEmpty()) { return error; }
	if (QFileInfo::exists(path))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the take path %1 already exists: choose a new file; nothing was overwritten and the take stays in hand")
				.arg(path));
	}
	const std::shared_ptr<const SampleBuffer> take = recorder.takeFinished();
	if (take == nullptr || take->size() == 0)
	{
		recorder.releaseFinished();
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the take holds no audio: nothing was landed"));
	}
	const int rate = static_cast<int>(recorder.sampleRate());
	const std::vector<SampleFrame> frames(take->data(), take->data() + take->size());
	std::uint64_t written = 0;
	QString why;
	if (!writeRetroAudioTake(path, frames, rate, &written, &why))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the take could not be written: %1").arg(why));
	}

	const QString id = trackIdOf(track);
	const QString device = instrument->nodeName();
	// One undo step (the track's checkpoint) puts the device's previous sample and settings back;
	// the WAV the take wrote stays on disk, as session.record_slot_land's does.
	addUndoStep(QVector<JournallingObject*>{track});
	instrument->loadFile(path);
	recorder.releaseFinished();
	Engine::getSong()->setModified();

	QJsonObject result{{QStringLiteral("track"), id}, {QStringLiteral("device"), device},
		{QStringLiteral("file"), path}, {QStringLiteral("frames"), static_cast<double>(written)},
		{QStringLiteral("seconds"), static_cast<double>(written) / std::max(1, rate)},
		{QStringLiteral("sample_rate"), rate}, {QStringLiteral("loaded"), true}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("track"), id}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("ProjectJournal (Track checkpoint): undo restores "
		"the device's previous sample and settings; the take's WAV stays on disk")));
	return ControlResult::success(result);
}

void registerCapture(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.sampler_capture");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("sampler_capture");
	cmd.description = QStringLiteral("Record the engine's configured stereo input straight into an "
		"AudioFileProcessor or SlicerT on instrument track 'track'. The take is bounded: it ends when "
		"'seconds' (default 2, at most 30) of input have been taken, or when record.sampler_capture_stop "
		"ends it early. The first frames (the recording round trip, 'latency_frames' in the reply) are "
		"skipped so the take lines up with what was heard. record.sampler_capture_status follows it; "
		"record.sampler_capture_land writes it as a WAV and loads it into the device. 'cancel': true "
		"drops the take in hand. Not a project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("seconds"), numberProperty()}, {QStringLiteral("cancel"), booleanProperty()}},
		{QStringLiteral("track")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = capture;
	registry.registerCommand(cmd);
}

void registerStop(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.sampler_capture_stop");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("sampler_capture_stop");
	cmd.description = QStringLiteral("End the sampler take in hand early: the input frames taken so far are "
		"the take, and record.sampler_capture_land can install it. Idempotent once the take is done. Not a "
		"project edit: no transaction is recorded.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = stopCapture;
	registry.registerCommand(cmd);
}

void registerStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.sampler_capture_status");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("sampler_capture_status");
	cmd.description = QStringLiteral("Read the sampler take back: 'state' (idle, recording, done - ready for "
		"record.sampler_capture_land), the target track and device, the frames taken and the length it was "
		"bounded to, their lengths in seconds, the frames a starved writer dropped, the sample rate and the "
		"round trip it was shifted by. Read-only.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = captureStatus;
	registry.registerCommand(cmd);
}

void registerLand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.sampler_capture_land");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("sampler_capture_land");
	cmd.description = QStringLiteral("Land a finished sampler take as a 24-bit stereo WAV. Optional 'file' must be "
		"an absolute path that does not exist; without it, a new take is saved in the user's Recordings folder. "
		"Load it into the device it was recorded for through the instrument's own loader - AudioFileProcessor shows and plays it, "
		"SlicerT recalculates its slices. Refused until record.sampler_capture_status says done. One undo "
		"step: it restores the device's previous sample and settings; the WAV stays on disk.");
	cmd.argsSchema = objectSchema({{QStringLiteral("file"), stringProperty()}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = land;
	registry.registerCommand(cmd);
}

} // namespace

void registerSamplerTakeCommands(ControlRegistry& registry)
{
	registerCapture(registry);
	registerStop(registry);
	registerStatus(registry);
	registerLand(registry);
}

} // namespace lmms
