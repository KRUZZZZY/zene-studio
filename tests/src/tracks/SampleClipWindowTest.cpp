/*
 * SampleClipWindowTest.cpp - Slice 0 of the clip-and-capture wave (task #611):
 *                            the clip's authored source window, and the
 *                            read-only playback path.
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

// The defect this file was written to catch (docs/CLIP-CAPTURE-DESIGN.md §§2.1,
// 2.5, 4.1): `SampleTrack::play` recomputed the clip's source window from the
// playback position on every pass and wrote it back into the clip's own `Sample`
// (`src/tracks/SampleTrack.cpp:126-127`), so any trim a user made reverted the
// moment the transport reached the clip. Slice 0 makes the authored window the
// only source of truth and the playback path read-only.
//
// `authoredWindowSurvivesAPlaybackPass` is the RED test: it was written and run
// against the unmodified tree first, where the playback pass overwrote both
// frames the assertion checks.
//
// The `__has_include` guard is deliberate: before Slice 0 the window is
// observable only through `Sample`'s frame fields, after it through
// `SampleClip::sampleWindow()`. Guarding the direct assertions lets this one
// file be the RED test on the old tree and the regression guard on the new one.

#include <QtTest>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "../core/AllocationProbe.h"

#include "Engine.h"
#include "AudioEngine.h"
#include "LmmsTypes.h"
#include "PlayHandle.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

#if __has_include("SampleWindow.h")
#include "SampleWindow.h"
#define LMMS_HAVE_SAMPLE_WINDOW 1
#endif

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
//! Source length: long enough that the clip is never the limiting factor.
constexpr int kSourceSeconds = 8;
//! Harness render period; the audio device's own is not needed by these assertions.
constexpr f_cnt_t kPeriod = 1024;
//! The window used by the defect test: the second of source audio, [1 s, 2 s).
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

//! Silence, then a constant 0.5 inside [in, out), then silence again: a constant
//! survives the resampler, so the rendered window's start and length are
//! measurable without depending on the interpolator's phase.
std::vector<SampleFrame> makeStep(int rate, f_cnt_t in, f_cnt_t out)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	std::vector<SampleFrame> data(frames, SampleFrame(0.0f, 0.0f));
	for (f_cnt_t f = in; f < out && f < frames; ++f)
	{
		data[f] = SampleFrame(0.5f, 0.5f);
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

//! A clip whose source is the step-shaped signal, for the rendered-window test.
SampleClip* makeStepClip(SampleTrack& track, int rate, f_cnt_t in, f_cnt_t out)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeStep(rate, in, out);
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


class SampleClipWindowTest : public QObject
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

	//! THE RED TEST. A window authored on the clip is authored state: a playback
	//! pass must read it and leave it alone. Before Slice 0 the pass rewrote it
	//! from the playback position, so a trim reverted on the next pass.
	void authoredWindowSurvivesAPlaybackPass()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);

		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));

		// what the trim authored, observed the only way the old tree offers
		const auto authoredIn = static_cast<f_cnt_t>(clip->sample().startFrame());
		const auto authoredOut = static_cast<f_cnt_t>(clip->sample().endFrame());
		QCOMPARE(asNumber(authoredIn), asNumber(trimIn(rate)));
		QCOMPARE(asNumber(authoredOut), asNumber(trimOut(rate)));

		// exactly the call Song::processNextBuffer() makes
		const bool played = track.play(TimePos(0), kPeriod, 0, -1);
		QVERIFY(played);

		// the defect: the playback pass wrote the clip's window
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().startFrame())), asNumber(authoredIn));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().endFrame())), asNumber(authoredOut));

#ifdef LMMS_HAVE_SAMPLE_WINDOW
		// ... and with the window a first-class clip quantity, assert it directly
		QCOMPARE(asNumber(clip->sampleWindow().sourceIn), asNumber(authoredIn));
		QCOMPARE(asNumber(clip->sampleWindow().sourceOut), asNumber(authoredOut));
#endif
		drainPlayHandles(track);
	}

	//! A seek back into the clip (the flag Song clears) must not rewrite it either,
	//! and the window must not drift as the transport moves through the clip.
	void repeatedPassesLeaveTheWindowAlone()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);

		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));
		const auto authoredIn = static_cast<f_cnt_t>(clip->sample().startFrame());
		const auto authoredOut = static_cast<f_cnt_t>(clip->sample().endFrame());

		// a pass at the clip's start, then two more after a seek
		QVERIFY(track.play(TimePos(0), kPeriod, 0, -1));
		clip->setIsPlaying(false);
		track.play(TimePos(16), kPeriod, 0, -1);
		clip->setIsPlaying(false);
		track.play(TimePos(32), kPeriod, 0, -1);

		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().startFrame())), asNumber(authoredIn));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().endFrame())), asNumber(authoredOut));
		drainPlayHandles(track);
	}

	//! I4: a window that is not well formed is a REJECTED edit, not a clamped one.
	void malformedWindowIsRejected()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);

		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));
		const auto authoredIn = static_cast<f_cnt_t>(clip->sample().startFrame());
		const auto authoredOut = static_cast<f_cnt_t>(clip->sample().endFrame());

		// an empty window (in == out): nothing to play, so the edit is refused
		clip->setSamplePlayLength(trimIn(rate));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().startFrame())), asNumber(authoredIn));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().endFrame())), asNumber(authoredOut));

		// a start at or past the window end is refused too
		clip->setSampleStartFrame(trimOut(rate) + 1);
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().startFrame())), asNumber(authoredIn));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().endFrame())), asNumber(authoredOut));
	}

	//! The design's own test for the non-destructive primitive (§3 row 1): two clips
	//! share one source buffer, keep independent windows, and playback never writes
	//! the source bytes (I2).
	void twoClipsShareOneBufferWithIndependentWindows()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack trackA(Engine::getSong());
		SampleTrack trackB(Engine::getSong());
		auto* clipA = makeToneClip(trackA, rate);
		auto* clipB = makeToneClip(trackB, rate);

		// the second clip references the first one's buffer; zero audio is copied
		const auto shared = clipA->sample().buffer();
		clipB->setSampleBuffer(shared);
		QVERIFY(clipA->sample().buffer() == clipB->sample().buffer());
		QVERIFY(clipA->sample().buffer() == shared);

		const auto before = makeTone(rate);
		QCOMPARE(asNumber(shared->size()), asNumber(before.size()));
		QVERIFY(std::memcmp(shared->data(), before.data(), before.size() * sizeof(SampleFrame)) == 0);

		// A is trimmed, B is not: one buffer, two windows
		clipA->setSampleStartFrame(trimIn(rate));
		clipA->setSamplePlayLength(trimOut(rate));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipB->sample().startFrame())), asNumber(0));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipB->sample().endFrame())), asNumber(before.size()));

		// each clip's playback length already is its own window's length
		SamplePlayHandle handleA(clipA);
		SamplePlayHandle handleB(clipB);
		QCOMPARE(asNumber(handleA.totalFrames()), asNumber(trimOut(rate) - trimIn(rate)));
		QCOMPARE(asNumber(handleB.totalFrames()), asNumber(before.size()));

		// a playback pass on A must not disturb A's window, B's window, or the source
		QVERIFY(trackA.play(TimePos(0), kPeriod, 0, -1));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipA->sample().startFrame())), asNumber(trimIn(rate)));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipA->sample().endFrame())), asNumber(trimOut(rate)));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipB->sample().startFrame())), asNumber(0));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clipB->sample().endFrame())), asNumber(before.size()));

		// I2: the shared source is immutable - the pass wrote nothing into it
		QVERIFY(std::memcmp(shared->data(), before.data(), before.size() * sizeof(SampleFrame)) == 0);

		drainPlayHandles(trackA);
	}

	//! The read path renders EXACTLY the authored window: it starts inside the
	//! source at the window's first frame and stops at the window's last.
	void playbackRendersExactlyTheAuthoredWindow()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t in = trimIn(rate);
		const f_cnt_t out = trimOut(rate);

		SampleTrack track(Engine::getSong());
		auto* clip = makeStepClip(track, rate, in, out);
		clip->setSampleStartFrame(in);
		clip->setSamplePlayLength(out);

		SamplePlayHandle handle(clip);
		QCOMPARE(asNumber(handle.totalFrames()), asNumber(out - in));

		// render enough to cover the window plus a generous tail
		const auto windowFrames = static_cast<f_cnt_t>(handle.totalFrames());
		const f_cnt_t periods = windowFrames / kPeriod + 4;
		std::vector<SampleFrame> rendered(periods * kPeriod, SampleFrame(9.0f, 9.0f));
		for (f_cnt_t p = 0; p < periods; ++p)
		{
			handle.play(std::span<SampleFrame>(rendered.data() + p * kPeriod, kPeriod));
		}

		// The render starts INSIDE the window: the step source is silent before
		// sourceIn, so the very first rendered frames could not be audible if
		// playback had started at the beginning of the file instead.
		f_cnt_t firstAudible = rendered.size();
		f_cnt_t lastAudible = 0;
		for (f_cnt_t f = 0; f < rendered.size(); ++f)
		{
			if (std::fabs(rendered[f][0]) > 1e-6f)
			{
				if (firstAudible == rendered.size()) { firstAudible = f; }
				lastAudible = f;
			}
		}
		QVERIFY(firstAudible <= 4);

		// the audio is the window's content ...
		for (f_cnt_t f = 128; f < 256; ++f)
		{
			QVERIFY(std::fabs(rendered[f][0] - 0.5f) < 1e-6f);
		}

		// ... and it ends with the window, not with the file: a render that had
		// played the whole source would still be audible here, and one that had
		// stopped early would not reach it
		QVERIFY(lastAudible >= windowFrames - 128);
		QVERIFY(lastAudible <= windowFrames + 4);
		for (f_cnt_t f = windowFrames + 4; f < rendered.size(); ++f)
		{
			QCOMPARE(rendered[f][0], 0.0f);
			QCOMPARE(rendered[f][1], 0.0f);
		}
	}

	//! The mapping seam the wave freezes (#597 attaches here): a timeline position
	//! maps onto a source frame inside the window, and the inverse maps back.
	void timelinePositionMapsOntoTheWindow()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t in = trimIn(rate);
		const f_cnt_t out = trimOut(rate);

		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(in);
		clip->setSamplePlayLength(out);

#ifdef LMMS_HAVE_SAMPLE_WINDOW
		const auto framesPerTick = Engine::framesPerTick(rate);

		// at and before the clip's start the first window frame is what plays
		QVERIFY(clip->sourceFrameAt(TimePos(0)) == in);
		QVERIFY(clip->sourceFrameAt(TimePos(-8)) == in);

		// inside the clip the mapping is the linear one this wave froze
		for (int t = 1; t < 64; ++t)
		{
			const auto expected = in + static_cast<f_cnt_t>(t * framesPerTick);
			QVERIFY(clip->sourceFrameAt(TimePos(t)) == expected);
		}

		// it never leaves the window, not even past the clip's own end
		QVERIFY(clip->sourceFrameAt(TimePos(100000)) == out);

		// and it is invertible to within the frame-to-tick truncation trimming needs
		for (int t = 0; t < 64; ++t)
		{
			const auto mapped = clip->sourceFrameAt(TimePos(t));
			QVERIFY(std::abs(static_cast<int>(clip->timelinePosAt(mapped)) - t) <= 1);
		}
#else
		// pre-Slice-0 tree: the clip-level mapping does not exist yet, so the only
		// observable proxy for the authored window is the Sample's frame fields
		QCOMPARE(static_cast<int>(clip->sample().startFrame()), static_cast<int>(in));
		QCOMPARE(static_cast<int>(clip->sample().endFrame()), static_cast<int>(out));
#endif
	}

	//! I5: the length the author sees is the window's length, and a tempo change
	//! re-fits that length without ever touching the window itself.
	void windowLengthAndTempoChange()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t in = trimIn(rate);
		const f_cnt_t out = trimOut(rate);

		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(in);
		clip->setSamplePlayLength(out);

		const auto expectedLength = static_cast<int>((out - in) / Engine::framesPerTick(rate));
		clip->setAutoResize(true);
		clip->updateLength();
		QVERIFY(std::abs(static_cast<int>(clip->length()) - expectedLength) <= 1);

		// a tempo change re-fits the length but must not touch the window
		clip->tempoChanged();
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().startFrame())), asNumber(in));
		QCOMPARE(asNumber(static_cast<f_cnt_t>(clip->sample().endFrame())), asNumber(out));
	}

	//! Behaviour preservation, exactly. For a clip whose window is the whole buffer,
	//! the window a pass derives is the arithmetic the pre-Slice-0 code performed
	//! (SampleTrack.cpp:117-122, before it also wrote it back), bit for bit - a
	//! deterministic proof that does not depend on how the renderer schedules.
	void untrimmedPassWindowMatchesThePreSliceArithmetic()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const auto framesPerTick = Engine::framesPerTick(rate);

		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		const auto bufferFrames = static_cast<f_cnt_t>(clip->sample().sampleSize());
		const auto lengthTicks = static_cast<int>(bufferFrames / framesPerTick);
		clip->changeLength(TimePos(lengthTicks));

		for (const int clipPos : { 0, 96 })
		{
			clip->movePosition(TimePos(clipPos));
			for (const int transport : { clipPos, clipPos + 1, clipPos + 48, clipPos + lengthTicks - 1 })
			{
				// the pre-Slice-0 arithmetic, verbatim from SampleTrack.cpp:117-122
				const auto oldStart = static_cast<f_cnt_t>(
					framesPerTick * (transport - clip->startPosition() - clip->startTimeOffset()));
				const auto oldClipFrames = static_cast<f_cnt_t>(framesPerTick
					* (clip->endPosition() - clip->startPosition() - clip->startTimeOffset()));
				const auto oldLength = oldClipFrames > bufferFrames ? bufferFrames : oldClipFrames;

				// what the pass derives now
				QCOMPARE(asNumber(clip->sourceFrameAt(TimePos(transport))), asNumber(oldStart));
				QCOMPARE(asNumber(clip->sourceFrameAt(clip->endPosition())), asNumber(oldLength));
			}
		}
	}

	//! I8: the read path's own arithmetic allocates nothing (probe pattern from
	//! tests/src/core/RecordRingBufferTest.cpp:238, tests/src/core/AllocationProbe.h).
	void readPathArithmeticDoesNotAllocate()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));
		SamplePlayHandle handle(clip);

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
#ifdef LMMS_HAVE_SAMPLE_WINDOW
			(void)clip->sourceFrameAt(TimePos(i % 192));
			(void)clip->timelinePosAt(static_cast<f_cnt_t>(i));
#endif
			(void)handle.totalFrames();
		}
		test::tlCountAllocations = false;

		QCOMPARE(asNumber(test::tlAllocationCount), asNumber(0));
	}

	//! I8: rendering a windowed handle allocates nothing either. What allocates on
	//! this path is the play HANDLE construction in SampleTrack::play, which is
	//! upstream's and outside this slice; the render loop itself must be clean.
	void playHandleRenderDoesNotAllocate()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = makeToneClip(track, rate);
		clip->setSampleStartFrame(trimIn(rate));
		clip->setSamplePlayLength(trimOut(rate));
		SamplePlayHandle handle(clip);
		std::vector<SampleFrame> buffer(kPeriod, SampleFrame(0.0f, 0.0f));
		const auto span = std::span<SampleFrame>(buffer);

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 64; ++period)
		{
			handle.play(span);
		}
		test::tlCountAllocations = false;

		QCOMPARE(asNumber(test::tlAllocationCount), asNumber(0));
	}
};

QTEST_GUILESS_MAIN(SampleClipWindowTest)
#include "SampleClipWindowTest.moc"
