/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#include "MidiOutQueue.h"
#include "AllocationProbe.h"
#include "lmmsconfig.h"
#ifdef LMMS_HAVE_ALSA
#include "Engine.h"
#include "MidiPort.h"
#endif
#include "lmmsconfig.h"

#include <QtTest>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include <QMutex>
#include <QSemaphore>

#ifdef LMMS_HAVE_ALSA
#include <unistd.h>
#include "MidiAlsaSeq.h"
#endif

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

//! Q050-277: one audio-path producer (channel 0) and two off-audio senders (channels 1, 2) against one MidiAlsaSeq,
//! drained by the test thread. Each event carries its producer's sequence number in key (low 7 bits) and velocity.
struct TwoQueueStress
{
	static constexpr int PerProducer = 60000;

	explicit TwoQueueStress(lmms::MidiAlsaSeq& sequencer) : seq(sequencer) {}

	static MidiEvent tagged(int channel, int number)
	{
		return MidiEvent(lmms::MidiNoteOn, static_cast<int8_t>(channel), static_cast<int16_t>(number & 0x7f),
			static_cast<int16_t>((number >> 7) & 0x7f));
	}

	//! The audio thread's own path; a refused push (counted as overflow) is retried.
	void produceOnAudioPath()
	{
		for (int i = 0; i < PerProducer && !giveUp.load();)
		{
			const auto before = seq.outputQueueOverflowCount();
			seq.processOutEvent(tagged(0, i), TimePos(), nullptr);
			if (seq.outputQueueOverflowCount() == before) { ++i; } else { std::this_thread::yield(); }
		}
		producersDone.fetch_add(1);
	}

	void produceOffAudio(int channel)
	{
		for (int i = 0; i < PerProducer && !giveUp.load();)
		{
			if (seq.sendFromNonAudioThread(tagged(channel, i), nullptr)) { ++i; } else { std::this_thread::yield(); }
		}
		producersDone.fetch_add(1);
	}

	void take(const MidiOutQueue::Command& command, bool viaOffAudioQueue)
	{
		const int channel = command.event.channel();
		if (channel < 0 || channel > 2) { ++disordered; return; }
		if (viaOffAudioQueue != (channel != 0)) { ++misrouted; }
		const int number = (command.event.key() & 0x7f) | ((command.event.velocity() & 0x7f) << 7);
		int& expected = next[static_cast<std::size_t>(channel)];
		if (number != (expected & 0x3fff)) { ++disordered; }
		++expected;
	}

	//! A corrupted single-producer queue can refuse forever: give up after 20 s, so the test fails instead of hanging.
	void consumeUntilProducersFinish()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		const auto sink = [this](const MidiOutQueue::Command& command, bool off) { take(command, off); };
		while (producersDone.load() < 3)
		{
			seq.drainOutputForTesting(sink);
			if (std::chrono::steady_clock::now() > deadline) { giveUp.store(true); }
		}
	}

	void drainRest()
	{
		const auto sink = [this](const MidiOutQueue::Command& command, bool off) { take(command, off); };
		while (!giveUp.load() && seq.drainOutputForTesting(sink) > 0) {}
	}

	lmms::MidiAlsaSeq& seq;
	std::atomic<int> producersDone{0};
	std::atomic<bool> giveUp{false};
	std::array<int, 3> next{0, 0, 0};
	int misrouted = 0;
	int disordered = 0;
};

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

	//! The audio-thread output path must never take the sequencer mutex. The sequencer
	//! worker owns that mutex, and a contended mutex on the audio thread can stall an
	//! audio period - which is how the MIDI clock reached the device from the audio
	//! path (new-p9-270). The mutex is held for the whole check, so an output path
	//! that took it would block until the check gives up: the test fails instead of
	//! hanging, and the mutex is released on every path so the caller can be joined.
	void processOutEventDoesNotTakeTheSequencerMutex()
	{
		lmms::MidiAlsaSeq sequencer{lmms::MidiAlsaSeq::PipeOnlyForTesting{}};
		QMutex& mutex = sequencer.seqMutexForTesting();
		mutex.lock();
		QSemaphore returned;
		std::thread caller([&] {
			sequencer.processOutEvent(makeMidiEvent(66), TimePos(), nullptr);
			returned.release();
		});
		// The queued path returns in microseconds; a mutex-taking path blocks until
		// the mutex is released, which happens below, after the check gave up.
		const bool finished = returned.tryAcquire(1, 5000);
		mutex.unlock();
		caller.join();
		QVERIFY2(finished,
			"the audio-thread MIDI output path took the sequencer mutex and can stall an audio period");
	}

	//! Q050-277 (review round 2): the MIDI clock's capture sender sends from a second thread. It must not become a
	//! second producer of m_outQueue, which is single-producer (the audio thread's). One thread plays the audio thread
	//! (processOutEvent, channel 0) while TWO off-audio senders (sendFromNonAudioThread, channels 1 and 2 - the
	//! client's mutex serializes them) and this thread, the consumer, drain both queues concurrently. Every event must
	//! arrive exactly once, in its producer's order, and an off-audio event only ever through the off-audio queue.
	void offAudioOutputNeverSharesTheAudioThreadsQueue()
	{
		lmms::MidiAlsaSeq sequencer{lmms::MidiAlsaSeq::PipeOnlyForTesting{}};
		TwoQueueStress stress(sequencer);
		std::thread audio([&] { stress.produceOnAudioPath(); });
		std::thread senderA([&] { stress.produceOffAudio(1); });
		std::thread senderB([&] { stress.produceOffAudio(2); });
		stress.consumeUntilProducersFinish();
		audio.join();
		senderA.join();
		senderB.join();
		stress.drainRest();

		QCOMPARE(stress.misrouted, 0);   // the off-audio senders never touched the audio thread's queue
		QVERIFY2(!stress.giveUp.load(), "the producers never finished: the queue refused or lost events for 20 s");
		QCOMPARE(stress.disordered, 0);  // no slot overwritten, duplicated or skipped
		QCOMPARE(stress.next[0], TwoQueueStress::PerProducer);
		QCOMPARE(stress.next[1], TwoQueueStress::PerProducer);
		QCOMPARE(stress.next[2], TwoQueueStress::PerProducer);
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

QTEST_GUILESS_MAIN(MidiOutQueueTest)
#include "MidiOutQueueTest.moc"
