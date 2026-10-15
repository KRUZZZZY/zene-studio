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
#include <fstream>
#include <sstream>
#include <string>
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
	//! Round 4: pop() is single-consumer and wait-free. Its body must have no loop, no retry and no compare-exchange.
	void popHasNoRetryLoop()
	{
		std::ifstream file(MIDI_HANDOFF_QUEUE_HEADER);
		QVERIFY2(file.good(), "could not read include/MidiHandoffQueue.h");
		std::stringstream buffer;
		buffer << file.rdbuf();
		const std::string text = buffer.str();
		const auto start = text.find("auto pop(HandoffMidiEvent& event) noexcept -> bool");
		QVERIFY(start != std::string::npos);
		const auto open = text.find('{', start);
		QVERIFY(open != std::string::npos);
		std::size_t depth = 0;
		std::size_t end = open;
		for (; end < text.size(); ++end)
		{
			if (text[end] == '{') { ++depth; }
			if (text[end] == '}' && --depth == 0) { break; }
		}
		const std::string body = text.substr(open, end - open + 1);
		for (const char* forbidden : {"for (", "for(", "while", "do {", "goto", "compare_exchange", "fetch_", "exchange("})
		{
			QVERIFY2(body.find(forbidden) == std::string::npos, forbidden);
		}
	}

	//! Round 4: a refusal whose stamp update exhausted its budget is deferred to the consumer, and the cut
	//! comes after every event that was queued before the refusal.
	void exhaustedStampUpdateDefersTheCutBehindQueuedEvents()
	{
		lmms::MidiHandoffQueue<8> queue;
		lmms::MidiChannelOverflow overflow;
		auto event = [](int channel, std::uint32_t id) {
			lmms::HandoffMidiEvent e;
			e.channel = static_cast<std::int8_t>(channel);
			e.offset = id;
			return e;
		};
		for (std::uint32_t id : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) { QVERIFY(queue.push(event(3, id))); }
		std::uint32_t pushAttempts = 0;
		std::uint32_t attempts = 0;
		QVERIFY(!lmms::pushOrRecordWithTestHooks(queue, overflow, event(3, 99), &pushAttempts, &attempts,
			[](std::uint32_t) {},
			[](std::atomic<std::uint64_t>& stamp, std::uint64_t expected, std::uint64_t requested, std::uint32_t)
			{
				if (expected + 1 < requested) { stamp.compare_exchange_strong(expected, expected + 1); }
			}));
		QCOMPARE(attempts, lmms::MidiChannelOverflow::MaxStampUpdateAttempts);
		QCOMPARE(overflow.failedStampUpdates(), std::uint64_t{1});

		std::vector<std::string> sent;
		auto emitEvent = [&sent](const lmms::HandoffMidiEvent& e) { sent.push_back("E" + std::to_string(e.offset)); return true; };
		auto emitCut = [&sent](int channel) { sent.push_back("CUT" + std::to_string(channel)); return true; };
		lmms::drainMidi(queue, overflow, 10, emitEvent, emitCut);
		// One cut per channel, at the later (deferred) stamp: after the last queued event, never before it.
		const std::vector<std::string> expected{"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "CUT3"};
		QCOMPARE(sent, expected);
		QCOMPARE(overflow.stamp(3), std::uint64_t{0});
		QCOMPARE(overflow.recorded(), std::uint64_t{1});
	}

	//! Round 6: a producer raises the shared stamp during the cut's emit. A value covered by the cut (<= its
	//! position) must not cause a second cut; a value beyond it keeps its own later cut.
	void stampRaisedDuringTheCutEmitIsCoveredOrKept()
	{
		for (const bool beyond : {false, true})
		{
			lmms::MidiHandoffQueue<8> queue;
			lmms::MidiChannelOverflow overflow;
			lmms::HandoffMidiEvent e;
			e.channel = 3;
			for (std::uint32_t id = 1; id <= 8; ++id) { e.offset = id; QVERIFY(queue.push(e)); }
			std::uint32_t pushAttempts = 0;
			std::uint32_t stampAttempts = 0;
			e.offset = 99;
			QVERIFY(!lmms::pushOrRecordWithTestHooks(queue, overflow, e, &pushAttempts, &stampAttempts,
				[](std::uint32_t) {},
				[](std::atomic<std::uint64_t>& stamp, std::uint64_t expected, std::uint64_t requested, std::uint32_t)
				{
					if (expected + 1 < requested) { stamp.compare_exchange_strong(expected, expected + 1); }
				}));
			// shared stamp is 8, deferred stamp will be 9: the cut is placed at 9.
			std::vector<std::string> sent;
			bool raised = false;
			auto emitEvent = [&sent](const lmms::HandoffMidiEvent& ev) { sent.push_back("E" + std::to_string(ev.offset)); return true; };
			auto emitCut = [&](int channel)
			{
				sent.push_back("CUT" + std::to_string(channel));
				if (!raised)
				{
					raised = true;
					overflow.record(channel, beyond ? 9 : 8); // stamp 10 (beyond the cut at 9) or 9 (covered)
				}
				return true;
			};
			lmms::drainMidi(queue, overflow, 10, emitEvent, emitCut);
			e.offset = 100;
			QVERIFY(queue.push(e));
			lmms::drainMidi(queue, overflow, 10, emitEvent, emitCut);
			std::vector<std::string> expected{"E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "CUT3"};
			if (beyond) { expected.push_back("E100"); expected.push_back("CUT3"); }
			else { expected.push_back("E100"); }
			QCOMPARE(sent, expected);
			QCOMPARE(overflow.stamp(3), std::uint64_t{0});
			QCOMPARE(overflow.unclearedCoveredStamps(), std::uint64_t{0});
		}
	}

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

	void pushRetriesAreBoundedWhenContended()
	{
		lmms::MidiHandoffQueue<64> queue;
		std::uint32_t competingReservations = 0;
		bool competitorsAccepted = true;
		const auto scheduleCompetingReservation = [&](std::uint32_t)
		{
			if (!queue.push(eventWithOffset(competingReservations))) { competitorsAccepted = false; }
			else { ++competingReservations; }
		};
		auto refusedEvent = eventWithOffset(999);
		refusedEvent.channel = 5;
		std::uint64_t refusedAt = 0;
		std::uint32_t attemptsUsed = 0;
		const bool accepted = queue.pushWithTestHook(refusedEvent, &refusedAt, &attemptsUsed,
			scheduleCompetingReservation);

		QVERIFY(!accepted);
		QVERIFY(competitorsAccepted);
		QCOMPARE(competingReservations, lmms::MidiHandoffQueue<64>::MaxPushAttempts);
		QCOMPARE(attemptsUsed, lmms::MidiHandoffQueue<64>::MaxPushAttempts);
		QVERIFY(attemptsUsed <= lmms::MidiHandoffQueue<64>::MaxPushAttempts);
		QCOMPARE(queue.dropped(), std::uint64_t{1});

		for (std::uint32_t offset = 0; offset < competingReservations; ++offset)
		{
			lmms::HandoffMidiEvent event;
			QVERIFY(queue.pop(event));
			QCOMPARE(event.offset, offset);
		}
		lmms::HandoffMidiEvent empty;
		QVERIFY(!queue.pop(empty));
	}

	//! A refused event is never silently lost: its channel is recorded, with the queue position it was refused at.
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
		// Both refusals came after two queued events: their cuts are due once two events have been taken.
		QVERIFY(!overflow.due(3, 1));
		QVERIFY(overflow.due(3, 2));
		QVERIFY(overflow.due(9, 2)); // the no-channel refusal affects every channel
	}

//! The cut for a refusal goes exactly at the refusal: after the events queued before it, before the events queued
	//! after it. The sequence is recorded by the emitters.
	void drainSendsTheCutAtTheRefusalPoint()
	{
		lmms::MidiHandoffQueue<4> queue;
		lmms::MidiChannelOverflow overflow;
		auto event = [](int channel, std::uint32_t id) {
			lmms::HandoffMidiEvent e;
			e.channel = static_cast<std::int8_t>(channel);
			e.offset = id;
			return e;
		};

		// A, B, C, D fill the queue; a note-off on channel 3 (N) is refused after them.
		for (std::uint32_t id : {1u, 2u, 3u, 4u}) { QVERIFY(lmms::pushOrRecord(queue, overflow, event(0, id))); }
		QVERIFY(!lmms::pushOrRecord(queue, overflow, event(3, 99)));

		std::vector<std::string> sent;
		auto emitEvent = [&sent](const lmms::HandoffMidiEvent& e) { sent.push_back("E" + std::to_string(e.offset)); return true; };
		auto emitCut = [&sent](int channel) { sent.push_back("CUT" + std::to_string(channel)); return true; };

		lmms::drainMidi(queue, overflow, 2, emitEvent, emitCut);       // A, B
		QVERIFY(lmms::pushOrRecord(queue, overflow, event(0, 5)));      // E, queued after N
		lmms::drainMidi(queue, overflow, 10, emitEvent, emitCut);      // C, D, then the cut, then E

		const std::vector<std::string> expected{"E1", "E2", "E3", "E4", "CUT3", "E5"};
		QCOMPARE(sent, expected);
		QCOMPARE(overflow.stamp(3), std::uint64_t{0});
	}

	void drainTakesAtMostTheCapPerCall()
	{
		lmms::MidiHandoffQueue<16> queue;
		lmms::MidiChannelOverflow overflow;
		for (std::uint32_t id = 0; id < 10; ++id) { QVERIFY(queue.push(eventWithOffset(id))); }

		std::size_t emitted = 0;
		lmms::drainMidi(queue, overflow, 3,
			[&emitted](const lmms::HandoffMidiEvent&) { ++emitted; return true; },
			[](int) { return true; });
		QCOMPARE(emitted, std::size_t{3});
		QCOMPARE(queue.consumed(), std::uint64_t{3});  // the rest stays queued
	}

	//! Overflow cuts share the fixed native event budget with queue events; a limited drain leaves the rest queued.
	void drainCapsEmittedEventsIncludingCuts()
	{
		lmms::MidiHandoffQueue<2> queue;
		lmms::MidiChannelOverflow overflow;
		auto event = [](std::uint32_t id)
		{
			lmms::HandoffMidiEvent queued;
			queued.offset = id;
			queued.channel = 3;
			return queued;
		};
		QVERIFY(lmms::pushOrRecord(queue, overflow, event(1)));
		QVERIFY(lmms::pushOrRecord(queue, overflow, event(2)));
		QVERIFY(!lmms::pushOrRecord(queue, overflow, event(99)));
		lmms::HandoffMidiEvent removed;
		QVERIFY(queue.pop(removed));
		QVERIFY(queue.pop(removed));
		QVERIFY(lmms::pushOrRecord(queue, overflow, event(3)));
		QVERIFY(lmms::pushOrRecord(queue, overflow, event(4)));

		std::vector<std::uint32_t> delivered;
		std::vector<int> cuts;
		lmms::drainMidi(queue, overflow, 2,
			[&](const lmms::HandoffMidiEvent& queued) { delivered.push_back(queued.offset); return true; },
			[&](int channel) { cuts.push_back(channel); return true; }, 2);

		QCOMPARE(cuts, std::vector<int>{3});
		QCOMPARE(delivered, std::vector<std::uint32_t>{3});
		QCOMPARE(queue.consumed(), std::uint64_t{3});
		QCOMPARE(overflow.stamp(3), std::uint64_t{0});
		lmms::HandoffMidiEvent pending;
		QVERIFY(queue.pop(pending));
		QCOMPARE(pending.offset, std::uint32_t{4});
	}

	//! When a cut cannot be sent, the drain stops and the cut stays due for the next call.
	void drainKeepsACutThatCouldNotBeSent()
	{
		lmms::MidiHandoffQueue<4> queue;
		lmms::MidiChannelOverflow overflow;
		for (std::uint32_t id = 0; id < 4; ++id) { QVERIFY(queue.push(eventWithOffset(id))); }
		QVERIFY(!lmms::pushOrRecord(queue, overflow, eventWithOffset(9)));
		const auto stamp = overflow.stamp(0);

		std::size_t cuts = 0;
		auto refuseCuts = [&cuts](int) { ++cuts; return false; };
		lmms::drainMidi(queue, overflow, 10, [](const lmms::HandoffMidiEvent&) { return true; }, refuseCuts);
		QVERIFY(cuts >= 1);
		QCOMPARE(overflow.stamp(0), stamp);   // still pending
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
