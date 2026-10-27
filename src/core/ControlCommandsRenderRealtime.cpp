/*
 * ControlCommandsRenderRealtime.cpp - render.realtime: the project played through the device, hardware included
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

/*! Q050-089.2 (R-mixing-18): an offline export runs no device, so a channel's hardware insert is bypassed and
 *  the dry signal is rendered (render.render says so in its reply's hardware_inserts). render.realtime plays
 *  the song on the running device instead, so each insert's send goes out and its return comes back, and keeps
 *  the master mix (RealtimeExport.h) into a WAV. It refuses, and renders nothing, when no insert is on or when
 *  the device cannot carry an insert's send or return - it never falls back to an offline render. */

#include <vector>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include <sndfile.h>

#include "AudioEngine.h"
#include "BounceInPlace.h"
#include "ControlCommandDef.h"
#include "ControlExportPresetSupport.h"
#include "ControlHardwareIoSupport.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "HardwareIo.h"
#include "Mixer.h"
#include "RealtimeExport.h"
#include "SampleBuffer.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace control::hardware;

namespace
{

constexpr int MaxSeconds = 600;     //!< the same bound render.render's child wait has (10 minutes)
constexpr int DefaultTailMs = 500;
constexpr int MaxTailMs = 10000;

//! The channels whose hardware insert is on.
std::vector<MixerChannel*> insertsInUse()
{
	std::vector<MixerChannel*> out;
	Mixer* mixer = Engine::mixer();
	for (int i = 0; mixer != nullptr && i < mixer->numChannels(); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (channel != nullptr && channel->m_hardwareIo.insert.enabled()) { out.push_back(channel); }
	}
	return out;
}

QJsonArray insertsJson(const std::vector<MixerChannel*>& channels, bool withLatency)
{
	QJsonArray out;
	for (MixerChannel* channel : channels)
	{
		const HardwareInsert::Settings s = channel->m_hardwareIo.insert.settings();
		QJsonObject entry{{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("name"), channel->m_name},
			{QStringLiteral("send"), audioio::channelLabel(s.send)}, {QStringLiteral("return"), audioio::channelLabel(s.ret)}};
		if (withLatency)
		{
			entry.insert(QStringLiteral("latency_frames"), hardwareio::insertLatency(*channel));
			entry.insert(QStringLiteral("latency_source"), hardwareio::insertLatencySource(*channel));
		}
		out.append(entry);
	}
	return out;
}

//! Why the running device cannot play @a channels' inserts in real time, or empty when it can.
QString unavailableReason(const std::vector<MixerChannel*>& channels)
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr || engine->audioDev() == nullptr) { return QStringLiteral("no audio device is running"); }
	const DeviceChannels device = deviceChannels();
	const audioio::Setup setup = currentSetup();
	// The return is read from the capture: a device that delivers no input brings nothing back.
	if (!device.capture && !engine->inputStreamStarted())
	{
		return QStringLiteral("the audio device (%1) delivers no input, so no insert's return can come back")
			.arg(device.backend.isEmpty() ? QStringLiteral("none") : device.backend);
	}
	for (MixerChannel* channel : channels)
	{
		const HardwareInsert::Settings s = channel->m_hardwareIo.insert.settings();
		const QJsonObject send = destinationJson(s.send, setup, device);
		const QJsonObject ret = destinationJson(s.ret, setup, device, true);
		const QJsonObject& bad = !send.value(QStringLiteral("available")).toBool() ? send : ret;
		if (send.value(QStringLiteral("available")).toBool() && ret.value(QStringLiteral("available")).toBool()) { continue; }
		return QStringLiteral("channel %1's insert %2 %3, which the device cannot carry (%4)")
			.arg(channelIdOf(channel), &bad == &send ? QStringLiteral("sends to") : QStringLiteral("returns from"),
				bad.value(QStringLiteral("channels")).toString(), bad.value(QStringLiteral("status")).toString());
	}
	return {};
}

int sndfileFormat(OutputSettings::BitDepth depth)
{
	switch (depth)
	{
	case OutputSettings::BitDepth::Depth24Bit: return SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	case OutputSettings::BitDepth::Depth32Bit: return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	default: return SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	}
}

