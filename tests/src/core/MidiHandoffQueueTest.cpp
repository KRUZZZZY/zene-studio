/*
 * MidiHandoffQueueTest.cpp - the bounded MIDI hand-off queue keeps its contract:
 *                            FIFO order, a full queue drops and counts, the
 *                            realtime calls allocate nothing, and concurrent
 *                            producers lose no event that push() accepted
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

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "MidiHandoffQueue.h"

#include "AllocationProbe.h"

namespace
{

//! The allocations \a body makes on this thread, counted by the probe.
template<typename Body>
auto countAllocations(Body&& body) -> std::uint64_t
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	body();
	lmms::test::tlCountAllocations = false;
	return lmms::test::tlAllocationCount;
}

auto eventWithOffset(std::uint32_t offset) -> lmms::HandoffMidiEvent
{
	lmms::HandoffMidiEvent event;
	event.offset = offset;
	return event;
}

//! Per-producer bookkeeping for the concurrent test: how many events each producer had popped,
//! the offset of the last one, and whether any producer's events came out out of order.
struct Consumed
{
	std::vector<std::uint64_t> popped;
	std::vector<std::int64_t> lastOffset;
	bool ordered = true;
};

//! Pops every event currently in \a queue into \a consumed.
template<std::size_t Capacity>
void drainInto(lmms::MidiHandoffQueue<Capacity>& queue, Consumed& consumed)
{
	lmms::HandoffMidiEvent event;
	while (queue.pop(event))
	{
		const auto p = static_cast<std::size_t>(event.channel);
		++consumed.popped[p];
		consumed.ordered = consumed.ordered && static_cast<std::int64_t>(event.offset) > consumed.lastOffset[p];
		consumed.lastOffset[p] = event.offset;
	}
}

} // namespace

class MidiHandoffQueueTest : public QObject
{
	Q_OBJECT

private slots:
	void keepsOrderAndDropsWhenFull()
	{
		lmms::MidiHandoffQueue<8> queue;

		std::size_t accepted = 0;
		for (std::uint32_t i = 0; i < 10; ++i)
		{
			accepted += queue.push(eventWithOffset(i)) ? 1 : 0;
		}
		QCOMPARE(accepted, std::size_t{8});
		QCOMPARE(queue.dropped(), std::uint64_t{2});

		// The first eight arrivals are the ones kept: the newest were refused.
		for (std::uint32_t i = 0; i < 8; ++i)
		{
			lmms::HandoffMidiEvent event;
			QVERIFY(queue.pop(event));
			QCOMPARE(event.offset, i);
		}
		lmms::HandoffMidiEvent none;
		QVERIFY(!queue.pop(none));

		// Space comes back once popped, and the queue wraps around correctly.
		for (std::uint32_t i = 0; i < 20; ++i)
		{
			QVERIFY(queue.push(eventWithOffset(100 + i)));
			lmms::HandoffMidiEvent event;
			QVERIFY(queue.pop(event));
			QCOMPARE(event.offset, 100 + i);
		}
		QCOMPARE(queue.dropped(), std::uint64_t{2});
	}

	void pushAndPopAllocateNothing()
	{
		lmms::MidiHandoffQueue<64> queue;

		const auto allocations = countAllocations([&] {
			for (std::uint32_t i = 0; i < 200; ++i)
			{
				queue.push(eventWithOffset(i));
				lmms::HandoffMidiEvent event;
				queue.pop(event);
			}
		});
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! A refused event is never silently lost: its channel is recorded and comes out of take(), once.
	void refusedEventsLeaveTheirChannelRecorded()
	{
		lmms::MidiHandoffQueue<2> queue;
		lmms::MidiChannelOverflow overflow;

		lmms::HandoffMidiEvent onChannel5;
		onChannel5.channel = 5;
		QVERIFY(lmms::pushOrRecord(queue, overflow, onChannel5));
		QVERIFY(lmms::pushOrRecord(queue, overflow, onChannel5));

		// The queue is full: a note-off on channel 3 and an event with no channel are refused, and recorded.
		lmms::HandoffMidiEvent noteOff;
		noteOff.type = 0x80;
		noteOff.channel = 3;
		QVERIFY(!lmms::pushOrRecord(queue, overflow, noteOff));
		lmms::HandoffMidiEvent noChannel;
		noChannel.channel = -1;
		QVERIFY(!lmms::pushOrRecord(queue, overflow, noChannel));

		QCOMPARE(overflow.recorded(), std::uint64_t{2});
		QCOMPARE(queue.dropped(), std::uint64_t{2});
		QCOMPARE(overflow.take(), std::uint32_t{0xFFFFu});  // channel 3 plus "all channels", as 16 bits
		QCOMPARE(overflow.take(), std::uint32_t{0});        // taking clears it

		// Channels that could not be handled are put back, and come out again.
		overflow.record(7);
		const auto taken = overflow.take();
		overflow.restore(taken);
		QCOMPARE(overflow.take(), std::uint32_t{1u << 7});
	}

	void concurrentProducersLoseNothingPushAccepted()
	{
		constexpr std::uint32_t producers = 4;
		constexpr std::uint32_t perProducer = 20000;
		lmms::MidiHandoffQueue<64> queue;

		// Each producer tags its events with its own id in `channel`, so the consumer can
		// check that every producer's events arrive in the order it sent them.
		std::vector<std::atomic<std::uint64_t>> acceptedBy(producers);
		std::vector<std::thread> threads;
		std::atomic<std::uint32_t> running{producers};
		for (std::uint32_t p = 0; p < producers; ++p)
		{
			threads.emplace_back([&, p] {
				for (std::uint32_t i = 0; i < perProducer; ++i)
				{
					lmms::HandoffMidiEvent event;
					event.channel = static_cast<std::int8_t>(p);
					event.offset = i;
					if (queue.push(event)) { acceptedBy[p].fetch_add(1); }
				}
				running.fetch_sub(1);
			});
		}

		Consumed consumed{std::vector<std::uint64_t>(producers, 0), std::vector<std::int64_t>(producers, -1)};
		while (running.load() != 0)
		{
			drainInto(queue, consumed);
		}
		for (auto& thread : threads) { thread.join(); }
		// Anything pushed after the loop above saw the queue empty is still in it.
		drainInto(queue, consumed);

		QVERIFY2(consumed.ordered, "events of one producer arrived out of order");
		for (std::uint32_t p = 0; p < producers; ++p)
		{
			QCOMPARE(consumed.popped[p], acceptedBy[p].load());
		}
		std::uint64_t accepted = 0;
		for (auto& count : acceptedBy) { accepted += count.load(); }
		QCOMPARE(accepted + queue.dropped(), std::uint64_t{producers} * perProducer);
	}
};

QTEST_APPLESS_MAIN(MidiHandoffQueueTest)

#include "MidiHandoffQueueTest.moc"
