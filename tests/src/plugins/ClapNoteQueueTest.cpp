/*
 * ClapNoteQueueTest.cpp - a full CLAP note queue must not lose a note release
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

#include <QtTest>

#include <memory>
#include <vector>

#include "ClapNoteQueue.h"

namespace lmms::clap
{

namespace
{
constexpr std::uint8_t kNoteOn = 0;
constexpr std::uint8_t kNoteOff = 1;
constexpr std::uint8_t kNoteChoke = 2;
constexpr std::uint8_t kNoteExpression = 4;
constexpr int kMaxBlocks = 64;

auto makeEvent(std::uint8_t type, std::uint8_t channel, int key, std::int32_t frameOffset = 0) -> NoteEventIn
{
	NoteEventIn event;
	event.type = type;
	event.channel = channel;
	event.key = static_cast<std::int16_t>(key);
	event.frameOffset = frameOffset;
	return event;
}

auto makeFiller() -> NoteEventIn
{
	return makeEvent(kNoteExpression, 0, -1);
}

//! Pops up to one block of events. @returns true when the last pop returned false.
auto drainBlock(NoteQueue& queue, std::vector<NoteEventIn>& out) -> bool
{
	for (int i = 0; i < kMaxNoteEventsPerBlock; ++i)
	{
		NoteEventIn event;
		if (!queue.pop(&event)) { return true; }
		out.push_back(event);
	}
	return false;
}

//! Drains block by block, bounded, until a pop returns false. @returns whether it did.
auto drainUntilEmpty(NoteQueue& queue, std::vector<NoteEventIn>& out) -> bool
{
	for (int block = 0; block < kMaxBlocks; ++block)
	{
		if (drainBlock(queue, out)) { return true; }
	}
	return false;
}

auto isRelease(const NoteEventIn& event) -> bool
{
	return event.type == kNoteOff || event.type == kNoteChoke;
}

auto countOf(const std::vector<NoteEventIn>& events, std::uint8_t type, int channel, int key) -> int
{
	int count = 0;
	for (const auto& event : events)
	{
		if (event.type == type && event.channel == channel && event.key == key) { ++count; }
	}
	return count;
}

auto indexOf(const std::vector<NoteEventIn>& events, std::uint8_t type, int channel, int key) -> int
{
	for (std::size_t i = 0; i < events.size(); ++i)
	{
		const auto& event = events[i];
		if (event.type == type && event.channel == channel && event.key == key) { return static_cast<int>(i); }
	}
	return -1;
}

//! Pushes the note-on and enough fillers to leave the ring exactly full.
auto fillRing(NoteQueue& queue, std::uint8_t channel, int key) -> bool
{
	if (!queue.push(makeEvent(kNoteOn, channel, key))) { return false; }
	for (std::size_t i = 1; i < kNoteQueueCapacity; ++i)
	{
		if (!queue.push(makeFiller())) { return false; }
	}
	return true;
}

//! Final state of one key after the delivered events: true when still sounding.
auto soundingAfter(const std::vector<NoteEventIn>& events, int channel, int key) -> bool
{
	bool sounding = false;
	for (const auto& event : events)
	{
		if (event.channel != channel || event.key != key) { continue; }
		if (event.type == kNoteOn) { sounding = true; }
		else if (isRelease(event)) { sounding = false; }
	}
	return sounding;
}
} // namespace

class ClapNoteQueueTest : public QObject
{
	Q_OBJECT

private slots:
	void droppedNoteOffIsDelivered();
	void droppedChokeIsDelivered();
	void recoveryIsBoundedUnderLoad();
	void retriggerDuringRecoveryKeepsOrder();
	void resetForgetsDeferredRelease();
	void releasesWithRoomAreUnchanged();

private:
	static void checkReleaseDelivered(std::uint8_t type, std::uint8_t channel, int key);
};

void ClapNoteQueueTest::checkReleaseDelivered(std::uint8_t type, std::uint8_t channel, int key)
{
	auto queue = std::make_unique<NoteQueue>();
	QVERIFY(fillRing(*queue, channel, key));
	// the ring is full: the release is the one event that must not be lost
	queue->push(makeEvent(type, channel, key));

	std::vector<NoteEventIn> delivered;
	QVERIFY(drainUntilEmpty(*queue, delivered));

	const int on = indexOf(delivered, kNoteOn, channel, key);
	const int off = indexOf(delivered, type, channel, key);
	QCOMPARE(on, 0);
	QVERIFY2(off > on, "the dropped release was never delivered");
	QCOMPARE(countOf(delivered, type, channel, key), 1);
}

void ClapNoteQueueTest::droppedNoteOffIsDelivered()
{
	checkReleaseDelivered(kNoteOff, 0, 60);
}

void ClapNoteQueueTest::droppedChokeIsDelivered()
{
	checkReleaseDelivered(kNoteChoke, 3, 100);
}

void ClapNoteQueueTest::recoveryIsBoundedUnderLoad()
{
	auto queue = std::make_unique<NoteQueue>();
	QVERIFY(fillRing(*queue, 0, 60));
	queue->push(makeEvent(kNoteOff, 0, 60));

	std::vector<NoteEventIn> delivered;
	bool emptied = false;
	for (int block = 0; block < 8; ++block)
	{
		for (int i = 0; i < 300; ++i) { queue->push(makeFiller()); }
		emptied = drainBlock(*queue, delivered);
	}
	QVERIFY2(countOf(delivered, kNoteOff, 0, 60) == 1, "note-off not delivered within 8 blocks under load");

	// once the release is out and a pop has returned false, pushes are accepted again
	for (int block = 0; block < kMaxBlocks && !emptied; ++block) { emptied = drainBlock(*queue, delivered); }
	QVERIFY(emptied);
	QVERIFY2(queue->push(makeEvent(kNoteOn, 0, 62)), "queue keeps refusing events after the release was delivered");
}

void ClapNoteQueueTest::retriggerDuringRecoveryKeepsOrder()
{
	auto queue = std::make_unique<NoteQueue>();
	QVERIFY(fillRing(*queue, 0, 60));
	queue->push(makeEvent(kNoteOff, 0, 60));

	std::vector<NoteEventIn> delivered;
	drainBlock(*queue, delivered);
	const bool accepted = queue->push(makeEvent(kNoteOn, 0, 60));
	QVERIFY(drainUntilEmpty(*queue, delivered));

	// a retrigger the queue accepted must end up sounding, a refused one must not
	QCOMPARE(soundingAfter(delivered, 0, 60), accepted);
}

void ClapNoteQueueTest::resetForgetsDeferredRelease()
{
	auto queue = std::make_unique<NoteQueue>();
	QVERIFY(fillRing(*queue, 0, 60));
	queue->push(makeEvent(kNoteOff, 0, 60));
	queue->reset();

	NoteEventIn event;
	QVERIFY(!queue->pop(&event));
	QVERIFY(queue->push(makeEvent(kNoteOn, 0, 61)));
}

void ClapNoteQueueTest::releasesWithRoomAreUnchanged()
{
	auto queue = std::make_unique<NoteQueue>();
	QVERIFY(queue->push(makeEvent(kNoteOn, 0, 60, 7)));
	QVERIFY(queue->push(makeEvent(kNoteOff, 0, 60, 9)));

	NoteEventIn event;
	QVERIFY(queue->pop(&event));
	QCOMPARE(event.type, kNoteOn);
	QCOMPARE(event.key, std::int16_t{60});
	QCOMPARE(event.frameOffset, std::int32_t{7});
	QVERIFY(queue->pop(&event));
	QCOMPARE(event.type, kNoteOff);
	QCOMPARE(event.key, std::int16_t{60});
	QCOMPARE(event.frameOffset, std::int32_t{9});
	QVERIFY(!queue->pop(&event));
}

} // namespace lmms::clap

QTEST_GUILESS_MAIN(lmms::clap::ClapNoteQueueTest)

#include "ClapNoteQueueTest.moc"
