/*
 * SampleClipWindowRemainderTest.cpp - cluster new-p12-222: a playback callback
 *                                      longer than the window's remainder.
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
 * License along with this program (see COPYING); if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 */

/* The defect this file was written to catch (Luna's patrol, cluster new-p12-222):
 *
 * `SamplePlayHandle::play` rendered the whole callback even when the snapshotted
 * window had fewer frames left. `totalFrames()` bounds the handle, but nothing
 * bounded THIS period, and `Sample::play` (src/core/Sample.cpp:100) is given the
 * buffer, the cursor and a ratio - never the window.
 *
 * A NON-looping clip hides the overrun: `Sample::render` returns short at the
 * clip's end frame and `Sample::play` zero-fills the rest. A LOOPING clip does
 * not - `snapshotAudioLoop` sets the loop copy's end frame to the whole buffer
 * and `Loop::On` (src/core/ClipAudioLoop.cpp:204-209), so `render` WRAPS the
 * cursor instead of returning short (src/core/Sample.cpp:220-226) and the frames
 * past the window are source audio. That is audible, and it happens at the last
 * period of every looped clip.
 *
 * These tests were written and run against the unmodified tree first, where all
 * three fail. Their own target rather than a test added to SampleClipWindowTest.cpp
 * because Gate 7 measures that file at 494 of its 500 lines.
 */

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <array>
#include <span>
#include <vector>

#include "Engine.h"
#include "AudioEngine.h"
#include "ClipEdits.h"
#include "LmmsTypes.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "SampleWindow.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! Source length: long enough that the source is never the limiting factor.
constexpr int kSourceSeconds = 8;

qulonglong asNumber(std::size_t value)
{
	return static_cast<qulonglong>(value);
}

/*! A source that is 0.5 on EVERY frame.
 *
 *  The source has to be audible past the window, or the overrun renders silence
 *  and looks correct: `SampleClipWindowTest`'s step fixture is silent outside its
 *  window and therefore cannot see this defect at all.
 */
std::vector<SampleFrame> makeConstant(int rate)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	return std::vector<SampleFrame>(frames, SampleFrame(0.5f, 0.5f));
}

//! A track holding one clip over the constant source.
SampleClip* makeConstantClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeConstant(rate);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

} // namespace


