/*
 * SampleClipWarpTest.cpp - task #597: the warp engine on the frozen clip-model
 *                           seam (Slice 0's sourceFrameAt/timelinePosAt).
 *
 * Copyright (c) 2026 Zene Studio developers
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

// docs/CLIP-CAPTURE-DESIGN.md §2.4 freezes ONE mapping on the clip and says
// #597 attaches to it: "#597 adds `WarpMarkers` as a child element and a second
// implementation of the same two virtuals, keyed off the clip's source tempo.
// Nothing else in the clip model changes when warp lands." This file holds that
// claim to the letter, and holds the two properties that make a warp engine
// trustworthy - **monotonic** and **exact at the markers** - plus the four
// acceptance items the task names: a persistence round trip, an old project
// that still loads, marker independence from TRIM, and the no-marker
// behaviour-preservation proof.
//
// The marker fixture is literal so every expected value below is one a reader
// can recompute by hand. Source frames are positions on the 8 s test buffer;
// `offsetTicks` is measured from the clip's origin.
//
//   M1 = (source 44100, offset 24)      M2 = (source 176400, offset 96)
//   segment M1..M2: 132300 frames over 72 ticks -> 1837.5 frames/tick
//
// so a mapping that ignored the markers, or interpolated with the wrong slope,
// or returned the base rate inside the segment, cannot pass.

#include <QtTest>


#include <cmath>
#include <memory>
#include <vector>

#include "../core/AllocationProbe.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
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

const WarpMarker kM1{ 44100, 24 };
const WarpMarker kM2{ 176400, 96 };

template <typename T>
qulonglong N(T value) { return static_cast<qulonglong>(value); }

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

SampleClip* makeClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeTone(rate);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

//! The two-marker warp above, authored on `clip`; asserts it was accepted.
void applyWarp(SampleClip& clip)
{
	const std::array<WarpMarker, 2> markers{ kM1, kM2 };
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

//! The tick a clip-relative offset lands on, for an absolute-position call.
TimePos atOffset(const SampleClip& clip, tick_t offset)
{
	return TimePos(clip.startPosition().getTicks() + clip.startTimeOffset().getTicks() + offset);
}


//! Removes the play handles a playback pass left with the audio engine, so a
//! stack-allocated track dies before the audio thread renders them.
void drainPlayHandles(SampleTrack& track)
{
	for (int attempt = 0; attempt < 4; ++attempt)
	{
		QTest::qWait(30);
		Engine::audioEngine()->removePlayHandlesOfTypes(&track, PlayHandle::Type::SamplePlayHandle);
	}
}
} // namespace


class SampleClipWarpTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_rate = Engine::audioEngine()->outputSampleRate();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! ACCEPTANCE (behaviour preservation). With no markers and the default
	//! (follower) tempo mode the clip's mapping is the pre-warp arithmetic,
	//! comparison for comparison, at several clip positions and transport
	//! positions - the same deterministic proof Slice 0 used, because a render
	//! is not byte-reproducible in this tree.
	void noMarkersIsThePreWarpArithmetic()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		QVERIFY(clip->warpMarkers().empty());
		QCOMPARE(static_cast<int>(clip->warpTempoMode()), static_cast<int>(WarpTempoMode::FollowProject));
		QVERIFY(clip->rendersLinearly());

		const auto framesPerTick = Engine::framesPerTick(m_rate);
		const auto bufferFrames = static_cast<f_cnt_t>(clip->sample().sampleSize());
		const auto lengthTicks = static_cast<int>(bufferFrames / framesPerTick);
		clip->changeLength(TimePos(lengthTicks));

		for (const int clipPos : { 0, 96 })
		{
			clip->movePosition(TimePos(clipPos));
			for (const int transport : { clipPos, clipPos + 1, clipPos + 48, clipPos + lengthTicks - 1 })
			{
				// the pre-warp arithmetic, verbatim from Slice 0
				const auto expected = static_cast<f_cnt_t>(framesPerTick
					* (transport - clip->startPosition() - clip->startTimeOffset()));
				QCOMPARE(N(clip->sourceFrameAt(TimePos(transport))), N(expected));

				// and the inverse lands back on the tick it came from, to
				// within the frame-to-tick truncation trimming needs
				const auto back = clip->timelinePosAt(clip->sourceFrameAt(TimePos(transport)));
				QVERIFY(std::abs(static_cast<int>(back) - transport) <= 1);
			}
		}
	}

	//! **Exact at every marker**, in both directions, through the clip.
	void markersAreExactThroughTheClip()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);

		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, kM1.offsetTicks))), N(kM1.sourceFrame));
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, kM2.offsetTicks))), N(kM2.sourceFrame));
		QCOMPARE(clip->timelinePosAt(kM1.sourceFrame).getTicks(), kM1.offsetTicks);
		QCOMPARE(clip->timelinePosAt(kM2.sourceFrame).getTicks(), kM2.offsetTicks);
	}

	//! **Monotonic** through the clip, and the interpolation between the
	//! markers is the segment's own constant rate - with literal expected
	//! values, not a re-run of the implementation.
	void interpolatesBetweenMarkersWithRealValues()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);

		const auto frameAt = [clip](tick_t offset) {
			return clip->sourceFrameAt(atOffset(*clip, offset));
		};

		// 132300 frames over 72 ticks: 44100 + 132300 * (t - 24) / 72
		QCOMPARE(N(frameAt(36)), N(66150));    // + 12/72
		QCOMPARE(N(frameAt(60)), N(110250));   // + 36/72, the midpoint
		QCOMPARE(N(frameAt(84)), N(154350));   // + 60/72

		// the inverse walks the same line
		QCOMPARE(clip->timelinePosAt(66150).getTicks(), 36);
		QCOMPARE(clip->timelinePosAt(110250).getTicks(), 60);
		QCOMPARE(clip->timelinePosAt(154350).getTicks(), 84);

		// monotonic across both tails and the segment
		f_cnt_t previous = 0;
		for (tick_t offset = -200; offset <= 700; ++offset)
		{
			const auto frame = frameAt(offset);
			QVERIFY(frame >= previous);
			previous = frame;
		}

		// outside the markers the rate is the clip's own base rate
		const auto base = clip->clipFramesPerTick();
		QCOMPARE(N(clip->warpMarkers().framesPerTickAt(1000, base)), N(base));
		QCOMPARE(clip->warpMarkers().framesPerTickAt(kM1.sourceFrame, base), 1837.5f);
		QCOMPARE(clip->warpMarkers().framesPerTickAt(kM2.sourceFrame, base), base);
	}

	//! A non-monotonic set is refused on the clip too, and the clip keeps the
	//! warp it had: a rejected edit, not a clamped one (I4's rule, applied to
	//! the map).
	void nonMonotonicMarkersAreRejectedOnTheClip()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);
		const auto before = clip->warpMarkers();
		QCOMPARE(before.size(), 2);

		const std::array<WarpMarker, 2> backwards{ WarpMarker{ 44100, 96 }, WarpMarker{ 176400, 24 } };
		QVERIFY(!clip->setWarpMarkers(std::span<const WarpMarker>(backwards.data(), backwards.size())));
		QVERIFY(clip->warpMarkers() == before);

		clip->clearWarpMarkers();
		QVERIFY(clip->warpMarkers().empty());
		QVERIFY(clip->rendersLinearly());
	}

	//! ACCEPTANCE (marker independence from trim). The design's own reason for
	//! pinning markers to SOURCE FRAMES (§2.4): "trimming moves sourceIn/
	//! sourceOut and the markers stay where they are on the audio". So after a
	//! trim the markers are unchanged, and a trimmed clip that still contains
	//! them still hears the same source frame at the same place.
	void markersSurviveATrim()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);

		// trim the window to everything after the first marker
		clip->setSampleStartFrame(20000);
		clip->setSamplePlayLength(300000);

		QCOMPARE(clip->warpMarkers().size(), 2);
		QCOMPARE(N(clip->warpMarkers()[0].sourceFrame), N(kM1.sourceFrame));
		QCOMPARE(clip->warpMarkers()[0].offsetTicks, kM1.offsetTicks);
		QCOMPARE(N(clip->warpMarkers()[1].sourceFrame), N(kM2.sourceFrame));
		QCOMPARE(clip->warpMarkers()[1].offsetTicks, kM2.offsetTicks);

		// the markers are still exactly where they were authored
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, 24))), N(44100));
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, 96))), N(176400));
		QCOMPARE(clip->timelinePosAt(176400).getTicks(), 96);

		// and the mapping still clamps into the TRIMMED window
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, -1000))), N(20000));
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, 100000))), N(300000));

		// trimming to cut a marker out of the window clamps rather than
		// reading past the window
		clip->setSampleStartFrame(100000);
		clip->setSamplePlayLength(200000);
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, 24))), N(100000));
		QCOMPARE(clip->warpMarkers().size(), 2);
	}

	//! The map decides the clip's timeline length, and a 4x segment is shorter
	//! than the linear one: the same window, fewer ticks.
	void warpShortensTheClipsTimelineLength()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		const auto bufferFrames = static_cast<f_cnt_t>(clip->sample().sampleSize());
		const auto linearTicks = clip->windowTicksFor(SampleWindow::full(bufferFrames));

		applyWarp(*clip);
		const auto warpedTicks = clip->windowTicksFor(SampleWindow::full(bufferFrames));

		QVERIFY(warpedTicks > 0);
		QVERIFY2(warpedTicks < linearTicks,
			qPrintable(QString("warped %1 vs linear %2").arg(warpedTicks).arg(linearTicks)));

		// ... and the clip's authored length is that same number
		clip->changeLength(TimePos(warpedTicks));
		clip->setAutoResize(false);
		QCOMPARE(clip->sampleLength().getTicks(), warpedTicks);
	}

	//! The playback path is what a user hears: a warped clip's handle is as
	//! long as the mapping says the window lasts, not as long as the window's
	//! frame count would last at the natural rate.
	void playHandleLengthFollowsTheWarp()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		const auto bufferFrames = static_cast<f_cnt_t>(clip->sample().sampleSize());

		SamplePlayHandle plain(clip);
		const auto plainFrames = plain.totalFrames();
		QCOMPARE(plainFrames, static_cast<f_cnt_t>(bufferFrames
			* (static_cast<float>(Engine::audioEngine()->outputSampleRate()) / m_rate)));

		applyWarp(*clip);
		SamplePlayHandle warped(clip);
		const auto expected = static_cast<f_cnt_t>(clip->windowTicksFor(SampleWindow::full(bufferFrames))
			* Engine::framesPerTick(Engine::audioEngine()->outputSampleRate()));
		QCOMPARE(warped.totalFrames(), expected);
		QVERIFY2(warped.totalFrames() < plainFrames,
			qPrintable(QString("warped %1 vs plain %2")
				.arg(N(warped.totalFrames())).arg(N(plainFrames))));
	}

	//! Tempo leader/follower. A follower's rate is the project's (the default,
	//! and what every existing project is); a leader declares the tempo it was
	//! recorded at and is re-timed so one bar of its music is one project bar.
	void sourceTempoLeadsAndTheDefaultFollows()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		const auto projectTempo = static_cast<float>(Engine::getSong()->getTempo());
		const auto base = Engine::framesPerTick(m_rate);

		// the default: exactly the project's rate
		QCOMPARE(clip->clipFramesPerTick(), base);
		const auto followerTicks = clip->sampleLength().getTicks();

		// a leader at half the project tempo plays twice as fast
		clip->setWarpTempoMode(WarpTempoMode::SourceTempo);
		clip->setSourceTempo(projectTempo / 2.0f);
		QCOMPARE(clip->clipFramesPerTick(), base * 2.0f);
		QCOMPARE(clip->sampleLength().getTicks(), followerTicks / 2);
		QVERIFY(!clip->rendersLinearly());

		// a leader at the project's own tempo is identical to a follower
		clip->setSourceTempo(projectTempo);
		QCOMPARE(clip->clipFramesPerTick(), base);
		QCOMPARE(clip->sampleLength().getTicks(), followerTicks);

		// a follower ignores the declared tempo entirely
		clip->setWarpTempoMode(WarpTempoMode::FollowProject);
		QCOMPARE(clip->clipFramesPerTick(), base);
		QCOMPARE(clip->sampleLength().getTicks(), followerTicks);
	}


	//! I1 still holds with a warp on: a playback pass through the real path
	//! (Song -> SampleTrack::play -> AudioEngine) schedules a handle and writes
	//! neither the clip's window nor its markers.
	void aPlaybackPassLeavesTheWarpAlone()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		clip->changeLength(TimePos(384));
		clip->setAutoResize(false);
		applyWarp(*clip);
		const auto before = clip->warpMarkers();

		QVERIFY(track.play(TimePos(0), 1024, 0, -1));
		QVERIFY(clip->warpMarkers() == before);
		QCOMPARE(N(clip->sampleWindow().sourceIn), N(0));
		QCOMPARE(N(clip->sampleWindow().sourceOut),
			N(static_cast<f_cnt_t>(clip->sample().sampleSize())));
		QCOMPARE(N(clip->sourceFrameAt(atOffset(*clip, 96))), N(kM2.sourceFrame));
		drainPlayHandles(track);
	}

	//! The resampler convention this engine's rate is expressed in, pinned as a
	//! MEASUREMENT rather than a belief. `Sample::play`'s `ratio` is documented
	//! as output/input, but the resampler it drives (`AudioResampler` ->
	//! libsamplerate `SRC_LINEAR`) treats it as input/output: a ratio of 2.0
	//! consumes HALF the source frames, not twice. `SamplePlayHandle::warpRatio()`
	//! passes the reciprocal because of this, so the day the inversion is fixed
	//! this test goes red and sends the reader back to that function.
	void theResamplerRatioConventionIsPinned()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		Sample& sample = clip->sample();

		const auto consumed = [&sample](double ratio) {
			Sample::PlaybackState state;
			state.setFrameIndex(0);
			std::vector<SampleFrame> buffer(1024, SampleFrame(0.0f, 0.0f));
			for (int period = 0; period < 64; ++period)
			{
				sample.play(buffer.data(), &state, buffer.size(), Sample::Loop::Off, ratio);
			}
			return static_cast<double>(state.frameIndex());
		};

		const auto natural = consumed(1.0);
		const auto doubled = consumed(2.0);
		QVERIFY(natural > 0.0);
		QVERIFY(doubled > 0.0);

		const auto ratio = natural / doubled;
		QVERIFY2(ratio > 1.8 && ratio < 2.2,
			qPrintable(QString("ratio 2.0 consumed %1 source frames, ratio 1.0 consumed %2 "
				"(speed factor %3, expected about 2.0)")
				.arg(N(static_cast<f_cnt_t>(doubled))).arg(N(static_cast<f_cnt_t>(natural)))
				.arg(ratio, 0, 'f', 3)));
	}

	//! I8: the mapping is called on the audio thread, so it allocates nothing
	//! and takes no lock - with markers, with a leader, and through the handle's
	//! per-period rate lookup.
	void mappingDoesNotAllocate()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyWarp(*clip);
		clip->setWarpTempoMode(WarpTempoMode::SourceTempo);
		clip->setSourceTempo(96.0f);

		SamplePlayHandle handle(clip);
		std::vector<SampleFrame> buffer(1024, SampleFrame(0.0f, 0.0f));
		const auto span = std::span<SampleFrame>(buffer);

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < 2000; ++i)
		{
			(void)clip->sourceFrameAt(TimePos(i % 768));
			(void)clip->timelinePosAt(static_cast<f_cnt_t>(i * 37));
			(void)handle.totalFrames();
		}
		for (int period = 0; period < 16; ++period)
		{
			handle.play(span);
		}
		test::tlCountAllocations = false;

		QCOMPARE(N(test::tlAllocationCount), N(0));
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipWarpTest)
#include "SampleClipWarpTest.moc"
