/*
 * SessionAudioCellPlaybackTest.cpp - a launched AUDIO cell sounds its clip (0.5.0 L02, WF-055 / WF-054)
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

/*! "Ready to launch": an audio cell on a sample track's column, launched through the agent surface on the
 *  Dummy device (the test drives the periods), plays its clip through the track - the engine's output is the
 *  clip, frame for frame, from its top, looping round its end - and a cell recorded with session.record_slot and
 *  landed plays back the take it recorded. Before this path existed a launched audio cell only silenced its
 *  column: the same binary built without it (the recording step alone) renders silence and fails both cases. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <initializer_list>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleFrame.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "SessionAudioBake.h"
#include "SessionAudioCells.h"
#include "SessionModel.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Clip frame @a n: distinct for every frame of the clip, well inside full scale.
SampleFrame clipFrame(std::size_t n)
{
	return SampleFrame(static_cast<float>(n % 997) / 2048.0f, -static_cast<float>(n % 1009) / 2048.0f);
}

//! Two steps of a 24-bit file (libsndfile writes with 2^23 - 1 and reads with 2^23), plus float mixing.
constexpr float Tolerance = 4.0f / 8388608.0f;

bool writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const sf_count_t written = sf_writef_float(file, &frames[0][0], static_cast<sf_count_t>(frames.size()));
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

std::vector<SampleFrame> readWav(const QString& path)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (file == nullptr) { return {}; }
	std::vector<SampleFrame> frames(static_cast<std::size_t>(info.frames));
	sf_readf_float(file, &frames[0][0], info.frames);
	sf_close(file);
	return frames;
}

} // namespace

class SessionAudioCellPlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
	}

	//! A file in a cell: launched, the output is the clip from its top and round its end again.
	void aLaunchedAudioCellPlaysItsClipThroughItsTrack()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> clip(1500);
		for (std::size_t i = 0; i < clip.size(); ++i) { clip[i] = clipFrame(i); }
		const QString path = m_dir.filePath(QStringLiteral("clip.wav"));
		QVERIFY(writeFloatWav(path, clip, rate));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path}}).ok);
		launchAndCompare(clip, 0);
	}

	//! The take a cell recorded, landed, is what the cell plays when it is launched again.
	void aRecordedCellPlaysBackItsTake()
	{
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("bars"), 1}}).ok);
		for (int i = 0; i < 6000 && status() != QStringLiteral("done"); ++i) { period(); }
		QCOMPARE(status(), QStringLiteral("done"));
		const QString path = m_dir.filePath(QStringLiteral("take.wav"));
		const ControlResult landed = run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		for (int i = 0; i < 8; ++i) { period(); }  // the recorded cell has stopped; the column is silent again
		launchAndCompare(readWav(path), 1);
	}

	//! The realtime rule: the renderer's audio-thread entry point allocates nothing while a cell plays,
	//! across launches and the clip's wrap (AllocationProbe.h, the SessionSchedulerTest pattern).
	void theCellRendererDoesNotAllocate()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> clip(700);
		for (std::size_t i = 0; i < clip.size(); ++i) { clip[i] = clipFrame(i + 1); }
		const QString path = m_dir.filePath(QStringLiteral("probe.wav"));
		QVERIFY(writeFloatWav(path, clip, rate));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path}}).ok);
		QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().front());
		QVERIFY(track != nullptr);
		SessionCellPlayHandle probe(track);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()));
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < 2000; ++i) { probe.play(buffer); }
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(allocations == 0, qPrintable(QStringLiteral("the cell renderer allocated %1 times").arg(allocations)));
		QCOMPARE(probe.framesRendered(), std::uint64_t{2000} * buffer.size());
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		period();
	}

	// ---- Q050-013: instrument columns and the cell's stored playback settings ----

	//! An audio cell on an INSTRUMENT track's column plays its clip through that track.
	void aLaunchedAudioCellPlaysThroughAnInstrumentTrack()
	{
		useTrack(QStringLiteral("instrument"));
		const std::vector<SampleFrame> clip = rampClip(1500);
		setAudioCell(clip, QStringLiteral("inst.wav"), {});
		launchAndCompare(clip, 0);
	}

	//! gain_db scales the rendered level on both track types: -6.0206 dB is exactly half amplitude.
	void gainScalesTheRenderedLevelOnBothTrackTypes()
	{
		for (const QString& type : {QStringLiteral("sample"), QStringLiteral("instrument")})
		{
			useTrack(type);
			const std::vector<SampleFrame> clip = rampClip(1500);
			setAudioCell(clip, QStringLiteral("gain.wav"), {{QStringLiteral("gain_db"), -6.020599913}});
			std::vector<SampleFrame> half = clip;
			for (SampleFrame& f : half) { f = f * 0.5f; }
			launchAndCompare(half, 0);
		}
	}

	//! loop_start / loop_length: the cell starts at its loop start and wraps at its loop end, on both track types.
	void theLoopRegionBoundsThePlayedFrames()
	{
		for (const QString& type : {QStringLiteral("sample"), QStringLiteral("instrument")})
		{
			useTrack(type);
			const std::vector<SampleFrame> clip = rampClip(1500);
			setAudioCell(clip, QStringLiteral("loop.wav"),
				{{QStringLiteral("loop_start"), 2}, {QStringLiteral("loop_length"), 1}});
			const float fpt = Engine::framesPerTick();
			const auto start = static_cast<std::size_t>(std::llround(2 * fpt));
			const auto length = static_cast<std::size_t>(std::llround(1 * fpt));
			QVERIFY(start + length < clip.size());
			const std::vector<SampleFrame> region(clip.begin() + static_cast<long>(start),
				clip.begin() + static_cast<long>(start + length));
			launchAndCompare(region, 0);
		}
	}

	//! transpose / detune repitch: +12 semitones doubles the frequency (and halves the length); +100 cents is one semitone.
	void transposeAndDetuneRepitchTheClip()
	{
		useTrack(QStringLiteral("sample"));
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const std::vector<SampleFrame> tone = sineClip(rate * 2, rate / 100);  // 100 Hz
		struct Case { QJsonObject settings; double ratio; };
		const std::vector<Case> cases = {
			{{{QStringLiteral("transpose"), 12}}, 2.0},
			{{{QStringLiteral("transpose"), -12}}, 0.5},
			{{{QStringLiteral("detune"), 100}}, std::exp2(1.0 / 12.0)},
			{{{QStringLiteral("transpose"), 5}, {QStringLiteral("detune"), -50}}, std::exp2(4.5 / 12.0)},
		};
		for (const Case& c : cases)
		{
			setAudioCell(tone, QStringLiteral("tone.wav"), c.settings);
			const double crossings = crossingsPerSecond(launchAndCollect(0, rate));
			QVERIFY2(std::fabs(crossings / 100.0 - c.ratio) < 0.02 * c.ratio,
				qPrintable(QStringLiteral("ratio %1 expected, measured %2").arg(c.ratio).arg(crossings / 100.0)));
		}
	}

	//! Warp: the clip recorded at warp_bpm lasts warp_bpm/tempo as long at the song's tempo, pitch unchanged -
	//! checked at two tempos (60 and 240 against a 120 bpm clip).
	void warpFollowsTheSongTempoKeepingPitch()
	{
		useTrack(QStringLiteral("sample"));
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const std::vector<SampleFrame> tone = sineClip(rate, rate / 400);  // one second, 400 Hz (the stretcher finds periods below 128 frames)
		for (const int tempo : {60, 240})
		{
			QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), tempo}}).ok);
			setAudioCell(tone, QStringLiteral("warp.wav"), {{QStringLiteral("warp"), true}, {QStringLiteral("warp_bpm"), 120.0}});
			// the duration: the baked clip's length is the clip's length x warp_bpm / tempo
			const sessionaudio::BakeParams params = sessionaudio::bakeParamsFor(
				Engine::getSong()->sessionModel().slot(0, 0), tempo);
			const auto baked = sessionaudio::bakeClip(tone.data(), tone.size(), params);
			const double expected = static_cast<double>(tone.size()) * 120.0 / tempo;
			QVERIFY2(std::fabs(static_cast<double>(baked.size()) - expected) < 2.0,
				qPrintable(QStringLiteral("tempo %1: %2 frames, expected %3").arg(tempo).arg(baked.size()).arg(expected)));
			// the pitch, rendered: still 400 Hz
			const std::vector<SampleFrame> out = launchAndCollect(0, rate / 4);
			QVERIFY2(std::fabs(crossingsPerSecond(out) - 400.0) < 12.0,
				qPrintable(QStringLiteral("tempo %1: %2 Hz").arg(tempo).arg(crossingsPerSecond(out))));
			// the level: a warped sine keeps its amplitude
			float peak = 0.0f;
			for (std::size_t i = out.size() / 2; i < out.size(); ++i) { peak = std::max(peak, std::fabs(out[i].left())); }
			QVERIFY2(peak > 0.45f && peak < 0.55f, qPrintable(QStringLiteral("peak %1").arg(peak)));
		}
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
	}

	//! A cell's settings survive save and load (the attributes are written only when set).
	void theCellSettingsRoundTripThroughTheSessionBlock()
	{
		ClipSlot slot;
		slot.setAudioReference(QStringLiteral("a.wav"));
		slot.setWarp(true);
		slot.setWarpBpm(97.5);
		slot.setGainDb(-3.0f);
		slot.setTranspose(7);
		slot.setDetune(-20);
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("clip"));
		slot.saveState(doc, element);
		ClipSlot back;
		back.restoreState(element);
		QVERIFY(back.warp());
		QCOMPARE(back.warpBpm(), 97.5);
		QCOMPARE(back.transpose(), 7);
		QCOMPARE(back.detune(), -20);
		QCOMPARE(back.gainDb(), -3.0f);
		ClipSlot plain;
		plain.setAudioReference(QStringLiteral("a.wav"));
		QDomElement quiet = doc.createElement(QStringLiteral("clip"));
		plain.saveState(doc, quiet);
		QVERIFY(!quiet.hasAttribute(QStringLiteral("warp")));  // a project that never warps saves as before
	}

	//! The realtime rule on BOTH track types, with gain, loop region and a pitch/warp-baked clip in play.
	void theInstrumentCellRendererDoesNotAllocateEither()
	{
		for (const QString& type : {QStringLiteral("sample"), QStringLiteral("instrument")})
		{
			useTrack(type);
			const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
			const std::vector<SampleFrame> tone = sineClip(rate / 2, rate / 100);
			setAudioCell(tone, QStringLiteral("probe2.wav"), {{QStringLiteral("gain_db"), -3.0}, {QStringLiteral("transpose"), 3},
				{QStringLiteral("warp"), true}, {QStringLiteral("warp_bpm"), 100.0}, {QStringLiteral("loop_start"), 1},
				{QStringLiteral("loop_length"), 4}});
			QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
			for (int i = 0; i < 4; ++i) { period(); }
			Track* track = Engine::getSong()->tracks().front();
			std::unique_ptr<SessionCellPlayHandle> probe;
			if (auto* st = dynamic_cast<SampleTrack*>(track)) { probe = std::make_unique<SessionCellPlayHandle>(st); }
			else { probe = std::make_unique<SessionCellPlayHandle>(dynamic_cast<InstrumentTrack*>(track)); }
			std::vector<SampleFrame> buffer(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()));
			lmms::test::tlCountAllocations = true;
			lmms::test::resetAllocationCount();
			for (int i = 0; i < 2000; ++i) { probe->play(buffer); }
			const std::uint64_t allocations = lmms::test::tlAllocationCount;
			lmms::test::tlCountAllocations = false;
			QVERIFY2(allocations == 0, qPrintable(QStringLiteral("%1 track: the cell renderer allocated %2 times").arg(type).arg(allocations)));
			QCOMPARE(probe->framesRendered(), std::uint64_t{2000} * buffer.size());
			probe.reset();
			QVERIFY(run(QStringLiteral("session.stop_all")).ok);
			period();
		}
	}

	//! A cached clip resampled for the engine rate is rebuilt when the engine rate changes.
	void cachedClipsRebuildWhenEngineRateChanges()
	{
		const auto originalRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const std::vector<SampleFrame> clip = rampClip(1000);
		const QString path = m_dir.filePath(QStringLiteral("rate_test.wav"));
		QVERIFY(writeFloatWav(path, clip, originalRate));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path}}).ok);
		Song& song = *Engine::getSong();
		const ClipSlot& slot = song.sessionModel().slot(0, 0);
		const sessionaudio::Prepared p1 = sessionaudio::prepare(song, 0, slot, 120.0);
		QVERIFY(p1.clip != nullptr);
		QCOMPARE(static_cast<int>(p1.clip->sampleRate()), originalRate);
		const int newRate = originalRate == 44100 ? 48000 : 44100;
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(newRate);
		const sessionaudio::Prepared p2 = sessionaudio::prepare(song, 0, slot, 120.0);
		QVERIFY(p2.clip != nullptr);
		QCOMPARE(static_cast<int>(p2.clip->sampleRate()), newRate);
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(originalRate);
	}

private:
	void useTrack(const QString& type)
	{
		Engine::getSong()->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
	}

	static std::vector<SampleFrame> rampClip(std::size_t n)
	{
		std::vector<SampleFrame> clip(n);
		for (std::size_t i = 0; i < n; ++i) { clip[i] = clipFrame(i); }
		return clip;
	}

	//! A sine of @a periodFrames frames per cycle at amplitude 0.5.
	static std::vector<SampleFrame> sineClip(int frames, int periodFrames)
	{
		std::vector<SampleFrame> clip(static_cast<std::size_t>(frames));
		for (int i = 0; i < frames; ++i)
		{
			const float v = 0.5f * std::sin(6.2831853f * static_cast<float>(i % periodFrames) / static_cast<float>(periodFrames));
			clip[static_cast<std::size_t>(i)] = SampleFrame(v, v);
		}
		return clip;
	}

	void setAudioCell(const std::vector<SampleFrame>& clip, const QString& name, QJsonObject settings)
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const QString path = m_dir.filePath(name);
		QVERIFY(writeFloatWav(path, clip, rate));
		// set_slot writes only the fields given, so every setting starts from its default here
		for (const auto& [key, value] : std::initializer_list<std::pair<const char*, QJsonValue>>{
				 {"gain_db", 0.0}, {"transpose", 0}, {"detune", 0}, {"warp", false}, {"warp_bpm", 120.0},
				 {"loop_start", 0}, {"loop_length", 0}})
		{
			if (!settings.contains(QLatin1String(key))) { settings.insert(QLatin1String(key), value); }
		}
		settings.insert(QStringLiteral("track"), 0);
		settings.insert(QStringLiteral("scene"), 0);
		settings.insert(QStringLiteral("type"), QStringLiteral("audio"));
		settings.insert(QStringLiteral("source"), path);
		const ControlResult set = run(QStringLiteral("session.set_slot"), settings);
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
	}

	//! Launches cell (0, @a scene) and returns @a frames of its output from the first sound on, then stops it.
	std::vector<SampleFrame> launchAndCollect(int scene, int frames)
	{
		m_out.clear();
		const ControlResult launched = run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), scene}, {QStringLiteral("quantisation"), QStringLiteral("none")}});
		if (!launched.ok) { QTest::qFail(qPrintable(launched.errorMessage), __FILE__, __LINE__); return {}; }
		const auto need = static_cast<std::size_t>(frames) + 16 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		while (m_out.size() < need) { period(); }
		std::size_t first = 0;
		while (first < m_out.size() && m_out[first].left() == 0.0f && m_out[first].right() == 0.0f) { ++first; }
		std::vector<SampleFrame> out(m_out.begin() + static_cast<long>(first), m_out.end());
		out.resize(std::min(out.size(), static_cast<std::size_t>(frames)));
		run(QStringLiteral("session.stop_all"));
		period();
		return out;
	}

	//! Rising zero crossings per second of the left channel (a 100 Hz sine gives 100).
	static double crossingsPerSecond(const std::vector<SampleFrame>& out)
	{
		const auto rate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
		const std::size_t from = out.size() / 8;  // clear of the stretcher's onset
		int crossings = 0;
		for (std::size_t i = from + 1; i < out.size(); ++i)
		{
			if (out[i - 1].left() < 0.0f && out[i].left() >= 0.0f) { ++crossings; }
		}
		return crossings * rate / static_cast<double>(out.size() - from);
	}
	//! One period of input (a ramp, so a take holds something), rendered; its output is appended to m_out.
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		for (f_cnt_t f = 0; f < fpp; ++f) { block[static_cast<std::size_t>(f)] = clipFrame(m_fed++ * 7 + 3); }
		engine->pushInputFrames(block.data(), fpp);
		const std::span<const SampleFrame> out = engine->renderNextPeriod();
		m_out.insert(m_out.end(), out.begin(), out.end());
	}

	QString status() const
	{
		return run(QStringLiteral("session.record_slot_status")).result.value(QStringLiteral("state")).toString();
	}

	void launchAndCompare(const std::vector<SampleFrame>& clip, int scene)
	{
		QVERIFY(!clip.empty());
		m_out.clear();
		const ControlResult launched = run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), scene}, {QStringLiteral("quantisation"), QStringLiteral("none")}});
		QVERIFY2(launched.ok, qPrintable(launched.errorMessage));
		const std::size_t enough = 2 * clip.size() + 8 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		while (m_out.size() < enough) { period(); }
		std::size_t first = 0;
		while (first < m_out.size() && std::fabs(m_out[first].left()) + std::fabs(m_out[first].right()) == 0.0f) { ++first; }
		QVERIFY2(first < m_out.size(), "the launched audio cell rendered silence");
		std::size_t from = 0;  // the clip may open on silent frames: line the first sound up with the clip's
		while (from < clip.size() && std::fabs(clip[from].left()) + std::fabs(clip[from].right()) == 0.0f) { ++from; }
		QVERIFY(first >= from);
		first -= from;
		QVERIFY(first + 2 * clip.size() <= m_out.size());
		for (std::size_t i = 0; i < 2 * clip.size(); ++i)  // twice round: from the top, and looping
		{
			const SampleFrame& want = clip[i % clip.size()];
			const SampleFrame& got = m_out[first + i];
			if (std::fabs(got.left() - want.left()) > Tolerance || std::fabs(got.right() - want.right()) > Tolerance)
			{
				QFAIL(qPrintable(QStringLiteral("output frame %1 is (%2, %3), the clip's frame %4 is (%5, %6)").arg(i)
					.arg(got.left()).arg(got.right()).arg(i % clip.size()).arg(want.left()).arg(want.right())));
			}
		}
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		period();
	}

	QTemporaryDir m_dir;
	std::uint64_t m_fed = 0;
	std::vector<SampleFrame> m_out;
};

QTEST_GUILESS_MAIN(SessionAudioCellPlaybackTest)
#include "SessionAudioCellPlaybackTest.moc"