class SampleClipWindowRemainderTest : public QObject
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

	/*! A period longer than what the window has left renders only the window:
	 *  the window's own frames sound, the rest of the period is silence, and the
	 *  handle advances by what it rendered so `framesDone()` never passes
	 *  `totalFrames()`.
	 */
	void aCallbackLongerThanTheWindowRendersOnlyTheWindow()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t windowFrames = 100;
		const f_cnt_t period = 256;	// the audio device's period; longer than the window

		SampleTrack track(Engine::getSong());
		auto* clip = makeConstantClip(track, rate);
		clip->setSampleWindow(SampleWindow(0, windowFrames));

		SamplePlayHandle handle(clip);
		QCOMPARE(asNumber(handle.totalFrames()), asNumber(windowFrames));

		std::vector<SampleFrame> buffer(period, SampleFrame(9.0f, 9.0f));
		handle.play(std::span<SampleFrame>(buffer));

		// the window's own frames are rendered: the clamp is not "stop early"
		for (f_cnt_t f = 0; f < windowFrames; ++f)
		{
			QVERIFY2(std::fabs(buffer[f][0] - 0.5f) < 1e-6f,
				qPrintable(QStringLiteral("window frame %1 is not the source: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		// ... and every frame past it is silence, not the next source frames
		for (f_cnt_t f = windowFrames; f < period; ++f)
		{
			QVERIFY2(buffer[f][0] == 0.0f && buffer[f][1] == 0.0f,
				qPrintable(QStringLiteral("frame %1 past the window is audible: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		// the handle advanced by what it rendered, not by the period
		QCOMPARE(asNumber(handle.framesDone()), asNumber(windowFrames));
		QVERIFY(handle.isFinished());

		// a further period renders nothing and still advances nothing
		std::vector<SampleFrame> again(period, SampleFrame(9.0f, 9.0f));
		handle.play(std::span<SampleFrame>(again));
		for (f_cnt_t f = 0; f < period; ++f)
		{
			QCOMPARE(again[f][0], 0.0f);
		}
		QCOMPARE(asNumber(handle.framesDone()), asNumber(windowFrames));
	}

	/*! THE AUDIBLE CASE. A looping clip renders through `Sample::Loop::On` with the
	 *  loop copy's end frame at the whole buffer, so `Sample::render` wraps instead
	 *  of returning short and the frames past the window are source audio. Every
	 *  period whose remaining window is shorter than the period - the last period
	 *  of every looped clip - played audio past the clip's end until the clamp.
	 */
	void aCallbackLongerThanALoopedWindowPlaysOnlyTheWindow()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t windowFrames = 100;
		const f_cnt_t period = 256;

		SampleTrack track(Engine::getSong());
		auto* clip = makeConstantClip(track, rate);
		clip->setSampleWindow(SampleWindow(0, windowFrames));

		ClipEdits edits;
		edits.audioLoopStart = 0;
		edits.audioLoopFrames = 50;	// a loop well inside the source
		clip->setClipEdits(edits);

		SamplePlayHandle handle(clip);
		QCOMPARE(asNumber(handle.totalFrames()), asNumber(windowFrames));

		std::vector<SampleFrame> buffer(period, SampleFrame(9.0f, 9.0f));
		handle.play(std::span<SampleFrame>(buffer));

		// the looped window still renders inside the window
		for (f_cnt_t f = 0; f < windowFrames; ++f)
		{
			QVERIFY2(std::fabs(buffer[f][0] - 0.5f) < 1e-6f,
				qPrintable(QStringLiteral("window frame %1 is not the source: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		// ... and the loop does not play on past the window
		for (f_cnt_t f = windowFrames; f < period; ++f)
		{
			QVERIFY2(buffer[f][0] == 0.0f && buffer[f][1] == 0.0f,
				qPrintable(QStringLiteral("the loop played past the window at frame %1: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		QCOMPARE(asNumber(handle.framesDone()), asNumber(windowFrames));
	}

	/*! The clamp composes with the first-period offset: that block moves
	 *  `workingBuffer` and shrinks `frames`, so the tail this fix silences has to
	 *  be the settled region, not the period's original length.
	 */
	void aShortWindowRendersCorrectlyAfterAFirstPeriodOffset()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t windowFrames = 100;
		const f_cnt_t offsetFrames = 10;
		const f_cnt_t period = 256;

		SampleTrack track(Engine::getSong());
		auto* clip = makeConstantClip(track, rate);
		clip->setSampleWindow(SampleWindow(0, windowFrames));

		SamplePlayHandle handle(clip);
		handle.setOffset(offsetFrames);
		std::vector<SampleFrame> buffer(period, SampleFrame(9.0f, 9.0f));
		handle.play(std::span<SampleFrame>(buffer));

		// the offset is silence
		for (f_cnt_t f = 0; f < offsetFrames; ++f)
		{
			QCOMPARE(buffer[f][0], 0.0f);
		}
		// the window follows it ...
		for (f_cnt_t f = offsetFrames; f < offsetFrames + windowFrames; ++f)
		{
			QVERIFY2(std::fabs(buffer[f][0] - 0.5f) < 1e-6f,
				qPrintable(QStringLiteral("frame %1 is not the source: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		// ... and nothing past it sounds
		for (f_cnt_t f = offsetFrames + windowFrames; f < period; ++f)
		{
			QVERIFY2(buffer[f][0] == 0.0f,
				qPrintable(QStringLiteral("frame %1 past the window is audible: %2")
					.arg(asNumber(f)).arg(buffer[f][0])));
		}
		QCOMPARE(asNumber(handle.framesDone()), asNumber(windowFrames));
	}
};

QTEST_GUILESS_MAIN(SampleClipWindowRemainderTest)
#include "SampleClipWindowRemainderTest.moc"
