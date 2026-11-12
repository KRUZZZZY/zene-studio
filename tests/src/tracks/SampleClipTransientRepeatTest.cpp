/*
 * SampleClipTransientRepeatTest.cpp - transient Repeat / Gate warp modes (Q050-162)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The transient fixture is four decaying 1 kHz bursts, one every 0.5 s of a
 *  2 s clip, warped to twice its length - so each transient owns a 1 s slot at
 *  120 BPM and only 0.5 s of audio to fill it with. Measured on real play
 *  handles, period by period, the constructor SampleTrack::play uses:
 *   * Repeat and Gate at a free rate fill the warped span exactly: Repeat
 *     retriggers each segment at every interval (a partial last one at the
 *     clip end included), Gate plays each once and is silent after the first
 *     interval - two audibly different renders of one clip;
 *   * a synced rate follows the project tempo (the same onsets per slot at 60
 *     and 120 BPM, twice as far apart) and a free rate does not (the same
 *     spacing in milliseconds at both tempos);
 *   * the mode and the rate go through warp.stretch (typed refusals, the
 *     read-back in warp.list, one undo per call) and a real project save/load,
 *     and the reloaded clips render bit for bit what they rendered before;
 *   * a project that never used the modes writes and reads exactly as before.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "TransientRepeat.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;
constexpr int kBursts = 4;
constexpr double kBurstSpacing = 0.5;  // s of source between transients
constexpr double kBurstDecay = 0.45;   // s for a burst to decay to silence
constexpr float kOnsetLevel = 0.5f;

//! Four bursts at 0, 0.5, 1.0 and 1.5 s: a full-scale 1 kHz sine decaying
//! linearly to silence over 0.45 s, then 50 ms of silence before the next.
SampleClip* makeTransientClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(kBursts * kBurstSpacing * rate), SampleFrame(0.0f, 0.0f));
	for (std::size_t f = 0; f < data.size(); ++f)
	{
		const double t = std::fmod(static_cast<double>(f) / rate, kBurstSpacing);
		if (t >= kBurstDecay) { continue; }
		const auto value = static_cast<sample_t>(0.8 * (1.0 - t / kBurstDecay) * std::sin(kTwoPi * 1000.0 * t));
		data[f] = SampleFrame(value, value);
	}
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

//! The clip's 2 s pinned to twice the ticks it spans at the current tempo.
void applyHalfSpeedWarp(SampleClip& clip, int sourceRate)
{
	const auto frames = static_cast<f_cnt_t>(clip.sample().sampleSize());
	const auto ticks = static_cast<tick_t>(std::llround(2.0 * frames / Engine::framesPerTick(sourceRate)));
	const std::array<WarpMarker, 2> markers{WarpMarker{0, 0}, WarpMarker{frames, ticks}};
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

std::vector<SampleFrame> renderClip(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> rendered;
	std::vector<SampleFrame> buffer(512);
	for (int guard = 0; static_cast<int>(handle.framesDone()) < total && guard < total / 512 + 16; ++guard)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		rendered.insert(rendered.end(), buffer.begin(), buffer.end());
	}
	rendered.resize(static_cast<std::size_t>(total));
	return rendered;
}

//! Output frames the clip's window spans on the timeline: the warped target.
f_cnt_t warpedSpan(const SampleClip& clip)
{
	return static_cast<f_cnt_t>(clip.windowTicksFor(clip.sampleWindow())
		* Engine::framesPerTick(Engine::audioEngine()->outputSampleRate()));
}

//! Where the render jumps to an attack, in seconds: a 1 ms block whose peak is
//! at least kOnsetLevel and more than 10 % above both blocks before it (the
//! bursts only decay, so only a (re)trigger or a new burst rises), 5 ms apart at least.
//! The loudest sample of every @a block frames.
std::vector<double> blockPeaks(const std::vector<SampleFrame>& data, std::size_t block)
{
	std::vector<double> peaks;
	for (std::size_t from = 0; from < data.size(); from += block)
	{
		double peak = 0.0;
		for (std::size_t f = from; f < std::min(from + block, data.size()); ++f)
		{
			peak = std::max(peak, static_cast<double>(std::fabs(data[f][0])));
		}
		peaks.push_back(peak);
	}
	return peaks;
}

std::vector<double> onsets(const std::vector<SampleFrame>& data, int rate)
{
	const auto block = static_cast<std::size_t>(rate / 1000);
	const std::vector<double> peaks = blockPeaks(data, block);
	std::vector<double> out;
	for (std::size_t b = 0; b < peaks.size(); ++b)
	{
		const double before = std::max(b >= 1 ? peaks[b - 1] : 0.0, b >= 2 ? peaks[b - 2] : 0.0);
		const double at = static_cast<double>(b * block) / rate;
		// One attack can straddle two blocks (the ramp before a retrigger): count it once.
		if (peaks[b] >= kOnsetLevel && peaks[b] > 1.1 * before && (out.empty() || at - out.back() > 0.005))
		{
			out.push_back(at);
		}
	}
	return out;
}

double peakBetween(const std::vector<SampleFrame>& data, int rate, double from, double to)
{
	double peak = 0.0;
	for (auto f = static_cast<std::size_t>(from * rate); f < static_cast<std::size_t>(to * rate) && f < data.size(); ++f)
	{
		peak = std::max(peak, static_cast<double>(std::fabs(data[f][0])));
	}
	return peak;
}

/*! The onsets the clip's own segments predict: each segment starts where the
 *  half-speed warp puts its transient (twice its source time at 120 BPM, scaled
 *  with the span @a end), the window's head for the first; then (Repeat) every
 *  @a interval s until the next segment or the clip's end - the partial last
 *  interval included. Gate: the segment starts only. */
