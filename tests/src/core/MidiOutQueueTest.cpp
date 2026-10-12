/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include "MidiOutQueue.h"
#include "AllocationProbe.h"
#include "lmmsconfig.h"
#ifdef LMMS_HAVE_ALSA
#include "Engine.h"
#include "MidiPort.h"
#endif

#include <QtTest>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#ifdef LMMS_HAVE_ALSA
#include <unistd.h>
#include "MidiAlsaSeq.h"
#endif

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

#ifdef LMMS_HAVE_ALSA
struct SequencerHandle
{
	snd_seq_t* value = nullptr;
	~SequencerHandle() { if (value != nullptr) { snd_seq_close(value); } }
};

struct SequencerAddress
{
	int client = -1;
	int port = -1;
};

bool findPort(snd_seq_t* sequencer, const char* wantedName, SequencerAddress* result)
{
	snd_seq_client_info_t* clientInfo = nullptr;
	snd_seq_port_info_t* portInfo = nullptr;
	if (snd_seq_client_info_malloc(&clientInfo) < 0 || snd_seq_port_info_malloc(&portInfo) < 0)
	{
		if (clientInfo != nullptr) { snd_seq_client_info_free(clientInfo); }
		if (portInfo != nullptr) { snd_seq_port_info_free(portInfo); }
		return false;
	}

	bool found = false;
	snd_seq_client_info_set_client(clientInfo, -1);
	while (snd_seq_query_next_client(sequencer, clientInfo) >= 0 && !found)
	{
		const int client = snd_seq_client_info_get_client(clientInfo);
		snd_seq_port_info_set_client(portInfo, client);
		snd_seq_port_info_set_port(portInfo, -1);
		while (snd_seq_query_next_port(sequencer, portInfo) >= 0)
		{
			if (std::strcmp(snd_seq_port_info_get_name(portInfo), wantedName) == 0)
			{
				result->client = client;
				result->port = snd_seq_port_info_get_port(portInfo);
				found = true;
				break;
			}
		}
	}

	snd_seq_client_info_free(clientInfo);
	snd_seq_port_info_free(portInfo);
	return found;
}
//! Opens and names the receiving client and creates its input port. Returns the skip reason, or an empty string.
QString openReceiver(SequencerHandle& receiver, int* receiverPort)
{
	const int openResult = snd_seq_open(&receiver.value, "default", SND_SEQ_OPEN_INPUT, 0);
	if (openResult < 0)
	{
		return QStringLiteral("ALSA sequencer is unavailable: %1").arg(snd_strerror(openResult));
	}
	const int nameResult = snd_seq_set_client_name(receiver.value, "Q050-028 receiver");
	if (nameResult < 0)
	{
		return QStringLiteral("Cannot name ALSA receiver client: %1").arg(snd_strerror(nameResult));
	}
	*receiverPort = snd_seq_create_simple_port(receiver.value, "capture",
		SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
		SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	if (*receiverPort < 0)
	{
		return QStringLiteral("Cannot create ALSA receiver port: %1").arg(snd_strerror(*receiverPort));
	}
	return {};
}

//! Waits until \a deadline for a NOTE_ON. Returns its key, or -1 on timeout; \a error is set on an ALSA failure.
int waitForNoteOn(snd_seq_t* receiver, std::chrono::steady_clock::time_point deadline, QString* error)
{
	while (std::chrono::steady_clock::now() < deadline)
	{
		const int pending = snd_seq_event_input_pending(receiver, 1);
		if (pending < 0)
		{
			*error = QString::fromUtf8(snd_strerror(pending));
			return -1;
		}
		if (pending == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		snd_seq_event_t* event = nullptr;
		const int inputResult = snd_seq_event_input(receiver, &event);
		if (inputResult < 0)
		{
			*error = QString::fromUtf8(snd_strerror(inputResult));
			return -1;
		}
		if (event == nullptr) { continue; }
		const int key = event->type == SND_SEQ_EVENT_NOTEON ? event->data.note.note : -1;
		snd_seq_free_event(event);
		if (key >= 0) { return key; }
	}
	return -1;
}

//! Waits for the output worker to enter its poll wait, so the queued event must wake it rather than be found.
bool waitForPollWait(const lmms::MidiAlsaSeq& outputClient)
{
	const auto pollDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!outputClient.pollWaitActiveForTesting() && std::chrono::steady_clock::now() < pollDeadline)
	{
		std::this_thread::yield();
	}
	return outputClient.pollWaitActiveForTesting();
}
#endif
}

