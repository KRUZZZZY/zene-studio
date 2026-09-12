/*
 * RecordingRealtimeTest.cpp - realtime-safety evidence for the recording and
 *                              capture paths: D9b (no lock on the capture
 *                              thread), D9c (no audio-thread allocation in the
 *                              clip record path), D9a (no unbounded growth)
 *
 * Copyright (c) 2026 LMMS developers
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

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "AllocationProbe.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleFrame.h"
#include "SampleFrameRingBuffer.h"
#include "SampleRecordAccumulator.h"

using lmms::AudioEngine;
using lmms::f_cnt_t;
using lmms::sample_rate_t;
using lmms::sample_t;
using lmms::SampleFrame;
using lmms::SampleFrameRingBuffer;
using lmms::SampleRecordAccumulator;

namespace
{

// Exact float values (multiples of 1/64) so comparisons are exact.
sample_t testValue(std::uint64_t index)
{
	return static_cast<sample_t>(static_cast<long long>(index % 61) - 30) / 64.0f;
}

SampleFrame frameAt(std::uint64_t index)
{
	return SampleFrame(testValue(index), -testValue(index));
}

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

} // namespace


class RecordingRealtimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The engine-level cases below need a real AudioEngine. Qt needs a
		// platform plugin for that even though the test is guiless, which
		// tests/CMakeLists.txt arranges with QT_QPA_PLATFORM=offscreen.
		lmms::Engine::init(true);
		QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
		// Nothing may consume the capture staging ring while the bound and the
		// lock-independence of pushInputFrames() are measured.
		if (auto dev = lmms::Engine::audioEngine()->audioDev()) { dev->stopProcessing(); }
	}

	// -----------------------------------------------------------------------
	// D9a / staging ring: the input path is bounded and counts what it drops.
	// -----------------------------------------------------------------------
	void ringBufferIsBoundedAndCountsOverflow()
	{
		constexpr std::size_t capacity = 4096;
		constexpr std::size_t overflowBy = 1000;
		SampleFrameRingBuffer ring{capacity};
		QCOMPARE(asNumber(ring.capacity()), asNumber(capacity));

		std::vector<SampleFrame> block;
		for (std::size_t i = 0; i < capacity + overflowBy; ++i) { block.push_back(frameAt(i)); }

		const auto written = ring.writeBlock(block.data(), block.size());
		QCOMPARE(asNumber(written), asNumber(capacity));
		// Drop-newest: the unread data is never overwritten, and the drops are
		// counted rather than silently absorbed into unbounded storage.
		QCOMPARE(asNumber(ring.available()), asNumber(capacity));
		QCOMPARE(static_cast<qulonglong>(ring.overflowCount()), static_cast<qulonglong>(overflowBy));

		std::vector<SampleFrame> out(capacity);
		QCOMPARE(asNumber(ring.read(out.data(), out.size())), asNumber(capacity));
		for (std::size_t i = 0; i < capacity; ++i)
		{
			QCOMPARE(out[i].left(), block[i].left());
			QCOMPARE(out[i].right(), block[i].right());
		}
		QCOMPARE(asNumber(ring.available()), asNumber(0));

		// Once drained, the producer can fill it again - the capacity is
		// spatial, the counters are monotonic.
		QCOMPARE(asNumber(ring.writeBlock(block.data(), block.size())), asNumber(capacity));
	}

	//! The producer/consumer contract pushInputFrames()/drainInputStage() rely on.
	void ringBufferIsSpscUnderConcurrency()
	{
		constexpr std::size_t capacity = 1024;
		constexpr std::size_t totalFrames = 500000;
		SampleFrameRingBuffer ring{capacity};

		std::vector<SampleFrame> out;
		out.reserve(totalFrames);
		std::vector<SampleFrame> block(64);

		// Drop-newest overflow policy: a producer that must not lose frames
		// retries the part that did not fit, exactly as a capture backend does.
		std::thread producer([&] {
			std::size_t produced = 0;
			while (produced < totalFrames)
			{
				const auto batch = std::min(block.size(), totalFrames - produced);
				for (std::size_t i = 0; i < batch; ++i) { block[i] = frameAt(produced + i); }
				const auto written = ring.writeBlock(block.data(), batch);
				if (written == 0) { std::this_thread::yield(); }
				produced += written;
			}
		});

		std::vector<SampleFrame> scratch(128);
		while (out.size() < totalFrames)
		{
			const auto got = ring.read(scratch.data(), scratch.size());
			for (std::size_t i = 0; i < got; ++i) { out.push_back(scratch[i]); }
		}
		producer.join();

		// Nothing was lost and nothing was reordered across the handoff. The
		// producer retried the writes that did not fit, so the ring's
		// "rejected" counter is allowed to be non-zero here - it counts writes
		// the ring refused, not frames the take lost.
		QVERIFY2(out.size() >= totalFrames, "the consumer read fewer frames than were produced");
		for (std::size_t i = 0; i < totalFrames; ++i)
		{
			QVERIFY2(out[i].left() == testValue(i),
				qPrintable(QString("frame %1 out of order after the SPSC handoff").arg(i)));
		}
	}

	// -----------------------------------------------------------------------
	// D9c: the audio-thread entry point of the clip record path allocates
	//      nothing and never grows without bound.
	// -----------------------------------------------------------------------
	void accumulatorAppendPerformsNoAllocation()
	{
		std::vector<SampleFrame> block;
		for (std::size_t i = 0; i < 256; ++i) { block.push_back(frameAt(i)); }

		// Built before the probe is armed: the single ring allocation and the
		// drain thread belong to construction, not to the audio-thread path.
		SampleRecordAccumulator accum{48000};

		// A burst that fits in the ring, so this measures the append path and
		// not the drop policy (which the ring tests above cover).
		constexpr std::size_t blocks = 32;
		const auto expected = static_cast<std::uint64_t>(blocks) * block.size();
		QVERIFY(expected <= SampleRecordAccumulator::DefaultRingFrames);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (std::size_t i = 0; i < blocks; ++i)
		{
			accum.append(block.data(), static_cast<f_cnt_t>(block.size()));
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
		QCOMPARE(accum.framesStaged(), expected);
	}

	//! The take the drain thread assembles is complete, ordered and carries the
	//! recording sample rate - i.e. moving the work off the audio thread did
	//! not change what is recorded.
	void accumulatorTakeIsCompleteAndOrdered()
	{
		constexpr sample_rate_t rate = 44100;
		constexpr std::size_t blockFrames = 256;
		// Exactly one ring full, so it all fits and nothing is dropped.
		constexpr std::size_t blocks = SampleRecordAccumulator::DefaultRingFrames / blockFrames;

		SampleRecordAccumulator accum{rate, SampleRecordAccumulator::DefaultRingFrames};
		std::vector<SampleFrame> block(blockFrames);
		for (std::size_t b = 0; b < blocks; ++b)
		{
			for (std::size_t i = 0; i < blockFrames; ++i)
			{
				block[i] = frameAt(b * blockFrames + i);
			}
			accum.append(block.data(), static_cast<f_cnt_t>(blockFrames));
		}

		auto take = accum.finish();
		QVERIFY2(take != nullptr, "the accumulator produced no take");
		const auto expected = blocks * blockFrames;
		QCOMPARE(static_cast<qulonglong>(take->size()), static_cast<qulonglong>(expected));
		QCOMPARE(static_cast<int>(take->sampleRate()), static_cast<int>(rate));
		QCOMPARE(static_cast<qulonglong>(accum.framesDropped()), static_cast<qulonglong>(0));
		for (std::size_t i = 0; i < expected; ++i)
		{
			QVERIFY2(take->data()[i].left() == testValue(i),
				qPrintable(QString("frame %1 out of order in the assembled take").arg(i)));
		}
	}

	// -----------------------------------------------------------------------
	// D9b: the capture thread must not block behind the model lock.
	// -----------------------------------------------------------------------
	void pushInputFramesDoesNotBlockOnTheModelLock()
	{
		auto* engine = lmms::Engine::audioEngine();
		QVERIFY(engine != nullptr);

		// Hold the model lock (what a GUI thread does) for a bounded time on
		// another thread, so the measurement below cannot deadlock whatever the
		// result.
		constexpr int holdMs = 600;
		std::atomic<bool> holding{false};
		std::thread holder([&] {
			auto guard = engine->requestChangesGuard();
			holding.store(true, std::memory_order_release);
			std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
		});
		while (!holding.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		std::vector<SampleFrame> block(256, SampleFrame(0.25f, -0.25f));
		const auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < 64; ++i)
		{
			engine->pushInputFrames(block.data(), static_cast<f_cnt_t>(block.size()));
		}
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		holder.join();

		// With requestChangeInModel() still in pushInputFrames() this loop
		// blocks for the whole holdMs, because m_changeMutex is held by the
		// other thread. Without it the 64 pushes complete immediately.
		QVERIFY2(elapsedMs < holdMs / 2,
			qPrintable(QString("pushInputFrames took %1 ms while the model lock was held "
				"for %2 ms: the capture thread is still blocking on m_changeMutex")
				.arg(elapsedMs).arg(holdMs)));
	}

	//! D9a: a stopped device keeps pushing input, but the input path must not
	//! grow without bound. Every frame is either staged or counted as dropped,
	//! and the ring never holds more than its fixed capacity - a million
	//! offered frames do not become a million frames of storage.
	void inputStagingIsBoundedWhileTheDeviceIsStopped()
	{
		auto* engine = lmms::Engine::audioEngine();
		QVERIFY(engine != nullptr);

		constexpr std::size_t blockFrames = 1024;
		constexpr std::size_t pushes = 1000;
		std::vector<SampleFrame> block(blockFrames, SampleFrame(0.25f, -0.25f));

		// Relative to whatever the ring already holds: earlier cases in this
		// suite push into the same engine instance.
		const auto stagedBefore = static_cast<std::uint64_t>(engine->inputFramesStaged());
		const auto droppedBefore = engine->inputFramesDropped();
		for (std::size_t i = 0; i < pushes; ++i)
		{
			engine->pushInputFrames(block.data(), static_cast<f_cnt_t>(blockFrames));
		}
		const auto stagedAfter = static_cast<std::uint64_t>(engine->inputFramesStaged());
		const auto dropped = engine->inputFramesDropped() - droppedBefore;
		const auto total = static_cast<std::uint64_t>(pushes) * blockFrames;

		qInfo("pushed %llu frames: staged %llu -> %llu, dropped %llu",
			static_cast<unsigned long long>(total),
			static_cast<unsigned long long>(stagedBefore),
			static_cast<unsigned long long>(stagedAfter),
			static_cast<unsigned long long>(dropped));

		// Bounded: the capture path holds at most one fixed-capacity ring,
		// whatever is offered. The pre-fix code doubled its input buffer and
		// kept every frame, so this was unbounded growth.
		QVERIFY2(stagedAfter <= static_cast<std::uint64_t>(AudioEngine::InputStageCapacityFrames),
			"the input path holds more than its fixed staging capacity");
		QVERIFY2(dropped > 0, "the ring reported no overflow although far more was pushed than fits");
		// Conservation: no frame vanished silently, and none was stored.
		QCOMPARE(stagedAfter - stagedBefore + dropped, total);
	}
} ;


QTEST_MAIN(RecordingRealtimeTest)

#include "RecordingRealtimeTest.moc"
