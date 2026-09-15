/*
 * RetroAudioCaptureTest.cpp - retrospective AUDIO capture: off by default, one
 *                              bounded window, drop-oldest, and a take that holds
 *                              exactly the retained frames (0.3.0, feature row 16)
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
 * The MIDI half of retrospective capture (include/RetroMidiCapture.h) is the
 * model, and this file holds this half to it: OFF until armed, one bounded
 * pre-allocated ring, a producer entry point that is realtime-safe (no
 * allocation - asserted here with the same dynamic allocation probe the offline
 * harness uses), drop-OLDEST so the window is always the most recent frames, and
 * counters for what fell out of it. The take writer is checked against the frames
 * that were retained, not against the frames that were pushed: a window is a past,
 * and the file must hold that past and nothing else.
 */

#include "RetroAudioCapture.h"
#include "RetroAudioRing.h"
#include "SampleFrame.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QFileInfo>
#include <QTemporaryDir>

#include <sndfile.h>

#include "AllocationProbe.h"

using lmms::RetroAudioCapture;
using lmms::RetroAudioRing;
using lmms::SampleFrame;
using lmms::sample_t;

namespace
{

constexpr int SampleRate = 48000;
constexpr double OneLsb24 = 1.0 / 16777216.0;

//! Frame \a frame as an exact dyadic value (multiples of 1/64, |x| <= 0.5).
SampleFrame frameValue(std::size_t frame)
{
	const auto left = static_cast<sample_t>(static_cast<int>(frame % 16) - 8) * 0.0625f;
	const auto right = static_cast<sample_t>(static_cast<int>(frame % 8) - 4) * 0.125f;
	return SampleFrame(left, right);
}

std::vector<SampleFrame> makeFrames(std::size_t count, std::size_t offset = 0)
{
	std::vector<SampleFrame> frames(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		frames[i] = frameValue(offset + i);
	}
	return frames;
}

} // namespace


class RetroAudioCaptureTest : public QObject
{
	Q_OBJECT

private slots:

	//! OFF BY DEFAULT: the mode is the only thing that makes this cost anything,
	//! and while it is off the window stays empty however much audio goes past.
	void nothingIsRecordedUntilTheCaptureIsArmed()
	{
		RetroAudioCapture capture;
		QVERIFY(!capture.isArmed());

		const auto frames = makeFrames(4096);
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{0});
		QCOMPARE(capture.ring().overwrittenCount(), std::uint64_t{0});