QString writeWav(const QString& path, const SampleBuffer& take, OutputSettings::BitDepth depth)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	SF_INFO info{};
	info.samplerate = static_cast<int>(take.sampleRate());
	info.channels = DEFAULT_CHANNELS;
	info.format = sndfileFormat(depth);
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (file == nullptr) { return QStringLiteral("cannot write %1: %2").arg(path, QString::fromUtf8(sf_strerror(nullptr))); }
	sf_command(file, SFC_SET_CLIPPING, nullptr, SF_TRUE);
	const auto frames = static_cast<sf_count_t>(take.size());
	const bool written = sf_writef_float(file, take.data()->data(), frames) == frames;
	sf_close(file);
	return written ? QString() : QStringLiteral("cannot write %1: the disk took fewer frames than were rendered").arg(path);
}

struct Plan
{
	QString out;
	tick_t endTick = 0;
	f_cnt_t skip = 0;
	f_cnt_t tail = 0;
	sample_rate_t rate = 0;
	double seconds = 0.0;
};

ControlResult readPlan(const QJsonObject& args, Plan* plan)
{
	plan->out = args.value(QStringLiteral("out")).toString();
	if (plan->out.isEmpty() || !QDir::isAbsolutePath(plan->out) || !plan->out.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'out' is an absolute path ending in .wav: a real-time render is written as WAV"));
	}
	const int tailMs = args.value(QStringLiteral("tail_ms")).toInt(DefaultTailMs);
	if (tailMs < 0 || tailMs > MaxTailMs)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'tail_ms' is 0 .. %1").arg(MaxTailMs));
	}
	Song* song = Engine::getSong();
	if (song->isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the session is empty: there is nothing to render"));
	}
	// Song::isRecording() alone stays set after a Stop (an upstream quirk): a recording is a pass that is rolling.
	const char* busy = song->isExporting() ? "an export" : song->isRecording() && !song->isStopped() ? "a recording"
		: RealtimeExportCapture::instance().state() != RealtimeExportCapture::State::Idle ? "another real-time render" : nullptr;
	if (busy != nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy, QStringLiteral("%1 is running: wait for it to end").arg(QLatin1String(busy)));
	}
	const std::vector<MixerChannel*> inserts = insertsInUse();
	if (inserts.empty())
	{
		return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("no hardware insert is on, so there is nothing to "
			"play through the device: switch one on with mixer.hardware_insert, or render offline with render.render"));
	}
	const QString unavailable = unavailableReason(inserts);
	if (!unavailable.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("real-time hardware is unavailable: %1. Nothing was "
			"rendered; render.render renders offline with the inserts bypassed (dry)").arg(unavailable));
	}
	song->updateLength();
	plan->rate = Engine::audioEngine()->outputSampleRate();
	plan->endTick = TimePos(song->length(), 0).getTicks();
	plan->skip = Engine::mixer()->totalLatencyFrames();
	plan->tail = static_cast<f_cnt_t>(static_cast<qint64>(tailMs) * plan->rate / 1000);
	plan->seconds = static_cast<double>(plan->endTick) * Engine::framesPerTick() / plan->rate + tailMs / 1000.0;
	if (plan->seconds > MaxSeconds)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the song plays for %1 s; a real-time render is "
			"bounded at %2 s").arg(plan->seconds, 0, 'f', 1).arg(MaxSeconds));
	}
	return ControlResult::success();
}

//! Plays the song from its start and waits for the capture; false when the device stopped delivering periods.
bool playAndCapture(const Plan& plan)
{
	Song* song = Engine::getSong();
	AudioEngine* engine = Engine::audioEngine();
	RealtimeExportCapture& capture = RealtimeExportCapture::instance();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	const bool loop = timeline.loopEnabled();
	const bool click = song->metronome().active();
	if (!song->isStopped()) { song->stop(); }
	const auto maxFrames = static_cast<std::uint64_t>(MaxSeconds + MaxTailMs / 1000) * plan.rate;
	// One change-lock section, so the first period that sees the take armed is the song's first. The loop is
	// off for the pass (it would never reach the end) and so is the click (an export has none).
	engine->requestChangeInModel();
	timeline.setLoopEnabled(false);
	song->metronome().setActive(false);
	song->setPlayPos(0, Song::PlayMode::Song);
	capture.arm(plan.rate, plan.endTick, plan.skip, plan.tail, engine->masterGain(), maxFrames);
	song->playSong();
	engine->doneChangeInModel();

	// The surface waits, as it does for render.render's child: a period renders on the device's thread.
	QElapsedTimer timer;
	timer.start();
	const auto budget = static_cast<qint64>(plan.seconds * 2000.0) + 10000;
	while (capture.state() != RealtimeExportCapture::State::Done && timer.elapsed() < budget) { QThread::msleep(10); }
	const bool done = capture.state() == RealtimeExportCapture::State::Done;
	song->stop();
	timeline.setLoopEnabled(loop);
	song->metronome().setActive(click);
	if (!done)
	{
		engine->requestChangeInModel();
		capture.cancel();
		engine->doneChangeInModel();
	}
	return done;
}

