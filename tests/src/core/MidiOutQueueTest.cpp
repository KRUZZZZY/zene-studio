/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include "MidiOutQueue.h"
#include "AllocationProbe.h"

#include <QtTest>
#include <atomic>
#include <cstdint>
#include <thread>

using lmms::MidiEvent;
using lmms::MidiOutQueue;
using lmms::TimePos;

namespace
{
MidiEvent makeMidiEvent(std::uint8_t note)
{
	MidiEvent value;
	value.setType(lmms::MidiNoteOn);
	value.setKey(note);
	value.setVelocity(100);
	return value;
}
}

class MidiOutQueueTest : public QObject
{
	Q_OBJECT
private slots:
	void AudioEnqueue_IsAllocationFreeAndLockFree()
	{
		MidiOutQueue queue;
		const auto note = makeMidiEvent(60);
		const TimePos time;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			QVERIFY(queue.push(note, time, nullptr));
			MidiOutQueue::Command command;
			QVERIFY(queue.pop(command));
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);
		QVERIFY(MidiOutQueue::Capacity > 0);
		QCOMPARE(static_cast<qulonglong>(queue.overflowCount()), 0ull);
	}

	void FullQueue_CountsOverflowWithoutDroppingSilently()
	{
		MidiOutQueue queue;
		const auto note = makeMidiEvent(61);
		const TimePos time;
		for (std::size_t i = 0; i < MidiOutQueue::Capacity; ++i)
		{
			QVERIFY(queue.push(note, time, nullptr));
		}
		QVERIFY(!queue.push(note, time, nullptr));
		QCOMPARE(static_cast<qulonglong>(queue.overflowCount()), 1ull);
	}

	void ProducerConsumer_PreservesOrder()
	{
		MidiOutQueue queue;
		constexpr std::uint64_t count = 100000;
		std::atomic<bool> done{false};
		std::thread producer([&] {
			for (std::uint64_t i = 0; i < count; ++i)
			{
				MidiEvent value = makeMidiEvent(static_cast<std::uint8_t>(i & 0x7f));
				while (!queue.push(value, TimePos(), nullptr)) { std::this_thread::yield(); }
			}
			done.store(true, std::memory_order_release);
		});
		std::uint64_t received = 0;
		MidiOutQueue::Command command;
		for (;;)
		{
			if (queue.pop(command))
			{
				QCOMPARE(static_cast<unsigned>(command.event.key()),
						static_cast<unsigned>(received & 0x7f));
				++received;
				continue;
			}
			if (done.load(std::memory_order_acquire)) { break; }
			std::this_thread::yield();
		}
		producer.join();
		QCOMPARE(static_cast<qulonglong>(received), count);
	}

	//! Cluster 5129: the sequencer thread sleeps in poll() for up to EventPollTimeOut, so an audio-thread
	//! push must wake it. The wake is requested once per consumer drain (the first push after the consumer
	//! acknowledged the previous one), not once per event, and the path stays allocation-free.
	void FirstPushAfterAnAcknowledgedDrainRequestsOneWake()
	{
		MidiOutQueue queue;
		const auto note = makeMidiEvent(62);
		const TimePos time;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		QVERIFY(queue.pushAndRequestWake(note, time, nullptr));   // nothing acknowledged yet: wake
		QVERIFY(!queue.pushAndRequestWake(note, time, nullptr));  // same batch: no second wake
		QVERIFY(!queue.pushAndRequestWake(note, time, nullptr));
		lmms::test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);

		// The consumer acknowledges, then drains all three events; the next push asks again.
		queue.acknowledgeWake();
		MidiOutQueue::Command command;
		int drained = 0;
		while (queue.pop(command)) { ++drained; }
		QCOMPARE(drained, 3);
		QVERIFY(queue.pushAndRequestWake(note, time, nullptr));
		QVERIFY(queue.pop(command));
	}

	//! A refused push (queue full) requests no wake: there is nothing new to drain.
	void FullQueue_RequestsNoWake()
	{
		MidiOutQueue queue;
		const auto note = makeMidiEvent(63);
		const TimePos time;
		for (std::size_t i = 0; i < MidiOutQueue::Capacity; ++i)
		{
			static_cast<void>(queue.pushAndRequestWake(note, time, nullptr));
		}
		queue.acknowledgeWake();
		QVERIFY(!queue.pushAndRequestWake(note, time, nullptr));
		QCOMPARE(static_cast<qulonglong>(queue.overflowCount()), 1ull);
	}
};

QTEST_APPLESS_MAIN(MidiOutQueueTest)
#include "MidiOutQueueTest.moc"