class MidiOutQueueTest : public QObject
{
	Q_OBJECT
private slots:
#ifdef LMMS_HAVE_ALSA
	void initTestCase()
	{
		lmms::Engine::init(true);
		QVERIFY(lmms::Engine::getSong() != nullptr);
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}
#endif

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

#ifdef LMMS_HAVE_ALSA
	//! Exercise processOutEvent() itself: removing its wake-pipe request leaves this non-blocking read empty.
	void processOutEventRequestsTheSequencerWake()
	{
		lmms::MidiAlsaSeq sequencer{lmms::MidiAlsaSeq::PipeOnlyForTesting{}};
		const int readFd = sequencer.wakeReadFdForTesting();
		QVERIFY(readFd >= 0);

		const auto note = makeMidiEvent(64);
		sequencer.processOutEvent(note, TimePos(), nullptr);

		char wake = 0;
		QCOMPARE(::read(readFd, &wake, 1), ssize_t{1});
		QCOMPARE(wake, '\0');
	}
#endif

	//! Route a real Zene ALSA output port to an ALSA capture client. The queue event is sent while
	//! MidiAlsaSeq's worker is blocked in poll(); receipt time distinguishes the pipe wake from the
	//! old 250 ms poll timeout.
	void queuedEventReachesAnAlsaSequencerClientPromptly()
	{
#ifndef LMMS_HAVE_ALSA
		QSKIP("Zene was built without ALSA sequencer support");
#else
		SequencerHandle receiver;
		int receiverPort = -1;
		const QString skipReason = openReceiver(receiver, &receiverPort);
		if (!skipReason.isEmpty()) { QSKIP(qPrintable(skipReason)); }
		QVERIFY(snd_seq_nonblock(receiver.value, 1) >= 0);

		lmms::MidiAlsaSeq outputClient;
		QVERIFY2(outputClient.isRunning(), "Zene's ALSA MIDI output worker did not start");
		const QByteArray portName = QByteArrayLiteral("Q050-028 output") + QByteArray::number(::getpid());
		lmms::MidiPort outputPort(QString::fromUtf8(portName), &outputClient, nullptr,
			nullptr, lmms::MidiPort::Mode::Output);
		outputPort.setName(QString::fromUtf8(portName));

		SequencerAddress address;
		QVERIFY2(findPort(receiver.value, portName.constData(), &address),
			"could not find Zene's ALSA output port from the receiver client");
		const int connectResult = snd_seq_connect_from(receiver.value, receiverPort,
			address.client, address.port);
		QVERIFY2(connectResult >= 0, snd_strerror(connectResult));

		// Let ALSA's subscription notifications drain, then wait until the worker is in its poll wait.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		QVERIFY2(waitForPollWait(outputClient), "worker never entered its poll wait");

		const auto sentAt = std::chrono::steady_clock::now();
		outputPort.processOutEvent(makeMidiEvent(65), TimePos());
		QCOMPARE(outputPort.outputEventsWritten(), quint64{1});
		QCOMPARE(outputClient.outputQueueOverflowCount(), std::uint64_t{0});

		QString error;
		const int receivedKey = waitForNoteOn(receiver.value, sentAt + std::chrono::seconds(2), &error);
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - sentAt).count();
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY2(receivedKey >= 0, "ALSA receiver did not receive the queued Zene MIDI note");
		QCOMPARE(receivedKey, 65);
		qInfo() << "queued ALSA MIDI arrival:" << elapsed << "us";
		QVERIFY2(elapsed < 150000,
			qPrintable(QStringLiteral("queued MIDI arrived after %1 us (250 ms poll timeout)").arg(elapsed)));
#endif
	}
};

QTEST_GUILESS_MAIN(MidiOutQueueTest)
#include "MidiOutQueueTest.moc"
