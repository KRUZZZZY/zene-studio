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

#include "MidiJackOutSchedule.h"
#include "MpscQueue.h"

using namespace lmms;
using midijack::OutMessage;

namespace
{

auto message(std::uint8_t status, std::uint8_t key, std::uint32_t frame) -> OutMessage
{
	return OutMessage{{status, key, 100}, 3, frame};
}

} // namespace

class MidiJackOutScheduleTest : public QObject
{
	Q_OBJECT

private slots:
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