		QVERIFY(capture.arm(true));
		QVERIFY(capture.isArmed());
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{4096});

		// Disarming stops recording, and what was already retained stays: the
		// window is a past, so the frames recorded BEFORE a disarm are still
		// there to be written.
		QVERIFY(!capture.arm(false));
		QVERIFY(!capture.isArmed());
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{4096});
	}

	//! The window is the LAST capacity frames: drop-OLDEST, exactly as the MIDI
	//! ring's is, with the frames that fell out of it counted.
	void theWindowIsTheMostRecentFramesAndCountsWhatFellOut()
	{
		constexpr std::size_t capacity = 64;
		RetroAudioRing ring(capacity);
		QCOMPARE(ring.capacity(), capacity);

		auto frames = makeFrames(100);
		QCOMPARE(ring.push(frames.data(), frames.size()), std::size_t{100});
		QCOMPARE(ring.bufferedCount(), capacity);
		QCOMPARE(ring.overwrittenCount(), std::uint64_t{36});

		std::vector<SampleFrame> window(capacity);
		QCOMPARE(ring.copyOut(window.data(), window.size()), capacity);
		// The retained window is frames 36..99: the oldest 36 fell out.
		for (std::size_t i = 0; i < capacity; ++i)
		{
			const auto expected = frameValue(36 + i);
			QCOMPARE(window[i].left(), expected.left());
			QCOMPARE(window[i].right(), expected.right());
		}
		QCOMPARE(ring.pausedDropCount(), std::uint64_t{0});
		QCOMPARE(ring.refusedSnapshots(), std::uint64_t{0});
	}

	//! The producer path allocates nothing. This is the programme's realtime
	//! assertion (the same dynamic probe the offline recording harness uses), run
	//! on the entry point the engine's audio thread calls.
	void theProducerPathDoesNotAllocate()
	{
		RetroAudioCapture capture;
		capture.arm(true);
		const auto frames = makeFrames(2048);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int period = 0; period < 16; ++period)
		{
			capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));
		}
		const auto allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{2048 * 16});
	}

	//! The retained window comes out in ORDER, oldest first, and one window is one
	//! consistent copy (the publication handshake, with no producer running).
	void theWindowComesOutOldestFirstAndComplete()
	{
		RetroAudioCapture capture;
		capture.arm(true);
		const auto frames = makeFrames(1000, 100);
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));

		const RetroAudioCapture::Window window = capture.takeWindow();
		QCOMPARE(window.frames.size(), std::size_t{1000});
		QCOMPARE(window.refused, std::size_t{0});
		for (std::size_t i = 0; i < window.frames.size(); ++i)
		{
			QCOMPARE(window.frames[i].left(), frames[i].left());
			QCOMPARE(window.frames[i].right(), frames[i].right());
		}

		// Taking a window does not consume the ring: the same window can be asked
		// for twice, which is what makes a status read cheap.
		const RetroAudioCapture::Window again = capture.takeWindow();
		QCOMPARE(again.frames.size(), std::size_t{1000});
	}

	//! The take writer: a 24-bit STEREO WAV holding exactly the retained frames.
	void theTakeWriterWritesTheRetainedWindow()
	{
		RetroAudioCapture capture;
		capture.arm(true);
		const auto frames = makeFrames(700);
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()));
		const RetroAudioCapture::Window window = capture.takeWindow();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("retro.wav"));

		std::uint64_t written = 0;
		QString error;
		QVERIFY2(writeTake(path, window, &written, &error), qPrintable(error));
		QCOMPARE(written, std::uint64_t{700});

		// Read it back with libsndfile: the file's own header says stereo 24-bit,
		// and every sample is the frame that was retained.
		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.channels, 2);
		QCOMPARE(info.samplerate, SampleRate);
		QCOMPARE(info.frames, static_cast<sf_count_t>(700));

		std::vector<sample_t> interleaved(700 * 2, 0.f);
		const auto read = sf_readf_float(file, interleaved.data(), 700);
		sf_close(file);
		QCOMPARE(read, static_cast<sf_count_t>(700));
		for (std::size_t i = 0; i < 700; ++i)
		{
			QVERIFY(std::fabs(static_cast<double>(interleaved[i * 2])
				- static_cast<double>(window.frames[i].left())) <= OneLsb24);
			QVERIFY(std::fabs(static_cast<double>(interleaved[i * 2 + 1])
				- static_cast<double>(window.frames[i].right())) <= OneLsb24);
		}
	}

	//! An EMPTY window is refused, typed, and no file is created: a take with no
	//! audio in it is not a take.
	void anEmptyWindowIsRefusedRatherThanWritten()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("empty.wav"));

		std::uint64_t written = 12345;
		QString error;
		const std::vector<SampleFrame> empty;
		QVERIFY(!lmms::writeRetroAudioTake(path, empty, SampleRate, &written, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(written, std::uint64_t{0});
		QVERIFY(!QFileInfo::exists(path));
	}

private:
	//! The writer, called once through a named helper so the failure message of
	//! the QVERIFY2 above carries the writer's own text.
	static bool writeTake(const QString& path, const lmms::RetroAudioCapture::Window& window,
		std::uint64_t* written, QString* error)
	{
		return lmms::writeRetroAudioTake(path, window.frames, SampleRate, written, error);
	}
} ;

QTEST_GUILESS_MAIN(RetroAudioCaptureTest)

#include "RetroAudioCaptureTest.moc"
