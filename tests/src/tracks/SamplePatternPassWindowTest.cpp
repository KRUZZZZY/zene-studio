/*
 * SamplePatternPassWindowTest.cpp - cluster 3503: a pattern-store pass past the
 *                                   end of a clip's window schedules nothing.
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

// Cluster 3503: `SampleTrack::play`'s pattern-store branch scheduled a play
// handle with an inverted window { sampleStart, sourceOut } when the seek landed
// at or past the window end, latching the clip and the track as playing. The
// arrangement branch already refuses an empty window; the pattern branch must too.
//
// It is its own executable rather than a slot in SampleClipWindowTest.cpp
// because that file would exceed the 500-line file-length gate with this slot.

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "Engine.h"
#include "AudioEngine.h"
#include "LmmsTypes.h"
#include "PatternStore.h"
#include "PlayHandle.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
//! Source length: long enough that the clip is never the limiting factor.
constexpr int kSourceSeconds = 8;
//! Harness render period; the audio device's own is not needed by these assertions.
constexpr f_cnt_t kPeriod = 1024;
//! The window used by the test: the second of source audio, [1 s, 2 s).
f_cnt_t trimIn(int rate) { return static_cast<f_cnt_t>(rate); }
f_cnt_t trimOut(int rate) { return static_cast<f_cnt_t>(2 * rate); }

//! A deterministic 440 Hz tone.
std::vector<SampleFrame> makeTone(int rate)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	const double period = rate / 440.0;
	std::vector<SampleFrame> data(frames);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const auto value = static_cast<sample_t>(0.5 * std::sin(2.0 * kPi * f / period));
		data[f] = SampleFrame(value, value);
	}
	return data;
}

//! A sample track holding one clip whose source is the deterministic tone.
SampleClip* makeToneClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeTone(rate);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

/*! Removes the play handles a test's playback passes left with the audio engine.
 *
 *  AudioEngine::addPlayHandle() queues a handle in its lock-free new-handle list
 *  and only the audio thread moves it into the list
 *  AudioEngine::removePlayHandlesOfTypes() walks, so a handle removed in the same
 *  instant it was added is not found and outlives its track. Give the audio
 *  thread a period to adopt them, then remove them while the track is alive -
 *  otherwise the audio thread renders a handle whose track is gone.
 */
void drainPlayHandles(SampleTrack& track)
{
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		QTest::qWait(30);
		Engine::audioEngine()->removePlayHandlesOfTypes(&track, PlayHandle::Type::SamplePlayHandle);
	}
}

} // namespace


class SamplePatternPassWindowTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! Cluster 3503: a pattern-store pass whose seek lands at or past the end of the
	//! clip's window has nothing to play. It must schedule no handle, report false, and
	//! not latch the clip or the track as playing (the arrangement branch already refuses
	//! an empty window; the pattern branch scheduled { sampleStart, sourceOut } anyway).
	void patternPassPastTheWindowSchedulesNothing()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::patternStore());
		QVERIFY(track.trackContainer() == Engine::patternStore());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));
		const f_cnt_t sourceOut = clip->sampleWindow().sourceOut;

		// control: a seek inside the window still schedules and latches
		QVERIFY(track.play(TimePos(0), kPeriod, 0, 0));
		QVERIFY(clip->isPlaying());
		QVERIFY(track.isPlaying());
		clip->setIsPlaying(false);
		track.setPlaying(false);
		drainPlayHandles(track);

		// the first tick whose start frame, computed as SampleTrack::play computes it, is at the window end
		const float framesPerTick = Engine::framesPerTick(clip->sample().sampleRate());
		const auto startFrameAt = [&](int ticks) -> f_cnt_t
		{
			const f_cnt_t sampleStart = framesPerTick * TimePos(ticks);
			return sampleStart;
		};
		int boundary = 0;
		while (boundary < 1000000 && startFrameAt(boundary) < sourceOut) { ++boundary; }
		QVERIFY(startFrameAt(boundary) >= sourceOut);

		// the bug, at the exact boundary
		QVERIFY(!track.play(TimePos(boundary), kPeriod, 0, 0));
		QVERIFY(!clip->isPlaying());
		QVERIFY(!track.isPlaying());
		clip->setIsPlaying(false);
		track.setPlaying(false);
		drainPlayHandles(track);

		// ... and well past it
		QVERIFY(!track.play(TimePos(boundary + 100), kPeriod, 0, 0));
		QVERIFY(!clip->isPlaying());
		QVERIFY(!track.isPlaying());
		clip->setIsPlaying(false);
		track.setPlaying(false);
		drainPlayHandles(track);
	}
};

QTEST_GUILESS_MAIN(SamplePatternPassWindowTest)
#include "SamplePatternPassWindowTest.moc"
