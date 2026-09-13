/*
 * RetroMidiRingTest.cpp - invariants of the bounded, event-typed ring behind
 *                         retrospective MIDI capture (owner item 14)
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

#include "RetroMidiRing.h"

#include <QtTest>

#include <cstdint>
#include <thread>
#include <vector>

#include "Midi.h"
#include "AllocationProbe.h"

using lmms::RetroMidiEvent;
using lmms::RetroMidiRing;

namespace
{

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

//! A deterministic event whose fields encode \a index, so a copied window can
//! be checked for content and order at once.
RetroMidiEvent testEvent(std::uint64_t index)
{
	RetroMidiEvent event{};
	event.tick = static_cast<std::uint32_t>(index);
	event.source = static_cast<std::uint16_t>(index % 7);
	event.type = lmms::MidiNoteOn;
	event.channel = static_cast<std::uint8_t>(index % 16);
	event.param1 = static_cast<std::uint8_t>(index % 61);
	event.param2 = static_cast<std::uint8_t>((index * 7) % 128);
	return event;
}

bool sameEvent(const RetroMidiEvent& left, const RetroMidiEvent& right)
{
	return left.tick == right.tick && left.source == right.source && left.type == right.type
			&& left.channel == right.channel && left.param1 == right.param1
			&& left.param2 == right.param2 && left.flags == right.flags;
}

} // namespace


class RetroMidiRingTest : public QObject
{
	Q_OBJECT

private slots:
	//! Capacity is rounded up to a power of two and never below the request.
	void Capacity_IsPowerOfTwoAndAtLeastRequested()
	{
		RetroMidiRing small{1000};
		QCOMPARE(asNumber(small.capacity()), asNumber(1024));
		QVERIFY(small.capacity() >= 1000);
		QVERIFY((small.capacity() & (small.capacity() - 1)) == 0);

		RetroMidiRing exact{1u << 16};
		QCOMPARE(asNumber(exact.capacity()), asNumber(1u << 16));

		RetroMidiRing degenerate{0};
		QCOMPARE(asNumber(degenerate.capacity()), asNumber(1));
	}

	//! A fresh ring holds nothing and reports no loss.
	void Empty_OnConstruction()
	{
		RetroMidiRing ring{64};
		QCOMPARE(asNumber(ring.bufferedCount()), asNumber(0));
		QCOMPARE(asNumber(ring.overwrittenCount()), asNumber(0));
		QCOMPARE(asNumber(ring.pausedDropCount()), asNumber(0));

		RetroMidiEvent out = testEvent(999);
		QCOMPARE(asNumber(ring.copyOut(&out, 1)), asNumber(0));
		QCOMPARE(static_cast<unsigned>(out.tick), 999u);  // untouched
	}

	//! The slot is exactly the 16 bytes the capacity budget is written in.
	void Element_IsSixteenBytes()
	{
		QCOMPARE(sizeof(RetroMidiEvent), static_cast<std::size_t>(16));
	}

	//! Filling past capacity keeps the LAST N events, in arrival order.
	void Full_KeepsTheMostRecentWindowAndCountsTheRest()
	{
		constexpr std::size_t capacity = 64;
		RetroMidiRing ring{capacity};

		for (std::uint64_t i = 0; i < 100; ++i)
		{
			QVERIFY(ring.push(testEvent(i)));
		}

		QCOMPARE(asNumber(ring.bufferedCount()), asNumber(capacity));
		QCOMPARE(asNumber(ring.overwrittenCount()), asNumber(36));  // 100 - 64

		std::vector<RetroMidiEvent> out(capacity);
		QCOMPARE(asNumber(ring.copyOut(out.data(), out.size())), asNumber(capacity));
		for (std::size_t i = 0; i < capacity; ++i)
		{
			QVERIFY(sameEvent(out[i], testEvent(36 + i)));
		}
	}

	//! copyOut() is clamped by the caller's buffer and reports honestly.
	void CopyOut_IsClampedByTheBufferTheCallerOffers()
	{
		RetroMidiRing ring{64};
		for (std::uint64_t i = 0; i < 10; ++i) { QVERIFY(ring.push(testEvent(i))); }

		std::vector<RetroMidiEvent> out(4);
		QCOMPARE(asNumber(ring.copyOut(out.data(), out.size())), asNumber(4));
		for (std::size_t i = 0; i < out.size(); ++i) { QVERIFY(sameEvent(out[i], testEvent(i))); }

		QCOMPARE(asNumber(ring.copyOut(nullptr, 4)), asNumber(0));
		QCOMPARE(asNumber(ring.copyOut(out.data(), 0)), asNumber(0));
	}

	//! Many wraps do not disturb the order of the retained window.
	void WrapAround_PreservesArrivalOrder()
	{
		constexpr std::size_t capacity = 64;
		RetroMidiRing ring{capacity};

		std::vector<RetroMidiEvent> out(capacity);
		for (std::uint64_t round = 0; round < 20; ++round)
		{
			for (std::uint64_t i = 0; i < 50; ++i)
			{
				QVERIFY(ring.push(testEvent(round * 50 + i)));
			}

			const std::uint64_t pushed = round * 50 + 50;
			const std::size_t retained = pushed < capacity ? pushed : capacity;
			QCOMPARE(asNumber(ring.copyOut(out.data(), out.size())), asNumber(retained));
			// The retained window ends at the event just pushed.
			const std::uint64_t last = pushed - 1;
			for (std::size_t i = 0; i < retained; ++i)
			{
				QVERIFY(sameEvent(out[i], testEvent(last - retained + 1 + i)));
			}
		}
		QCOMPARE(asNumber(ring.overwrittenCount()), asNumber(20 * 50 - capacity));
	}

	//! A snapshot request stops the producer, is acknowledged by the next push,
	//! and the window the snapshot sees is the one from before the request.
	void Snapshot_PausesTheProducerAndCountsWhatItDropped()
	{
		RetroMidiRing ring{64};
		for (std::uint64_t i = 0; i < 8; ++i) { QVERIFY(ring.push(testEvent(i))); }

		ring.beginSnapshot();
		QVERIFY(!ring.writerIdle());  // the producer has not looked yet

		QVERIFY(!ring.push(testEvent(8)));  // dropped, and that acknowledges the snapshot
		QVERIFY(ring.writerIdle());
		QCOMPARE(asNumber(ring.pausedDropCount()), asNumber(1));

		std::vector<RetroMidiEvent> out(64);
		QCOMPARE(asNumber(ring.copyOut(out.data(), out.size())), asNumber(8));
		for (std::size_t i = 0; i < 8; ++i) { QVERIFY(sameEvent(out[i], testEvent(i))); }
		QCOMPARE(asNumber(ring.overwrittenCount()), asNumber(0));  // nothing overlapped

		ring.endSnapshot();
		QVERIFY(!ring.writerIdle());
		QVERIFY(ring.push(testEvent(9)));  // the producer is running again
		QCOMPARE(asNumber(ring.bufferedCount()), asNumber(9));
	}

	//! copyOut() without a snapshot is still safe: it is the producer that is
	//! moving, not the window, and a torn copy is refused rather than returned.
	void CopyOut_WithoutASnapshotIsSafeWhileTheProducerRuns()
	{
		constexpr std::uint64_t totalEvents = 200000;
		// Capacity exceeds the total, so the window holds every event and the
		// check below is deterministic (nothing is ever overlapped).
		RetroMidiRing ring{1u << 18};

		std::atomic<std::uint64_t> pushed{0};
		std::thread producer([&ring, &pushed] {
			for (std::uint64_t i = 0; i < totalEvents; ++i)
			{
				// The push must NOT sit inside Q_ASSERT: under -DNDEBUG - the
				// release configuration ctest runs this suite in (RelWithDebInfo,
				// unlike the Debug build this test was written in) - Q_ASSERT
				// compiles its argument AWAY, so the producer pushed nothing and
				// the loop below spun until its 300 s timeout instead of failing
				// fast. Measured on 030/retro-capture: 8 slots passed and this one
				// aborted with "Test function timed out". The assertion the author
				// wanted is a QCOMPARE on the count, after the join.
				if (ring.push(testEvent(i))) { pushed.fetch_add(1, std::memory_order_relaxed); }
				if ((i & 0xFFu) == 0) { std::this_thread::yield(); }
			}
		});

		std::vector<RetroMidiEvent> window(1u << 18);
		std::uint64_t received = 0;
		bool sequenceOk = true;
		while (received < totalEvents)
		{
			const auto count = ring.copyOut(window.data(), window.size());
			if (count > received)
			{
				for (std::uint64_t i = received; i < count; ++i)
				{
					if (!sameEvent(window[static_cast<std::size_t>(i)], testEvent(i)))
					{
						sequenceOk = false;
					}
				}
				received = count;
			}
			if (count <= received) { std::this_thread::yield(); }
		}
		producer.join();

		QCOMPARE(static_cast<qulonglong>(pushed.load()), static_cast<qulonglong>(totalEvents));
		QVERIFY(sequenceOk);
		QCOMPARE(static_cast<qulonglong>(received), static_cast<qulonglong>(totalEvents));
		QCOMPARE(asNumber(ring.overwrittenCount()), asNumber(0));
		QCOMPARE(asNumber(ring.pausedDropCount()), asNumber(0));
		// A copy that overlaps a producer write is refused and counted rather
		// than returned torn, so this counter may be non-zero here - it is the
		// honest report of the copy that had to be retried, and it is why the
		// snapshot handshake below exists.
		qInfo("copyOut refusals against a saturated producer: %llu",
				static_cast<unsigned long long>(ring.refusedSnapshots()));
	}

	//! With the producer paused by a snapshot, the copy is taken once, with no
	//! refusal, and the window it returns is coherent and in arrival order.
	void Snapshot_ThenCopyIsTakenWithoutRefusalWhileTheProducerRuns()
	{
		RetroMidiRing ring{1u << 16};

		std::atomic<bool> stop{false};
		std::thread producer([&ring, &stop] {
			for (std::uint64_t i = 0; !stop.load(std::memory_order_relaxed); ++i)
			{
				ring.push(testEvent(i));
				if ((i & 0xFFu) == 0) { std::this_thread::yield(); }
			}
		});

		// A producer that is definitely running, with something to copy.
		while (ring.bufferedCount() == 0) { std::this_thread::yield(); }

		ring.beginSnapshot();
		for (int spin = 0; spin < 1000000 && !ring.writerIdle(); ++spin) { std::this_thread::yield(); }
		QVERIFY(ring.writerIdle());

		std::vector<RetroMidiEvent> window(1u << 16);
		const auto count = ring.copyOut(window.data(), window.size());
		ring.endSnapshot();
		stop.store(true, std::memory_order_relaxed);
		producer.join();

		QVERIFY(count > 0);
		QCOMPARE(asNumber(ring.refusedSnapshots()), asNumber(0));
		for (std::size_t i = 0; i < count; ++i)
		{
			// Arrival order, and each slot holds the event its own tick encodes.
			QCOMPARE(static_cast<qulonglong>(window[i].tick),
					static_cast<qulonglong>(window[0].tick + i));
			QVERIFY(sameEvent(window[i], testEvent(window[i].tick)));
		}
	}

	//! The producer path must not allocate (dynamic check on this thread).
	void ProducerPath_DoesNotAllocate()
	{
		RetroMidiRing ring{1u << 16};
		const auto event = testEvent(1);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i) { ring.push(event); }
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
	}

	//! The consumer path must not allocate either - the caller's buffer is the
	//! caller's memory.
	void SnapshotPath_DoesNotAllocate()
	{
		RetroMidiRing ring{64};
		for (std::uint64_t i = 0; i < 32; ++i) { ring.push(testEvent(i)); }
		std::vector<RetroMidiEvent> out(64);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 100; ++i)
		{
			ring.beginSnapshot();
			ring.copyOut(out.data(), out.size());
			ring.endSnapshot();
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
	}
};


QTEST_APPLESS_MAIN(RetroMidiRingTest)

#include "RetroMidiRingTest.moc"