ControlResult renderRealtime(const QJsonObject& args)
{
	Plan plan;
	const ControlResult invalid = readPlan(args, &plan);
	if (!invalid.ok) { return invalid; }
	const std::vector<MixerChannel*> inserts = insertsInUse();
	const QJsonArray used = insertsJson(inserts, true);
	if (!playAndCapture(plan))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the audio device stopped delivering periods before "
			"the song ended; nothing was written"));
	}
	RealtimeExportCapture& capture = RealtimeExportCapture::instance();
	const auto dropped = static_cast<qint64>(capture.framesDropped());
	const bool capped = capture.capped();
	const std::shared_ptr<const SampleBuffer> take = capture.takeFinished();
	if (take == nullptr || take->size() == 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device played no frames; nothing was written"));
	}
	const ControlExportPreset settings = ControlExportPresetSettings::effective();
	const QString failed = writeWav(plan.out, *take, settings.bitDepth);
	if (!failed.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, failed); }

	const qint64 frames = BounceInPlace::wavFrameCount(plan.out);
	QJsonObject result{{QStringLiteral("path"), plan.out}, {QStringLiteral("mode"), QStringLiteral("realtime")},
		{QStringLiteral("format"), QStringLiteral("wav")}, {QStringLiteral("sample_rate"), static_cast<int>(take->sampleRate())},
		{QStringLiteral("bit_depth"), QString::fromLatin1(controlExportPresetBitDepthName(settings.bitDepth))},
		{QStringLiteral("frames"), frames}, {QStringLiteral("seconds"), static_cast<double>(frames) / take->sampleRate()},
		{QStringLiteral("bytes"), QFileInfo(plan.out).size()}, {QStringLiteral("sha256"), BounceInPlace::sha256OfFile(plan.out)},
		{QStringLiteral("latency_compensation_frames"), static_cast<int>(plan.skip)},
		{QStringLiteral("tail_frames"), static_cast<int>(plan.tail)}, {QStringLiteral("frames_dropped"), dropped},
		{QStringLiteral("capped"), capped},
		{QStringLiteral("hardware_inserts"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("realtime")},
			{QStringLiteral("hardware"), QStringLiteral("available")}, {QStringLiteral("bypassed"), false},
			{QStringLiteral("inserts"), used}}}};
	return ControlResult::success(result);
}

} // namespace

QJsonObject offlineHardwareInsertsJson()
{
	const std::vector<MixerChannel*> inserts = insertsInUse();
	return {{QStringLiteral("mode"), QStringLiteral("offline")}, {QStringLiteral("hardware"), QStringLiteral("unavailable")},
		{QStringLiteral("bypassed"), !inserts.empty()}, {QStringLiteral("inserts"), insertsJson(inserts, false)},
		{QStringLiteral("note"), inserts.empty() ? QStringLiteral("no hardware insert is on")
			: QStringLiteral("an offline render runs no audio device, so these hardware inserts were bypassed and the dry "
				"signal was rendered; render.realtime plays the song through them")}};
}

void registerRenderRealtimeCommand(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("render.realtime"), QStringLiteral("Render the project IN REAL TIME through the running "
		"audio device, so every hardware insert that is on is heard: its send goes out to the outboard gear and its return is "
		"recorded into the mix (an offline render - render.render - bypasses them and renders the dry signal). The song plays from "
		"its start to its end plus 'tail_ms' (default 500, at most 10000), with the loop and the metronome off for the pass, and "
		"the master mix is written to 'out' (absolute, .wav) at the device's sample rate and the export bit depth in force; the "
		"head that delay compensation adds is dropped, so frame k of the file is song frame k, as offline. The reply has the "
		"path, frames, seconds, sha256, the compensation dropped and each insert played (channel, send, return, latency). Refused, "
		"with nothing rendered and no offline fallback, when no insert is on or the device cannot carry an insert's send or "
		"return or delivers no input. The surface does not answer until the pass ends (at most 600 s of song). Changes nothing "
		"in the project."),
		{{QStringLiteral("out"), stringProperty()}, {QStringLiteral("tail_ms"), integerProperty(0, MaxTailMs)}},
		{QStringLiteral("out")}, false, renderRealtime);
}

} // namespace lmms
