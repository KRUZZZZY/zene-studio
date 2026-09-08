/*
 * RecordRingBufferTest.cpp - invariants of the lock-free SPSC ring buffer used
 *                            by the two-track recording prototype (task #556)
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

#include "RecordRingBuffer.h"

#include <QtTest>

#include <thread>
#include <vector>

#include "AllocationProbe.h"

using lmms::RecordRingBuffer;
using lmms::sample_t;

namespace
{

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

// Exact float values (multiples of 1/64) so comparisons are exact.
sample_t testValue(std::uint64_t index)
{
	return static_cast<sample_t>(static_cast<long long>(index % 61) - 30) / 64.0f;
}

} // namespace


class RecordRingBufferTest : public QObject
{
	Q_OBJECT

private slots:
	//! Capacity is rounded up to a power of two and never below the request.
	void Capacity_IsPowerOfTwoAndAtLeastRequested()
	{
		RecordRingBuffer small{1000};
		QCOMPARE(asNumber(small.capacity()), asNumber(1024));
		QVERIFY(small.capacity() >= 1000);
		QVERIFY((small.capacity() & (small.capacity() - 1)) == 0);

		RecordRingBuffer exact{1u << 16};
		QCOMPARE(asNumber(exact.capacity()), asNumber(1u << 16));
	}

	//! A fresh ring reports no data and read() returns 0.
	void Empty_OnConstruction()
	{
		RecordRingBuffer ring{128};
		QCOMPARE(asNumber(ring.available()), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));

		sample_t out = -1.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(0));
		QCOMPARE(out, -1.f);
	}

	//! A single frame round-trips unchanged.
	void WriteRead_SingleFrame()
	{
		RecordRingBuffer ring{128};
		QVERIFY(ring.write(0.25f));
		QCOMPARE(asNumber(ring.available()), asNumber(1));

		sample_t out = 0.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(1));
		QCOMPARE(out, 0.25f);
		QCOMPARE(asNumber(ring.available()), asNumber(0));
	}

	//! A block round-trips in order, without loss or duplication.
	void WriteRead_BlockPreservesOrderAndValues()
	{
		constexpr std::size_t frames = 1000;
		RecordRingBuffer ring{2048};

		std::vector<sample_t> in(frames);
		for (std::size_t i = 0; i < frames; ++i) { in[i] = testValue(i); }
		QCOMPARE(asNumber(ring.writeBlock(in.data(), frames)), asNumber(frames));

		std::vector<sample_t> out(frames, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), frames)), asNumber(frames));
		for (std::size_t i = 0; i < frames; ++i) { QCOMPARE(out[i], in[i]); }
	}

	//! A full ring drops the newest frames and counts them; old data survives.
	void Full_DropsNewestAndCountsOverflow()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> in(64);
		for (std::size_t i = 0; i < 64; ++i) { in[i] = testValue(i); }
		QCOMPARE(asNumber(ring.writeBlock(in.data(), 64)), asNumber(64));
		QCOMPARE(asNumber(ring.available()), asNumber(64));

		std::vector<sample_t> extra(10, 9.f);
		QCOMPARE(asNumber(ring.writeBlock(extra.data(), 10)), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(10));
		QVERIFY(!ring.write(9.f));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(11));

		// The 64 original frames are intact and in order.
		std::vector<sample_t> out(64, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), 64)), asNumber(64));
		for (std::size_t i = 0; i < 64; ++i) { QCOMPARE(out[i], in[i]); }
	}

	//! Reading and writing across the wrap boundary preserves order.
	void WrapAround_PreservesOrder()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> out(64);
		std::uint64_t produced = 0;

		for (int round = 0; round < 20; ++round)
		{
			std::vector<sample_t> in(50);
			for (std::size_t i = 0; i < in.size(); ++i) { in[i] = testValue(produced + i); }
			QCOMPARE(asNumber(ring.writeBlock(in.data(), in.size())), asNumber(in.size()));
			produced += in.size();

			const auto got = ring.read(out.data(), 50);
			QCOMPARE(asNumber(got), asNumber(50));
			for (std::size_t i = 0; i < got; ++i)
			{
				QCOMPARE(out[i], testValue(produced - 50 + i));
			}
		}
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));
	}

	//! writeStrided() demuxes one channel of an interleaved buffer.
	void WriteStrided_DemuxesOneChannel()
	{
		RecordRingBuffer ring{64};
		// interleaved stereo: [l0 r0 l1 r1 ...]
		std::vector<sample_t> interleaved(32);
		for (std::size_t frame = 0; frame < 16; ++frame)
		{
			interleaved[2 * frame] = testValue(frame);        // left
			interleaved[2 * frame + 1] = -testValue(frame);   // right
		}

		QCOMPARE(asNumber(ring.writeStrided(&interleaved[1], 2, 16)), asNumber(16));
		std::vector<sample_t> out(16, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), 16)), asNumber(16));
		for (std::size_t frame = 0; frame < 16; ++frame)
		{
			QCOMPARE(out[frame], -testValue(frame));
		}
	}

	//! reset() discards buffered frames and clears the overflow counter.
	void Reset_DiscardsFramesAndClearsOverflow()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> in(64, 0.5f);
		ring.writeBlock(in.data(), 64);
		ring.write(0.5f);
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(1));

		ring.reset();
		QCOMPARE(asNumber(ring.available()), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));

		QVERIFY(ring.write(0.5f));
		sample_t out = 0.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(1));
		QCOMPARE(out, 0.5f);
	}

	//! Producer and consumer threads: no frame is lost, duplicated or reordered.
	void ConcurrentProducerConsumer_NoLossNoDuplication()
	{
		constexpr std::uint64_t totalFrames = 1000000;
		// Capacity exceeds the total so the test is deterministic (no overflow).
		RecordRingBuffer ring{1u << 20};

		std::thread producer([&ring] {
			std::vector<sample_t> block(512);
			for (std::uint64_t pos = 0; pos < totalFrames; pos += block.size())
			{
				const auto frames = static_cast<std::size_t>(
					std::min<std::uint64_t>(block.size(), totalFrames - pos));
				for (std::size_t i = 0; i < frames; ++i)
				{
					block[i] = static_cast<sample_t>(pos + i); // exact below 2^24
				}
				const auto written = ring.writeBlock(block.data(), frames);
				Q_ASSERT(written == frames);
				std::this_thread::yield();
			}
		});

		std::vector<sample_t> out(1024);
		std::uint64_t received = 0;
		bool sequenceOk = true;
		while (received < totalFrames)
		{
			const auto frames = ring.read(out.data(), out.size());
			for (std::size_t i = 0; i < frames; ++i)
			{
				if (out[i] != static_cast<sample_t>(received + i)) { sequenceOk = false; }
			}
			received += frames;
			if (frames == 0) { std::this_thread::yield(); }
		}
		producer.join();

		QVERIFY(sequenceOk);
		QCOMPARE(asNumber(received), asNumber(totalFrames));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));
	}

	//! The producer path must not allocate (dynamic check on this thread).
	void ProducerPath_DoesNotAllocate()
	{
		RecordRingBuffer ring{1u << 16};
		std::vector<sample_t> block(4096, 0.5f); // allocated before counting

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			ring.writeBlock(block.data(), block.size());
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
	}
} ;


QTEST_APPLESS_MAIN(RecordRingBufferTest)

#include "RecordRingBufferTest.moc"
