/*
 * EngineRenderBenchmarkTest.cpp - R7.4: the engine's render cost as a tracked number
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

/*! R7.4: a fixed session's render time, so a change that makes the audio path dearer - R1's
 *  per-sample automation above all - shows up as a number rather than as a user's dropout.
 *
 *  The session: sixteen sample tracks, each playing a four-second noise clip, into the
 *  master, over a two-bar loop inside the clips - so every period measured has all sixteen
 *  playing. The test asserts the output is not silent and that the automation audibly acts:
 *  a benchmark of silence would pass and measure nothing (the first draft of this one did). Two measurements over 2000 engine periods each, with the Dummy device's own
 *  render thread stopped so this test is the only renderer:
 *    * "plain": the tracks alone;
 *    * "automated": the same, with the master fader automated by a Linear clip - which,
 *      since R1.2, is sample-accurate by default, so its per-sample ramp is in the cost.
 *  Each reports microseconds per period and the realtime factor (period duration / cost).
 *  Sanity bound always: a period under 50 ms. The +20% gate is opt-in and same-machine
 *  (BenchmarkBaseline.h), for the reason every timing here has one.
 */

#include <QtTest>

#include <QElapsedTimer>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <random>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "BenchmarkBaseline.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int kTracks = 16;
constexpr int kPeriods = 2000;

//! The loudest sample of the next few periods: a benchmark of silence measures nothing. The
//! first two are skipped - the engine's output is double-buffered, so the period handed back
//! right after a change was rendered before it (the output-period term R2.2 measured).
float periodPeak()
{
	float peak = 0.0f;
	for (int period = 0; period < 4; ++period)
	{
		const std::span<const SampleFrame> frames = Engine::audioEngine()->renderNextPeriod();
		if (period < 2) { continue; }
		for (const SampleFrame& frame : frames)
		{
			peak = std::max({peak, std::abs(frame.left()), std::abs(frame.right())});
		}
	}
	return peak;
}

double microsecondsPerPeriod()
{
	AudioEngine* engine = Engine::audioEngine();
	for (int i = 0; i < 50; ++i) { engine->renderNextPeriod(); }  // warm-up
	QElapsedTimer timer;
	timer.start();
	for (int i = 0; i < kPeriods; ++i) { engine->renderNextPeriod(); }
	return static_cast<double>(timer.nsecsElapsed()) / 1000.0 / kPeriods;
}

} // namespace

class EngineRenderBenchmarkTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void renderCostPlainAndAutomated()
	{
		Song* song = Engine::getSong();
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::mt19937 noise(7);
		std::uniform_real_distribution<float> sample(-0.2f, 0.2f);
		for (int t = 0; t < kTracks; ++t)
		{
			auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
			QVERIFY(track != nullptr);
			std::vector<SampleFrame> data(static_cast<std::size_t>(4 * rate));
			for (auto& frame : data) { frame = SampleFrame(sample(noise), sample(noise)); }
			auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
			clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
		}
		// The run is ~12 s of audio and each clip 4 s: loop the first two bars, so every period
		// measured is one in which all sixteen clips play.
		const TimePos loopEnd(2 * DefaultTicksPerBar);
		song->getTimeline(Song::PlayMode::Song).setLoopPoints(TimePos(0), loopEnd);
		song->getTimeline(Song::PlayMode::Song).setLoopEnabled(true);
		song->playSong();
		QMap<QString, double> measured;
		const double periodUs = 1.0e6 * Engine::audioEngine()->framesPerPeriod() / rate;

		const double plain = microsecondsPerPeriod();
		measured.insert(QStringLiteral("plain"), plain);
		const float plainPeak = periodPeak();
		QVERIFY2(plainPeak > 0.05f, qPrintable(QStringLiteral("the session is silent (peak %1)").arg(plainPeak)));

		auto* automation = Track::create(Track::Type::Automation, song);
		auto* curve = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0)));
		QVERIFY(curve != nullptr);
		QVERIFY(curve->addObject(&Engine::mixer()->mixerChannel(0)->m_volumeModel));
		curve->setProgressionType(AutomationClip::ProgressionType::Linear);
		curve->putValue(TimePos(0), 0.2f, false);
		curve->putValue(loopEnd, 1.0f, false);
		QVERIFY2(curve->sampleAccurate(), "a Linear clip is not sample-accurate by default (R1.2)");
		song->stop();
		song->playSong();
		// The fader starts at 0.2: the automated session must be audibly quieter at its start.
		const float automatedPeak = periodPeak();
		QVERIFY2(automatedPeak < 0.5f * plainPeak, qPrintable(QStringLiteral("the automated fader did not "
			"act (peak %1 against %2)").arg(automatedPeak).arg(plainPeak)));
		const double automated = microsecondsPerPeriod();
		measured.insert(QStringLiteral("automated"), automated);
		song->stop();

		std::printf("ENGINE_BENCH %d sample tracks, %d-frame periods: plain %.1f us/period (x%.1f realtime), "
			"automated %.1f us/period (x%.1f realtime)\n", kTracks, static_cast<int>(Engine::audioEngine()->framesPerPeriod()),
			plain, periodUs / plain, automated, periodUs / automated);
		QVERIFY2(plain < 50000.0 && automated < 50000.0, "a period took longer than 50 ms - a hang, not a slowdown");
		benchtest::compareOrWriteBaseline(measured);
	}
};

QTEST_GUILESS_MAIN(EngineRenderBenchmarkTest)
#include "EngineRenderBenchmarkTest.moc"