std::vector<double> expectedOnsets(const SampleClip& clip, double interval, double end, bool repeat)
{
	std::vector<double> starts{0.0};
	const TransientTable table = clip.transientTable();
	for (int i = 0; i < table.size(); ++i)
	{
		starts.push_back(static_cast<double>(table[i]) / clip.sample().sampleRate() * (end / 2.0));
	}
	std::vector<double> out;
	for (std::size_t k = 0; k < starts.size(); ++k)
	{
		const double stop = k + 1 < starts.size() ? starts[k + 1] : end;
		for (double t = starts[k]; t < stop - 0.0015; t += interval)
		{
			out.push_back(t);
			if (!repeat) { break; }
		}
	}
	return out;
}

//! As many onsets as expected, each within 3 ms of its expected time.
bool onsetsMatch(const std::vector<double>& found, const std::vector<double>& expected)
{
	if (found.size() != expected.size()) { return false; }
	for (std::size_t i = 0; i < found.size(); ++i)
	{
		if (std::fabs(found[i] - expected[i]) > 0.003) { return false; }
	}
	return true;
}

QString describe(const std::vector<double>& found)
{
	QStringList parts;
	for (const double t : found) { parts << QString::number(t, 'f', 3); }
	return parts.join(QLatin1Char(' '));
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0)
{
	std::printf("TRANSIENT_REPEAT_EVIDENCE %-48s %10.4f %10.4f %10.4f\n", label, a, b, c);
	std::fflush(stdout);
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace


class SampleClipTransientRepeatTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	}

	void init() { Engine::getSong()->setTempo(120); }

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The clip's transients are analysed when it enters a transient mode, off
	//! the render path, and land on the fixture's burst onsets.
	void theTransientsAreAnalysedWhenTheModeIsChosen()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*clip, m_rate);
		QCOMPARE(clip->transientTable().size(), 0);
		clip->setWarpStretchMode(WarpStretchMode::Repeat);
		const TransientTable table = clip->transientTable();
		QStringList frames;
		for (int i = 0; i < table.size(); ++i) { frames << QString::number(table[i]); }
		std::printf("TRANSIENT_REPEAT_EVIDENCE transients: %s\n", qPrintable(frames.join(QLatin1Char(' '))));
		// The burst at 0 is the window's own head; the other three are transients.
		int found = 0;
		for (int i = 0; i < table.size(); ++i)
		{
			for (int k = 1; k < kBursts; ++k)
			{
				const double expected = k * kBurstSpacing * m_rate;
				if (std::fabs(static_cast<double>(table[i]) - expected) < 0.01 * m_rate) { ++found; }
			}
		}
		QVERIFY2(found == kBursts - 1, qPrintable(QStringLiteral("transients: %1").arg(frames.join(' '))));
	}

	//! ACCEPTANCE 1 (free rate): Repeat and Gate at a free 300 ms fill the 4 s
	//! warped span exactly. Repeat retriggers at 0/300/600/900 ms of each 1 s
	//! slot - the last a partial interval that the slot (and at the end, the
	//! clip) cuts at 100 ms; Gate plays 300 ms of each burst and is silent for
	//! the rest of its slot.
	void repeatAndGateFillTheWarpedSpanAtAFreeRate()
	{
		SampleTrack track(Engine::getSong());
		auto* repeat = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*repeat, m_rate);
		repeat->setTransientRepeatRate(TransientRepeatRate::freeInterval(300.0));
		repeat->setWarpStretchMode(WarpStretchMode::Repeat);
		auto* gate = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*gate, m_rate);
		gate->setTransientRepeatRate(TransientRepeatRate::freeInterval(300.0));
		gate->setWarpStretchMode(WarpStretchMode::Gate);

		const std::vector<SampleFrame> repeated = renderClip(repeat);
		const std::vector<SampleFrame> gated = renderClip(gate);
		const f_cnt_t target = warpedSpan(*repeat);
		evidence("target frames / repeat frames / gate frames", static_cast<double>(target),
			static_cast<double>(repeated.size()), static_cast<double>(gated.size()));
		// The warped span is 4 s (to the tick), and both renders last exactly it.
		QVERIFY(std::fabs(static_cast<double>(target) / m_rate - 4.0) < 0.011);
		QCOMPARE(static_cast<f_cnt_t>(repeated.size()), target);
		QCOMPARE(static_cast<f_cnt_t>(gated.size()), target);

		const std::vector<double> repeatOnsets = onsets(repeated, m_rate);
		const std::vector<double> gateOnsets = onsets(gated, m_rate);
		std::printf("TRANSIENT_REPEAT_EVIDENCE repeat onsets: %s\n", qPrintable(describe(repeatOnsets)));
		std::printf("TRANSIENT_REPEAT_EVIDENCE gate onsets:   %s\n", qPrintable(describe(gateOnsets)));
		QVERIFY2(onsetsMatch(repeatOnsets, expectedOnsets(*repeat, 0.3, 4.0, true)), qPrintable(describe(repeatOnsets)));
		QCOMPARE(static_cast<int>(repeatOnsets.size()), 16);
		QVERIFY2(onsetsMatch(gateOnsets, expectedOnsets(*gate, 0.3, 4.0, false)), qPrintable(describe(gateOnsets)));
		QCOMPARE(static_cast<int>(gateOnsets.size()), kBursts);

		for (int k = 0; k < kBursts; ++k)
		{
			// Gate: the burst sounds up to the interval, then silence to the slot's end.
			QVERIFY(peakBetween(gated, m_rate, k + 0.02, k + 0.28) > 0.2);
			QVERIFY2(peakBetween(gated, m_rate, k + 0.31, k + 0.99) < 1e-6,
				qPrintable(QStringLiteral("gate sounded after its interval in slot %1").arg(k)));
			// Repeat: the same span is filled by the retriggers.
			QVERIFY(peakBetween(repeated, m_rate, k + 0.31, k + 0.99) > 0.5);
		}
		// The partial last interval: the retrigger at 3.9 s sounds until the clip ends.
		const double end = static_cast<double>(target) / m_rate;
		QVERIFY(peakBetween(repeated, m_rate, 3.9, 3.95) > 0.5);
		QVERIFY(peakBetween(repeated, m_rate, end - 0.02, end) > 0.3);

		double difference = 0.0;
		for (std::size_t f = 0; f < repeated.size(); ++f) { difference += std::fabs(repeated[f][0] - gated[f][0]); }
		difference /= static_cast<double>(repeated.size());
		evidence("mean |repeat - gate|", difference);
		QVERIFY(difference > 0.05);
	}

	//! ACCEPTANCE 1 (synced vs free): a synced 1/8 is 250 ms at 120 BPM and
	//! 500 ms at 60 - four retriggers per slot either way, as the slot doubles
	//! with the interval - while a free 300 ms stays 300 ms at 60 BPM and so
	//! fits seven retriggers into the 2 s slot.
	void aSyncedRateFollowsTheTempoAndAFreeOneDoesNot()
	{
		SampleTrack track(Engine::getSong());
		auto* synced = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*synced, m_rate);
		synced->setTransientRepeatRate(TransientRepeatRate::syncedTo(8));
		synced->setWarpStretchMode(WarpStretchMode::Repeat);
		auto* unsynced = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*unsynced, m_rate);
		unsynced->setTransientRepeatRate(TransientRepeatRate::freeInterval(300.0));
		unsynced->setWarpStretchMode(WarpStretchMode::Repeat);

		const std::vector<double> synced120 = onsets(renderClip(synced), m_rate);
		const std::vector<double> free120 = onsets(renderClip(unsynced), m_rate);
		Engine::getSong()->setTempo(60);
		const f_cnt_t span60 = warpedSpan(*synced);
		const std::vector<SampleFrame> synced60Render = renderClip(synced);
		const std::vector<double> synced60 = onsets(synced60Render, m_rate);
		const std::vector<double> free60 = onsets(renderClip(unsynced), m_rate);
		std::printf("TRANSIENT_REPEAT_EVIDENCE synced 1/8 @120: %s\n", qPrintable(describe(synced120)));
		std::printf("TRANSIENT_REPEAT_EVIDENCE synced 1/8 @60:  %s\n", qPrintable(describe(synced60)));
		std::printf("TRANSIENT_REPEAT_EVIDENCE free 300ms @60:  %s\n", qPrintable(describe(free60)));
		evidence("span @60 (s)", static_cast<double>(span60) / m_rate);

		QVERIFY(std::fabs(static_cast<double>(span60) / m_rate - 8.0) < 0.021);
		QCOMPARE(static_cast<f_cnt_t>(synced60Render.size()), span60);
		QVERIFY2(onsetsMatch(synced120, expectedOnsets(*synced, 0.25, 4.0, true)), qPrintable(describe(synced120)));
		QVERIFY2(onsetsMatch(synced60, expectedOnsets(*synced, 0.5, 8.0, true)), qPrintable(describe(synced60)));
		QVERIFY2(onsetsMatch(free120, expectedOnsets(*unsynced, 0.3, 4.0, true)), qPrintable(describe(free120)));
		QVERIFY2(onsetsMatch(free60, expectedOnsets(*unsynced, 0.3, 8.0, true)), qPrintable(describe(free60)));
		// Synced: the same retriggers per slot at both tempos; free: more slots' worth at 60 BPM.
		QCOMPARE(synced60.size(), synced120.size());
		QVERIFY(free60.size() > free120.size() + 8);
	}

	//! ACCEPTANCE 2 (agent): warp.stretch selects both modes with either kind
	//! of rate, reports them (and the interval at the current tempo) in its
	//! reply and in warp.list, refuses a bad rate typed, and one control.undo
	//! takes back one call - mode, rate, or both.
	void theModeAndRateGoThroughTheCommandAndUndo()
	{
		const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("warp.stretch"));
		QVERIFY(command != nullptr);
		const QJsonObject properties = command->argsSchema.value(QStringLiteral("properties")).toObject();
		const QJsonArray modes = properties.value(QStringLiteral("mode")).toObject().value(QStringLiteral("enum")).toArray();
		QVERIFY(modes.contains(QStringLiteral("repeat")) && modes.contains(QStringLiteral("gate")));
		QVERIFY(properties.contains(QStringLiteral("rate_division")) && properties.contains(QStringLiteral("rate_ms")));

		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* linear = makeTransientClip(*track, m_rate);
		const QString linearId = control::clipIdOf(linear);
		const ControlResult refused = run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), linearId}, {QStringLiteral("mode"), QStringLiteral("repeat")}});
		QVERIFY(!refused.ok);
		QCOMPARE(static_cast<int>(refused.errorKind), static_cast<int>(ControlErrorKind::Refused));

		auto* clip = makeTransientClip(*track, m_rate);
		applyHalfSpeedWarp(*clip, m_rate);
		const QString id = control::clipIdOf(clip);
		const QJsonObject both{{QStringLiteral("clip"), id}, {QStringLiteral("mode"), QStringLiteral("repeat")},
			{QStringLiteral("rate_division"), QStringLiteral("1/8")}, {QStringLiteral("rate_ms"), 100}};
		QCOMPARE(static_cast<int>(run(QStringLiteral("warp.stretch"), both).errorKind),
			static_cast<int>(ControlErrorKind::InvalidArgs));
		QVERIFY(!run(QStringLiteral("warp.stretch"), {{QStringLiteral("clip"), id},
			{QStringLiteral("mode"), QStringLiteral("repeat")}, {QStringLiteral("rate_ms"), 1}}).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Resample);

		// Repeat at a synced 1/8: 250 ms at 120 BPM.
		const ControlResult repeat = run(QStringLiteral("warp.stretch"), {{QStringLiteral("clip"), id},
			{QStringLiteral("mode"), QStringLiteral("repeat")}, {QStringLiteral("rate_division"), QStringLiteral("1/8")}});
		QVERIFY2(repeat.ok, qPrintable(repeat.errorMessage));
		QCOMPARE(repeat.result.value(QStringLiteral("stretch")).toString(), QStringLiteral("repeat"));
		QCOMPARE(repeat.result.value(QStringLiteral("stretch_algorithm")).toString(), QStringLiteral("transients"));
		QCOMPARE(repeat.result.value(QStringLiteral("previous_repeat_rate")).toString(), QStringLiteral("1/16"));
		QCOMPARE(repeat.result.value(QStringLiteral("transient_count")).toInt(), kBursts - 1);
		const QJsonObject rate = repeat.result.value(QStringLiteral("repeat_rate")).toObject();
		QCOMPARE(rate.value(QStringLiteral("name")).toString(), QStringLiteral("1/8"));
		QCOMPARE(rate.value(QStringLiteral("synced")).toBool(), true);
		QVERIFY(std::fabs(rate.value(QStringLiteral("interval_ms")).toDouble() - 250.0) < 0.01);

		// A rate change alone, the mode kept: Gate is set with it in a second call.
		QVERIFY(run(QStringLiteral("warp.stretch"), {{QStringLiteral("clip"), id},
			{QStringLiteral("mode"), QStringLiteral("repeat")}, {QStringLiteral("rate_ms"), 125.5}}).ok);
		QVERIFY(run(QStringLiteral("warp.stretch"), {{QStringLiteral("clip"), id},
			{QStringLiteral("mode"), QStringLiteral("gate")}}).ok);
		const ControlResult listed = run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), id}});
		QVERIFY(listed.ok);
		QCOMPARE(listed.result.value(QStringLiteral("stretch")).toString(), QStringLiteral("gate"));
		const QJsonObject listedRate = listed.result.value(QStringLiteral("repeat_rate")).toObject();
		QCOMPARE(listedRate.value(QStringLiteral("name")).toString(), QStringLiteral("125.5ms"));
		QCOMPARE(listedRate.value(QStringLiteral("synced")).toBool(), false);
		QVERIFY(std::fabs(listedRate.value(QStringLiteral("interval_ms")).toDouble() - 125.5) < 0.01);

		// One undo per call: gate -> repeat (rate kept), then the rate, then the first switch.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Repeat);
		QCOMPARE(clip->transientRepeatRate(), TransientRepeatRate::freeInterval(125.5));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Repeat);
		QCOMPARE(clip->transientRepeatRate(), TransientRepeatRate::syncedTo(8));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::Resample);
		QCOMPARE(clip->transientRepeatRate(), TransientRepeatRate{});

		// The reported interval of a synced rate follows the tempo (the default
		// 1/16: 125 ms at 120 BPM, 250 ms at 60). The tempo change is a journal
		// step of its own, so it comes after the undo sequence above.
		const auto intervalMs = [&id] {
			return run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), id}}).result
				.value(QStringLiteral("repeat_rate")).toObject().value(QStringLiteral("interval_ms")).toDouble();
		};
		QVERIFY(std::fabs(intervalMs() - 125.0) < 0.01);
		Engine::getSong()->setTempo(60);
		QVERIFY(std::fabs(intervalMs() - 250.0) < 0.01);
		Engine::getSong()->setTempo(120);
	}

	//! ACCEPTANCE 2 (save/load + rendered-audio regression): a Repeat clip at a
	//! synced rate and a Gate clip at a free one survive a real project
	//! save/load - modes, rates and the file's own attributes - and render bit
	//! for bit what they rendered before the save.
	void modeAndRateRoundTripAndRenderTheSameAfterALoad()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Song* song = Engine::getSong();
		song->setLoadOnLaunch(false);
		song->clearProject();
		song->setTempo(120);
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(track != nullptr);
		auto* repeat = makeTransientClip(*track, m_rate);
		applyHalfSpeedWarp(*repeat, m_rate);
		repeat->setTransientRepeatRate(TransientRepeatRate::syncedTo(8));
		repeat->setWarpStretchMode(WarpStretchMode::Repeat);
		auto* gate = makeTransientClip(*track, m_rate);
		applyHalfSpeedWarp(*gate, m_rate);
		gate->setTransientRepeatRate(TransientRepeatRate::freeInterval(300.0));
		gate->setWarpStretchMode(WarpStretchMode::Gate);
		// A clip that never used the modes: no <warp> element at all, as ever.
		makeTransientClip(*track, m_rate);
		const std::vector<SampleFrame> repeatBefore = renderClip(repeat);
		const std::vector<SampleFrame> gateBefore = renderClip(gate);

		const QString path = dir.filePath(QStringLiteral("transient-repeat.mmp"));
		QVERIFY2(song->saveProjectFile(path), qPrintable(song->saveRefusal()));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(&file));
		file.close();
		const QDomNodeList clips = document.elementsByTagName(QStringLiteral("sampleclip"));
		QCOMPARE(clips.count(), 3);
		const QDomElement repeatWarp = clips.at(0).toElement().firstChildElement(QStringLiteral("warp"));
		const QDomElement gateWarp = clips.at(1).toElement().firstChildElement(QStringLiteral("warp"));
		QCOMPARE(repeatWarp.attribute(QStringLiteral("stretch")), QStringLiteral("repeat"));
		QCOMPARE(repeatWarp.attribute(QStringLiteral("repeat_rate")), QStringLiteral("1/8"));
		QCOMPARE(gateWarp.attribute(QStringLiteral("stretch")), QStringLiteral("gate"));
		QCOMPARE(gateWarp.attribute(QStringLiteral("repeat_rate")), QStringLiteral("300ms"));
		QVERIFY(clips.at(2).toElement().firstChildElement(QStringLiteral("warp")).isNull());

		song->clearProject();
		song->loadProject(path);
		std::vector<SampleClip*> loaded;
		for (Track* t : song->tracks())
		{
			for (Clip* c : t->getClips())
			{
				if (auto* s = dynamic_cast<SampleClip*>(c)) { loaded.push_back(s); }
			}
		}
		QCOMPARE(static_cast<int>(loaded.size()), 3);
		QCOMPARE(loaded[0]->warpStretchMode(), WarpStretchMode::Repeat);
		QCOMPARE(loaded[0]->transientRepeatRate(), TransientRepeatRate::syncedTo(8));
		QCOMPARE(loaded[1]->warpStretchMode(), WarpStretchMode::Gate);
		QCOMPARE(loaded[1]->transientRepeatRate(), TransientRepeatRate::freeInterval(300.0));
		QCOMPARE(loaded[2]->warpStretchMode(), WarpStretchMode::Resample);
		QCOMPARE(loaded[2]->transientRepeatRate(), TransientRepeatRate{});

		const std::vector<SampleFrame> repeatAfter = renderClip(loaded[0]);
		const std::vector<SampleFrame> gateAfter = renderClip(loaded[1]);
		QCOMPARE(repeatAfter.size(), repeatBefore.size());
		QCOMPARE(gateAfter.size(), gateBefore.size());
		for (std::size_t f = 0; f < repeatBefore.size(); ++f)
		{
			QVERIFY2(repeatAfter[f][0] == repeatBefore[f][0] && gateAfter[f][0] == gateBefore[f][0],
				qPrintable(QStringLiteral("the reloaded render differs at frame %1").arg(f)));
		}
		song->clearProject();
	}

	//! Older files: a <warp> without repeat_rate loads the default rate, and the
	//! older mode names still select their modes.
	void anOlderWarpElementLoadsTheDefaultRate()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeTransientClip(track, m_rate);
		clip->setTransientRepeatRate(TransientRepeatRate::freeInterval(80.0));
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("sampleclip"));
		QDomElement warp = doc.createElement(QStringLiteral("warp"));
		warp.setAttribute(QStringLiteral("mode"), QStringLiteral("follow"));
		warp.setAttribute(QStringLiteral("stretch"), QStringLiteral("wsola"));
		element.appendChild(warp);
		clip->restoreState(element);
		QCOMPARE(clip->warpStretchMode(), WarpStretchMode::PreservePitch);
		QCOMPARE(clip->transientRepeatRate(), TransientRepeatRate{});
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipTransientRepeatTest)
#include "SampleClipTransientRepeatTest.moc"
