/*
 * SampleClipMuteCursorTest.cpp - cluster new-p12-223: muting a track must not
 *                                 leave the sample cursor behind.
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

/* The defect (Luna's patrol, cluster new-p12-223): `SamplePlayHandle::play()`
 * skips its whole render while the owning track is muted, but still advances
 * `m_frame`, the handle's own timeline position. The playback state - the source
 * cursor - therefore stays on the frame the mute began on. Unmuting inside the
 * clip replays audio that has already sounded and the tail is cut short.
 *
 * The fix keeps the source in step while the track is muted, rendering its reads
 * into a scratch that belongs to the handle. This file proves it on the three
 * paths that consume the source - the resampler, the pitch-preserving stretcher,
 * and the realtime rule that the muted pass allocates nothing.
 *
 * It is its own executable rather than a slot in SampleClipWindowTest.cpp
 * because Gate 7 (tests/file-length-gate.sh) measures CODE lines and that file
 * sits at 494 of its 500-line limit; this cluster's slots do not fit there.
 */

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <array>
#include <memory>
#include <span>
#include <vector>

#include "../core/AllocationProbe.h"

#include "Engine.h"
#include "AudioEngine.h"
#include "LmmsTypes.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
//! Source length: long enough that the clip is never the limiting factor.
constexpr int kSourceSeconds = 8;
//! Harness render period; the audio device's own is not needed by these assertions.
constexpr f_cnt_t kPeriod = 1024;

//! The window these tests play: the second of source audio, [1 s, 2 s).
f_cnt_t trimIn(int rate) { return static_cast<f_cnt_t>(rate); }
f_cnt_t trimOut(int rate) { return static_cast<f_cnt_t>(2 * rate); }

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

//! A deterministic 440 Hz tone, so a rendered frame identifies its source frame.
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

//! A level that says WHERE in the source it sits: blocks of constant value
//! rising along a slow ramp. A render read from the wrong frame shows it, which
//! a tone cannot (any position of a tone sounds like the same tone).
std::vector<SampleFrame> makeRamp(int rate, f_cnt_t block)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	std::vector<SampleFrame> data(frames, SampleFrame(0.0f, 0.0f));
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const auto blocks = static_cast<float>(f / block) / 24.0f;  // 0.1 .. 0.9 over 24 blocks
		const auto level = static_cast<sample_t>(0.1f + 0.8f * std::min(1.0f, blocks));
		data[f] = SampleFrame(level, level);
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

//! A clip whose source is the position ramp.
SampleClip* makeRampClip(SampleTrack& track, int rate, f_cnt_t block)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeRamp(rate, block);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

/*! Pins the clip's whole source to HALF the ticks the project's rate gives it:
 *  a 2x warp, authored as the two markers the warp engine's own tests use
 *  (tests/src/tracks/SampleClipStretchTest.cpp:112-122). It is what makes a
 *  clip render non-linearly, which is the precondition for the
 *  pitch-preserving render (SamplePlayHandle::m_preservePitch). */
void applyTwoTimesWarp(SampleClip& clip, int rate)
{
	const auto frames = static_cast<f_cnt_t>(clip.sample().sampleSize());
	const auto ticks = static_cast<tick_t>(std::llround(frames / (2.0 * Engine::framesPerTick(rate))));
	const std::array<WarpMarker, 2> markers{ WarpMarker{ 0, 0 }, WarpMarker{ frames, ticks } };
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

} // namespace


