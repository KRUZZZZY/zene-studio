/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include "MidiOutQueue.h"
#include "AllocationProbe.h"
#include "lmmsconfig.h"

#include <QtTest>
#include <atomic>
#include <cstdint>
#include <thread>

#ifdef LMMS_HAVE_ALSA
#include "MidiAlsaRaw.h"

#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

using lmms::MidiEvent;
using lmms::MidiOutQueue;
using lmms::TimePos;

namespace
{
#ifdef LMMS_HAVE_ALSA
int g_pollReadFd = -1;
int g_inputHandle;
int g_outputHandle;
std::atomic<int> g_closedHandles{0};
std::atomic<int> g_pollErrors{0};
#endif

MidiEvent makeMidiEvent(std::uint8_t note)
{
	MidiEvent value;
	value.setType(lmms::MidiNoteOn);
	value.setKey(note);
	value.setVelocity(100);
	return value;
}
}

#ifdef LMMS_HAVE_ALSA
extern "C" int snd_rawmidi_open(snd_rawmidi_t** input, snd_rawmidi_t** output, const char*, int)
{
	*input = reinterpret_cast<snd_rawmidi_t*>(&g_inputHandle);
	*output = reinterpret_cast<snd_rawmidi_t*>(&g_outputHandle);
	return 0;
}

extern "C" int snd_rawmidi_close(snd_rawmidi_t*)
{
	++g_closedHandles;
	return 0;
}

extern "C" int snd_rawmidi_nonblock(snd_rawmidi_t*, int)
{
	return 0;
}

extern "C" int snd_rawmidi_poll_descriptors_count(snd_rawmidi_t*)
{
	return 1;
}

extern "C" int snd_rawmidi_poll_descriptors(snd_rawmidi_t*, pollfd* descriptors, unsigned int)
{
	descriptors[0] = {g_pollReadFd, POLLIN, 0};
	return 1;
}

extern "C" int snd_rawmidi_poll_descriptors_revents(snd_rawmidi_t*, pollfd*, unsigned int,
		unsigned short* revents)
{
	*revents = POLLIN;
	++g_pollErrors;
	return -EIO;
}

extern "C" ssize_t snd_rawmidi_read(snd_rawmidi_t*, void*, size_t)
{
	return 0;
}
#endif

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

#ifdef LMMS_HAVE_ALSA
	void AlsaRaw_ClosesHandlesAfterWorkerExitsOnPollError()
	{
		int pipeFds[2];
		QVERIFY(pipe(pipeFds) == 0);
		const char ready = 'x';
		QCOMPARE(write(pipeFds[1], &ready, 1), ssize_t{1});
		g_pollReadFd = pipeFds[0];
		g_closedHandles = 0;
		g_pollErrors = 0;
		{
			lmms::MidiAlsaRaw client;
			QVERIFY(client.wait(1000));
			QVERIFY(!client.isRunning());
			QCOMPARE(g_pollErrors.load(), 1);
		}
		QCOMPARE(g_closedHandles.load(), 2);
		g_pollReadFd = -1;
		QVERIFY(close(pipeFds[0]) == 0);
		QVERIFY(close(pipeFds[1]) == 0);
	}
#endif
};

QTEST_APPLESS_MAIN(MidiOutQueueTest)
#include "MidiOutQueueTest.moc"
