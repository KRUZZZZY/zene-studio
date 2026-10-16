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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <QDir>
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
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{0});
		QCOMPARE(capture.ring().overwrittenCount(), std::uint64_t{0});

		QVERIFY(capture.arm(true));
		QVERIFY(capture.isArmed());
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);
		QCOMPARE(capture.ring().bufferedCount(), std::size_t{4096});

		// Disarming stops recording, and what was already retained stays: the
		// window is a past, so the frames recorded BEFORE a disarm are still
		// there to be written.
		QVERIFY(!capture.arm(false));
		QVERIFY(!capture.isArmed());
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);
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
			capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);
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
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);

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

	//! Luna cluster 4901: a window carries the overwrite count it was COPIED with.
	//! A capture keeps running while the take is written, so a count read from the
	//! live ring afterwards also counts frames the window never contained - the
	//! take would report overwrites that did not affect its captured window.
	void theWindowCarriesTheOverwriteCountItWasCopiedWith()
	{
		RetroAudioCapture capture;
		capture.arm(true);

		// Overfill the ring by exactly its own capacity, so the overwrite count is
		// non-zero and every later push moves it (the default capacity is 1Mi
		// frames, so the count is driven from capacity(), not hardcoded).
		const std::size_t capacity = capture.ring().capacity();
		QVERIFY(capacity > 0);
		const auto chunk = makeFrames(1024, 100);
		while (capture.ring().bufferedCount() < capacity)
		{
			capture.push(chunk.data(), static_cast<lmms::f_cnt_t>(chunk.size()), SampleRate);
		}
		capture.push(chunk.data(), static_cast<lmms::f_cnt_t>(chunk.size()), SampleRate);
		const std::uint64_t atSnapshot = capture.ring().overwrittenCount();
		QVERIFY(atSnapshot > 0);

		const RetroAudioCapture::Window window = capture.takeWindow();
		// The count belongs to THIS window...
		QCOMPARE(static_cast<std::uint64_t>(window.overwritten), atSnapshot);

		// ...and the producer keeps overwriting after the copy, as it does while
		// the take file is being written.
		capture.push(chunk.data(), static_cast<lmms::f_cnt_t>(chunk.size()), SampleRate);
		QVERIFY(capture.ring().overwrittenCount() > atSnapshot);
		// A window already taken still reports the count it was copied with.
		QCOMPARE(static_cast<std::uint64_t>(window.overwritten), atSnapshot);
	}

	//! The take writer: a 24-bit STEREO WAV holding exactly the retained frames.
	void theTakeWriterWritesTheRetainedWindow()
	{
		RetroAudioCapture capture;
		capture.arm(true);
		const auto frames = makeFrames(700);
		capture.push(frames.data(), static_cast<lmms::f_cnt_t>(frames.size()), SampleRate);
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

	//! Concurrent writes to one destination each commit a complete take. Their
	//! temporary files must be independent while either writer is still active.
	void concurrentTakesToOnePathRemainIndependent()
	{
		constexpr std::size_t frameCount = 2u * 1024u * 1024u;
		const SampleFrame firstFrame(sample_t{0.25f}, sample_t{-0.5f});
		const SampleFrame secondFrame(sample_t{-0.75f}, sample_t{0.125f});
		const std::vector<SampleFrame> firstFrames(frameCount, firstFrame);
		const std::vector<SampleFrame> secondFrames(frameCount, secondFrame);
		QTemporaryDir tempDir;
		QVERIFY(tempDir.isValid());
		QDir dir(tempDir.path());
		const QString path = dir.filePath(QStringLiteral("concurrent.wav"));

		bool firstOk = false;
		std::uint64_t firstWritten = 0;
		QString firstError;
		std::thread first([&]()
		{
			firstOk = lmms::writeRetroAudioTake(path, firstFrames, SampleRate,
				&firstWritten, &firstError);
		});

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		bool writerVisible = false;
		while (!writerVisible && std::chrono::steady_clock::now() < deadline)
		{
			const QStringList entries = dir.entryList(QDir::Files | QDir::Hidden
				| QDir::NoDotAndDotDot);
			const bool stagingObserved = std::any_of(entries.cbegin(), entries.cend(),
				[](const QString& entry) { return entry != QStringLiteral("concurrent.wav"); });
			writerVisible = stagingObserved || QFileInfo::exists(path);
			if (!writerVisible) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
		}

		if (!writerVisible)
		{
			first.join();
			QFAIL("first writer did not create staging or destination file");
		}

		bool secondOk = false;
		std::uint64_t secondWritten = 0;
		QString secondError;
		std::thread second([&]()
		{
			secondOk = lmms::writeRetroAudioTake(path, secondFrames, SampleRate,
				&secondWritten, &secondError);
		});
		first.join();
		second.join();

		QVERIFY2(firstOk, qPrintable(firstError));
		QVERIFY2(secondOk, qPrintable(secondError));
		QCOMPARE(firstWritten, static_cast<std::uint64_t>(frameCount));
		QCOMPARE(secondWritten, static_cast<std::uint64_t>(frameCount));

		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.channels, 2);
		QCOMPARE(info.frames, static_cast<sf_count_t>(frameCount));

		constexpr sf_count_t blockFrames = 4096;
		std::vector<sample_t> block(static_cast<std::size_t>(blockFrames) * 2);
		const auto read = sf_readf_float(file, block.data(), blockFrames);
		QCOMPARE(read, blockFrames);
		const sample_t initialLeft = block[0];
		const sample_t initialRight = block[1];
		const auto sampleMatches = [](sample_t actual, sample_t expected)
		{
			return std::fabs(static_cast<double>(actual) - static_cast<double>(expected))
				<= 2 * OneLsb24;
		};
		const bool containsFirst = sampleMatches(block[0], firstFrame.left())
			&& sampleMatches(block[1], firstFrame.right());
		const bool containsSecond = sampleMatches(block[0], secondFrame.left())
			&& sampleMatches(block[1], secondFrame.right());
		bool intact = containsFirst || containsSecond;
		const SampleFrame expected = containsFirst ? firstFrame : secondFrame;
		for (sf_count_t frame = 0; intact && frame < read; ++frame)
		{
			intact = sampleMatches(block[static_cast<std::size_t>(frame) * 2], expected.left())
				&& sampleMatches(block[static_cast<std::size_t>(frame) * 2 + 1], expected.right());
		}
		for (sf_count_t offset = read; intact && offset < info.frames; offset += blockFrames)
		{
			const sf_count_t count = std::min(blockFrames, info.frames - offset);
			const sf_count_t countRead = sf_readf_float(file, block.data(), count);
			intact = countRead == count;
			for (sf_count_t frame = 0; intact && frame < count; ++frame)
			{
				intact = sampleMatches(block[static_cast<std::size_t>(frame) * 2], expected.left())
					&& sampleMatches(block[static_cast<std::size_t>(frame) * 2 + 1], expected.right());
			}
		}
		sf_close(file);
		QVERIFY2(intact, qPrintable(QStringLiteral("initial samples %1/%2; expected %3/%4")
			.arg(static_cast<double>(initialLeft), 0, 'g', 9)
			.arg(static_cast<double>(initialRight), 0, 'g', 9)
			.arg(static_cast<double>(expected.left()), 0, 'g', 9)
			.arg(static_cast<double>(expected.right()), 0, 'g', 9)));
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

	//! Luna cluster new-p12-200, BUG 5: a take must not label frames recorded at
	//! several sample rates with a single rate. While armed, if the engine's
	//! rate changes the stale (pre-change) frames must be dropped so the window
	//! stays rate-homogeneous and the take is labelled with the one rate its
	//! frames were actually captured at.
	void aTakeLabelsOneSampleRateEvenAfterAnEngineRateChange()
	{
		constexpr int FirstRate = 48000;
		constexpr int SecondRate = 96000;

		RetroAudioCapture capture;
		capture.arm(true);

		// The engine records at 48 kHz for a while...
		const auto firstFrames = makeFrames(700);
		capture.push(firstFrames.data(), static_cast<lmms::f_cnt_t>(firstFrames.size()), FirstRate);

		// ...then the engine's sample rate changes while the capture is STILL
		// armed, and more audio arrives.
		const auto secondFrames = makeFrames(200, 700);
		capture.push(secondFrames.data(), static_cast<lmms::f_cnt_t>(secondFrames.size()), SecondRate);

		const RetroAudioCapture::Window window = capture.takeWindow();
		// The window is rate-homogeneous: only the 96 kHz frames remain, and the
		// window reports that single rate - not 0, and not the old 48 kHz.
		QCOMPARE(window.sampleRate, SecondRate);
		QCOMPARE(window.frames.size(), std::size_t{200});
		for (std::size_t i = 0; i < window.frames.size(); ++i)
		{
			QCOMPARE(window.frames[i].left(), secondFrames[i].left());
			QCOMPARE(window.frames[i].right(), secondFrames[i].right());
		}

		// The take written from this window is labelled with the one rate the
		// window's frames were recorded at.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("retro-rate.wav"));
		std::uint64_t written = 0;
		QString error;
		QVERIFY2(lmms::writeRetroAudioTake(path, window.frames, window.sampleRate,
			&written, &error), qPrintable(error));
		QCOMPARE(written, std::uint64_t{200});

		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(info.samplerate, static_cast<int>(SecondRate));
		sf_close(file);
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