class SampleClipMuteCursorTest : public QObject
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

	/*! cluster new-p12-223: muting a track must not leave the sample cursor behind.
	 *
	 *  While the track is muted `play()` renders nothing
	 *  (src/core/SamplePlayHandle.cpp:573) and, before the fix, advanced nothing
	 *  either: `m_state` stayed on the source frame the mute began on while
	 *  `m_frame` - the handle's own timeline position - moved on
	 *  (src/core/SamplePlayHandle.cpp:637). Unmuting inside the clip then replayed
	 *  audio that had already sounded and cut the tail short.
	 *
	 *  Two handles read the same clip: the reference ignores the track mute, the
	 *  other spends the middle of the clip muted. The first audible period after
	 *  the mute must render the SAME source frames - exactly, because a source at
	 *  the engine's own rate is read 1:1 (src/core/Sample.cpp:132).
	 */
	void mutingATrackKeepsTheSampleCursorMoving()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));

		SamplePlayHandle reference(clip);
		reference.setIgnoresTrackMute(true);  // the unmuted reading of the same clip
		SamplePlayHandle muted(clip);

		std::vector<SampleFrame> referenceBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		std::vector<SampleFrame> handleBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		const auto referenceSpan = std::span<SampleFrame>(referenceBuffer);
		const auto handleSpan = std::span<SampleFrame>(handleBuffer);

		for (int period = 0; period < 4; ++period)
		{
			reference.play(referenceSpan);
			muted.play(handleSpan);  // both audible here: the track is not muted yet
		}
		QCOMPARE(asNumber(muted.framesDone()), asNumber(reference.framesDone()));

		track.setMuted(true);
		for (int period = 0; period < 10; ++period)
		{
			reference.play(referenceSpan);
			// as PlayHandle::doProcessing hands it over: zeroed, then played
			std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
			muted.play(handleSpan);
			for (const SampleFrame& frame : handleBuffer)
			{
				// a muted pass renders nothing at all into the caller's buffer
				QCOMPARE(frame[0], 0.0f);
				QCOMPARE(frame[1], 0.0f);
			}
		}
		// the timeline moved on for both of them
		QCOMPARE(asNumber(muted.framesDone()), asNumber(reference.framesDone()));

		track.setMuted(false);
		std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
		reference.play(referenceSpan);
		muted.play(handleSpan);
		for (f_cnt_t f = 0; f < kPeriod; ++f)
		{
			QVERIFY2(std::fabs(handleBuffer[f][0] - referenceBuffer[f][0]) < 1e-6f,
				qPrintable(QStringLiteral("frame %1 after the mute is %2, the unmuted reading of the "
					"same clip is %3 - the muted pass left the source cursor behind")
					.arg(static_cast<qulonglong>(f)).arg(handleBuffer[f][0]).arg(referenceBuffer[f][0])));
		}

		/* Exhaustion: a mute that runs past the clip's own window must not leave a
		 * cursor with audio behind it - the next audible period is silence, not the
		 * audio the window has already passed. */
		track.setMuted(true);
		const auto total = static_cast<f_cnt_t>(muted.totalFrames());
		while (muted.framesDone() < total)
		{
			muted.play(handleSpan);
		}
		track.setMuted(false);
		std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(7.0f, 7.0f));
		muted.play(handleSpan);
		for (const SampleFrame& frame : handleBuffer)
		{
			QCOMPARE(frame[0], 0.0f);
			QCOMPARE(frame[1], 0.0f);
		}
	}

	/*! The same cursor, in the mode that does not read the source through
	 *  `Sample::play`: a pitch-preserving clip is driven by its stretcher's
	 *  analysis cursor (src/core/SamplePlayHandle.cpp:748-751), so a muted pass
	 *  that renders nothing also leaves THAT cursor behind.
	 *
	 *  The source is the position ramp, read through a 2x warp, so a muted span
	 *  covers source blocks with different levels; the stretcher smears a little
	 *  at a block's edge, so the comparison is over the middle half of the period
	 *  and with a tolerance far below the level difference a stale cursor makes.
	 */
	void mutingATrackKeepsTheStretchedCursorMoving()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeRampClip(track, rate, 4096);
		applyTwoTimesWarp(*clip, rate);
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);

		SamplePlayHandle reference(clip);
		reference.setIgnoresTrackMute(true);
		SamplePlayHandle muted(clip);

		std::vector<SampleFrame> referenceBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		std::vector<SampleFrame> handleBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		const auto referenceSpan = std::span<SampleFrame>(referenceBuffer);
		const auto handleSpan = std::span<SampleFrame>(handleBuffer);

		for (int period = 0; period < 4; ++period)
		{
			reference.play(referenceSpan);
			muted.play(handleSpan);
		}

		track.setMuted(true);
		for (int period = 0; period < 12; ++period)
		{
			reference.play(referenceSpan);
			std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
			muted.play(handleSpan);
			for (const SampleFrame& frame : handleBuffer) { QCOMPARE(frame[0], 0.0f); }
		}
		QCOMPARE(asNumber(muted.framesDone()), asNumber(reference.framesDone()));

		track.setMuted(false);
		std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
		reference.play(referenceSpan);
		muted.play(handleSpan);

		double meanDifference = 0.0;
		double worstDifference = 0.0;
		const f_cnt_t from = kPeriod / 4;
		const f_cnt_t to = kPeriod - kPeriod / 4;
		for (f_cnt_t f = from; f < to; ++f)
		{
			const double difference = std::fabs(static_cast<double>(handleBuffer[f][0]) - referenceBuffer[f][0]);
			meanDifference += difference;
			worstDifference = std::max(worstDifference, difference);
		}
		meanDifference /= static_cast<double>(to - from);
		QVERIFY2(meanDifference < 0.05 && worstDifference < 0.15,
			qPrintable(QStringLiteral("the stretched read after the mute averaged %1 off the unmuted "
				"reading of the same clip (worst %2): the muted pass left the analysis cursor behind")
				.arg(meanDifference).arg(worstDifference)));
	}

	/*! I8 on the muted path: advancing the source silently while the track is
	 *  muted allocates nothing - the scratch the discarded audio goes into is a
	 *  member of the handle (probe pattern from tests/src/core/AllocationProbe.h).
	 *  The cursor must still move, so this measures the same catch-up as
	 *  `mutingATrackKeepsTheSampleCursorMoving` under the allocation probe.
	 */
	void aMutedPassAdvancesWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeRampClip(track, rate, 4096);
		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));

		SamplePlayHandle reference(clip);
		reference.setIgnoresTrackMute(true);
		SamplePlayHandle muted(clip);

		std::vector<SampleFrame> referenceBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		std::vector<SampleFrame> handleBuffer(kPeriod, SampleFrame(0.0f, 0.0f));
		const auto referenceSpan = std::span<SampleFrame>(referenceBuffer);
		const auto handleSpan = std::span<SampleFrame>(handleBuffer);

		for (int period = 0; period < 4; ++period)
		{
			reference.play(referenceSpan);
			muted.play(handleSpan);
		}

		track.setMuted(true);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 24; ++period)
		{
			reference.play(referenceSpan);
			std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
			muted.play(handleSpan);
		}
		test::tlCountAllocations = false;
		QCOMPARE(asNumber(test::tlAllocationCount), asNumber(0));

		track.setMuted(false);
		std::fill(handleBuffer.begin(), handleBuffer.end(), SampleFrame(0.0f, 0.0f));
		reference.play(referenceSpan);
		muted.play(handleSpan);
		for (f_cnt_t f = 0; f < kPeriod; ++f)
		{
			QVERIFY2(std::fabs(handleBuffer[f][0] - referenceBuffer[f][0]) < 1e-6f,
				qPrintable(QStringLiteral("frame %1 after the muted allocation pass is %2, the unmuted "
					"reading is %3").arg(static_cast<qulonglong>(f)).arg(handleBuffer[f][0])
					.arg(referenceBuffer[f][0])));
		}
	}
};

QTEST_GUILESS_MAIN(SampleClipMuteCursorTest)
#include "SampleClipMuteCursorTest.moc"
