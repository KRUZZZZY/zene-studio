/*
 * ControlCommandsRecordingRetro.cpp - the retrospective AUDIO capture verbs of
 *                                      the `record.` group (0.3.0, feature row
 *                                      16 "Retrospective audio capture").
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

/*
 * THE MODEL IS include/RetroMidiCapture.h, deliberately and to the sentence: off
 * by default, one bounded recent-window ring, an arm flag that is MODE state and
 * therefore no transaction, a realtime-safe producer entry point, and a
 * consumer-side snapshot that never blocks the producer. docs/MIDI-RETRO-CAPTURE.md
 * is the MIDI half's design record; docs/RETRO-AUDIO-CAPTURE.md is this half's,
 * and the row that asks for it is FEATURE-LIST-0.3.0.md row 16.
 *
 * WHAT IS DIFFERENT FROM THE MIDI HALF, and it is one thing: the source of the
 * frames is the ENGINE'S OWN INPUT PATH (AudioEngine::inputBuffer() /
 * inputWideBuffer(), feature row 64) rather than a device callback, so this
 * capture needs no extra device, no extra thread, and no MIDI client - it costs
 * one relaxed atomic load per rendered period while disarmed. The verbs are the
 * same three shapes the MIDI group registers: arm, status, and the one writer
 * that turns the window into something you can keep (there, a clip; here, a WAV
 * file).
 *
 * WHAT 0.3.0 DOES NOT DO WITH IT, said here rather than discovered: the take is
 * written to disk and is NOT inserted into the session as a clip. That is the
 * same absence record.recovery_restore states for a recovered take, for the same
 * reason - turning a file into a SampleClip is the product's own load path and
 * it is not this group's job to invent a second one. docs/KNOWN-LIMITATIONS.md
 * carries the line.
 */

#include <algorithm>
#include <cstdint>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RetroAudioCapture.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The live capture, or nullptr when this instance has no audio engine at all.
RetroAudioCapture* liveCapture()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? &engine->retroCapture() : nullptr;
}

//! The line every refusal about a missing engine shares.
ControlResult noEngine()
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this instance has no audio engine: the retrospective audio capture is a "
			"member of it (include/AudioEngine.h) and dies with it"));
}

int engineSampleRate()
{
	const AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? static_cast<int>(engine->baseSampleRate()) : 44100;
}

//! Seconds \a frames last at \a sampleRate, as a number a caller can compare.
double secondsFor(std::uint64_t frames, int sampleRate)
{
	return sampleRate > 0 ? static_cast<double>(frames) / static_cast<double>(sampleRate) : 0.0;
}

QJsonObject captureStatusJson(const RetroAudioCapture& capture)
{
	const int rate = engineSampleRate();
	RetroAudioRing& ring = const_cast<RetroAudioRing&>(capture.ring());
	const std::uint64_t retained = static_cast<std::uint64_t>(ring.bufferedCount());

	QJsonObject result;
	result.insert(QStringLiteral("armed"), capture.isArmed());
	result.insert(QStringLiteral("capacity_frames"), static_cast<qint64>(ring.capacity()));
	result.insert(QStringLiteral("capacity_seconds"), secondsFor(ring.capacity(), rate));
	result.insert(QStringLiteral("retained_frames"), static_cast<qint64>(retained));
	result.insert(QStringLiteral("window_seconds"), secondsFor(retained, rate));
	// The bound, as a number: frames a later period overwrote - i.e. how much of
	// the past has already fallen out of the window.
	result.insert(QStringLiteral("overwritten_frames"),
		static_cast<qint64>(ring.overwrittenCount()));
	result.insert(QStringLiteral("paused_drop_frames"),
		static_cast<qint64>(ring.pausedDropCount()));
	result.insert(QStringLiteral("refused_snapshots"),
		static_cast<qint64>(ring.refusedSnapshots()));
	result.insert(QStringLiteral("sample_rate"), rate);
	return result;
}

