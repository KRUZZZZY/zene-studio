/*
 * MidiJackOutScheduleTest.cpp - JACK MIDI output written at each message's frame, in time order
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
 *
 */

/*! 0.5.0 W4: MidiJack wrote every queued message at frame 0 of the JACK cycle. It now writes each
 *  at the frame it was produced at, and jack_midi_event_write() takes events only in time order,
 *  so the cycle's list is ordered by frame - stably, so a note-off and a note-on of one key at one
 *  frame stay a retrigger - and a frame past the cycle is bounded to its last frame. No JACK server
 *  is needed: this is MidiJackOutSchedule.h, the part of MidiJack::JackMidiWrite that decides. */

#include <QtTest>

#include <array>
#include <cstdint>
#include <type_traits>

#include "lmmsconfig.h"
#ifdef LMMS_HAVE_JACK
#ifndef LMMS_HAVE_WEAKJACK
#include <jack/jack.h>
#include <jack/midiport.h>
#else
#include <weak_libjack.h>
#endif

#include <QThread>

#include <atomic>

#include "MidiClient.h"
#include "MidiJackOutSchedule.h"
#include "MpscQueue.h"

// Keep the JACK and MIDI dependencies out of the private-access shim below.
#define private public
#include "MidiJack.h"
#undef private
#endif

using namespace lmms;
using midijack::OutMessage;

namespace
{

template <typename Message>
void setCycleIfSupported(Message& message, std::uint64_t cycle)
{
	if constexpr (requires { message.cycle = cycle; }) { message.cycle = cycle; }
}

template <typename Message>
auto scheduledFrame(const Message& message, std::uint64_t cycle, std::uint32_t nframes) -> std::uint32_t
{
	if constexpr (requires { midijack::cycleFrame(message, cycle, nframes); })
	{
		return midijack::cycleFrame(message, cycle, nframes);
	}
	return midijack::cycleFrame(message.frame, nframes);
}

auto message(std::uint8_t status, std::uint8_t key, std::uint32_t frame, std::uint64_t cycle = 0) -> OutMessage
{
	OutMessage result{{status, key, 100}, 3, frame};
	setCycleIfSupported(result, cycle);
	return result;
}

} // namespace

class MidiJackOutScheduleTest : public QObject
{
	Q_OBJECT

private slots:
	//! The shutdown signal shared between the destructor and worker loop must be atomic.
	void quitFlagIsAtomic()
	{
#ifdef LMMS_HAVE_JACK
		QVERIFY((std::is_same_v<decltype(MidiJack::m_quit), std::atomic<bool>>));
#else
		QSKIP("JACK support is disabled");
#endif
	}

	//! Queued out of time order (two senders), drained in time order; equal frames keep their order.
	void messagesAreOrderedByFrameStably()
	{
		MpscQueue<OutMessage, 16> queue;
		QVERIFY(queue.push(message(0x90, 60, 300)));
		QVERIFY(queue.push(message(0x80, 62, 120)));  // note-off of 62 ...
		QVERIFY(queue.push(message(0x90, 64, 0)));
		QVERIFY(queue.push(message(0x90, 62, 120)));  // ... then its retrigger, at the same frame
		std::array<OutMessage, 8> list{};
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{4});
		QCOMPARE(list[0].frame, std::uint32_t{0});
		QCOMPARE(list[1].frame, std::uint32_t{120});
		QCOMPARE(list[1].bytes[0], std::uint8_t{0x80});
		QCOMPARE(list[2].frame, std::uint32_t{120});
		QCOMPARE(list[2].bytes[0], std::uint8_t{0x90});
		QCOMPARE(list[3].frame, std::uint32_t{300});
	}

	//! A cycle takes at most its capacity; the rest wait in the queue, in order, for the next one.
	void aFullCycleLeavesTheRestQueued()
	{
		MpscQueue<OutMessage, 16> queue;
		for (std::uint32_t i = 0; i < 5; ++i) { QVERIFY(queue.push(message(0x90, 60, 40 - i * 10))); }
		std::array<OutMessage, 3> list{};
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{3});
		QCOMPARE(list[0].frame, std::uint32_t{20});
		QCOMPARE(list[2].frame, std::uint32_t{40});
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{2});
		QCOMPARE(list[0].frame, std::uint32_t{0});
		QCOMPARE(list[1].frame, std::uint32_t{10});
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{0});
	}

	//! An overdue event stays before the next cycle, and is moved to that cycle's first frame.
	void deferredMessagesKeepTheirCycleOrder()
	{
		MpscQueue<OutMessage, 16> queue;
		QVERIFY(queue.push(message(0x90, 60, 300, 4)));
		QVERIFY(queue.push(message(0x90, 61, 310, 4)));
		QVERIFY(queue.push(message(0x90, 62, 320, 4)));
		std::array<OutMessage, 2> list{};
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{2});

		QVERIFY(queue.push(message(0x90, 63, 0, 5)));
		QCOMPARE(midijack::drainByFrame(queue, list.data(), list.size()), std::size_t{2});
		QCOMPARE(list[0].bytes[1], std::uint8_t{62});
		QCOMPARE(list[1].bytes[1], std::uint8_t{63});
		QCOMPARE(scheduledFrame(list[0], 5, 256), std::uint32_t{0});
		QCOMPARE(scheduledFrame(list[1], 5, 256), std::uint32_t{0});
	}

	//! A message is written at its own frame - not frame 0 - and one past the cycle at its last frame.
	void aMessageIsWrittenAtItsFrame()
	{
		QCOMPARE(midijack::cycleFrame(0, 256), std::uint32_t{0});
		QCOMPARE(midijack::cycleFrame(200, 256), std::uint32_t{200});
		QCOMPARE(midijack::cycleFrame(255, 256), std::uint32_t{255});
		QCOMPARE(midijack::cycleFrame(256, 256), std::uint32_t{255});
		QCOMPARE(midijack::cycleFrame(9000, 256), std::uint32_t{255});
		QCOMPARE(midijack::cycleFrame(10, 0), std::uint32_t{0});
	}
};

QTEST_GUILESS_MAIN(MidiJackOutScheduleTest)
#include "MidiJackOutScheduleTest.moc"