// --------------------------------------------------------------------------
// record.retro_capture_arm
// --------------------------------------------------------------------------
ControlResult retroArm(const QJsonObject& args)
{
	RetroAudioCapture* capture = liveCapture();
	if (capture == nullptr) { return noEngine(); }

	const bool wasArmed = capture->isArmed();
	// No argument means "arm": the deterministic reading of a command named
	// retro_capture_ARM (the midi.retro_capture_arm precedent, verb for verb).
	const bool wanted = args.contains(QStringLiteral("armed"))
		? args.value(QStringLiteral("armed")).toBool() : true;
	capture->arm(wanted);

	QJsonObject result = captureStatusJson(*capture);
	result.insert(QStringLiteral("armed_before"), wasArmed);
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// record.retro_capture_status
// --------------------------------------------------------------------------
ControlResult retroStatus(const QJsonObject&)
{
	RetroAudioCapture* capture = liveCapture();
	if (capture == nullptr) { return noEngine(); }
	return ControlResult::success(captureStatusJson(*capture));
}

// --------------------------------------------------------------------------
// record.retro_capture_to_take
// --------------------------------------------------------------------------
ControlResult retroToTake(const QJsonObject& args)
{
	RetroAudioCapture* capture = liveCapture();
	if (capture == nullptr) { return noEngine(); }

	const QString file = args.value(QStringLiteral("file")).toString();
	if (!file.isEmpty() && !QFileInfo(file).isAbsolute())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'file' must be absolute: a relative path names a different file "
				"in the next process"));
	}
	const QString path = file.isEmpty()
		? QDir(defaultRecoveryDir()).absoluteFilePath(QStringLiteral("zene-retro-take.wav"))
		: file;

	const RetroAudioCapture::Window window = capture->takeWindow();
	const int rate = engineSampleRate();

	std::uint64_t written = 0;
	QString error;
	if (!writeRetroAudioTake(path, window.frames, rate, &written, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}

	QJsonObject result;
	result.insert(QStringLiteral("file"), path);
	result.insert(QStringLiteral("frames_written"), static_cast<qint64>(written));
	result.insert(QStringLiteral("seconds_written"), secondsFor(written, rate));
	result.insert(QStringLiteral("sample_rate"), rate);
	result.insert(QStringLiteral("channels"), 2);
	result.insert(QStringLiteral("window_quiesced"), window.quiesced);
	result.insert(QStringLiteral("window_refused_frames"), static_cast<qint64>(window.refused));
	// The window the take came FROM, so a caller can tell a short take caused by
	// a short session from one caused by the ring's bound.
	result.insert(QStringLiteral("overwritten_frames"),
		static_cast<qint64>(capture->ring().overwrittenCount()));
	result.insert(QStringLiteral("capacity_frames"),
		static_cast<qint64>(capture->ring().capacity()));
	return ControlResult::success(result);
}

// --------------------------------------------------------------------------
// registration
// --------------------------------------------------------------------------
QJsonObject captureStatusSchema()
{
	return objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("armed_before"), booleanProperty()},
		{QStringLiteral("capacity_frames"), integerProperty()},
		{QStringLiteral("capacity_seconds"), numberProperty()},
		{QStringLiteral("retained_frames"), integerProperty()},
		{QStringLiteral("window_seconds"), numberProperty()},
		{QStringLiteral("overwritten_frames"), integerProperty()},
		{QStringLiteral("paused_drop_frames"), integerProperty()},
		{QStringLiteral("refused_snapshots"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
	});
}

void registerRetroArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.retro_capture_arm");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("retro_capture_arm");
	cmd.description = QStringLiteral("Arm or disarm retrospective AUDIO capture: while armed, the "
		"engine keeps the most recent frames of its INPUT BUS in one bounded ring, so the take you "
		"did not press record for can still be written with record.retro_capture_to_take. With no "
		"'armed' argument the mode is ARMED; 'armed': false is its disarm. Nothing is recorded "
		"while disarmed, and the cost then is one relaxed atomic load per rendered period. The "
		"ring is allocated once, when the engine is built ('capacity_frames' in the reply is that "
		"allocation, %1 frames); a frame that falls out of it is counted in 'overwritten_frames' "
		"and cannot be recovered. Mode/engine state, not project state, so no transaction is "
		"recorded and control.undo has nothing to reverse (the midi.retro_capture_arm precedent, "
		"verb for verb).").arg(static_cast<qint64>(RetroAudioCapture::DefaultCapacityFrames));
	cmd.argsSchema = objectSchema({{QStringLiteral("armed"), booleanProperty()}});
	cmd.resultSchema = captureStatusSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return retroArm(args); };
	registry.registerCommand(cmd);
}

void registerRetroStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.retro_capture_status");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("retro_capture_status");
	cmd.description = QStringLiteral("What retrospective AUDIO capture is holding: whether it is "
		"armed, the ring's capacity in frames and in SECONDS, how many frames are retained right "
		"now and how many seconds that is, how many frames have already been overwritten by later "
		"audio (the window's bound, as a measurement), frames dropped because a snapshot was in "
		"flight, and the count of copies that could not obtain a consistent window. Read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = captureStatusSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return retroStatus(args); };
	registry.registerCommand(cmd);
}

void registerRetroToTake(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.retro_capture_to_take");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("retro_capture_to_take");
	cmd.description = QStringLiteral("Write the retained window to a 24-bit STEREO WAV - the take "
		"that was already played when this command ran. The window is copied under the ring's "
		"publication handshake, so the copy is never torn even with the engine running, and the "
		"file is written off the audio thread. Refused, typed, when the window holds no frames: a "
		"take with no audio in it is not a take. 'file' must be absolute and defaults to "
		"zene-retro-take.wav beside this instance's recovery file. NOT reversible and NOT inserted "
		"into the session: the file it writes is yours to keep, and turning it into a clip is the "
		"project's own load path, not this command's (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({{QStringLiteral("file"), stringProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("frames_written"), integerProperty()},
		{QStringLiteral("seconds_written"), numberProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("window_quiesced"), booleanProperty()},
		{QStringLiteral("window_refused_frames"), integerProperty()},
		{QStringLiteral("overwritten_frames"), integerProperty()},
		{QStringLiteral("capacity_frames"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return retroToTake(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRecordingRetroCommands(ControlRegistry& registry)
{
	registerRetroArm(registry);
	registerRetroStatus(registry);
	registerRetroToTake(registry);
}

} // namespace lmms
