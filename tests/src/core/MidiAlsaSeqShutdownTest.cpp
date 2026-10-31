/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! Q050-488: the ALSA sequencer worker is joined before the project's ports are destroyed.
 *
 *  The worker copies raw MidiPort pointers out of its port map and dispatches through them
 *  unlocked. It used to be stopped only in ~AudioEngine, after clearProject() had destroyed
 *  those ports. The fakes replace libasound's sequencer entry points (the executable's
 *  definitions win over the shared library, as in MidiAlsaRawShutdownTest): the "sequencer"
 *  is a pipe that only the test writes to, so with no input the worker blocks in poll() as
 *  on an idle real sequencer. Hermetic; no /dev/snd/seq needed. */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>

#include <QSemaphore>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiAlsaSeq.h"
#include "MidiEvent.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "Song.h"
#include "Track.h"

namespace
{

int g_eventPipe[2] = {-1, -1};
std::mutex g_eventsMutex;
std::deque<snd_seq_event_t> g_events;
snd_seq_event_t g_current{};

std::atomic<bool> g_endlessInput{false};
std::atomic<int> g_openResult{0};
std::atomic<int> g_queueResult{1};
std::atomic<int> g_nextPort{10};
std::atomic<int> g_lastPort{-1};
std::atomic<int> g_closeCalls{0};
std::atomic<int> g_notesOut{0};
std::atomic<int> g_notesOffOut{0};
std::atomic<bool> g_workerRunningAtClose{false};
const QThread* g_client = nullptr;

auto fakeHandle()
{
	static int storage = 0;
	return reinterpret_cast<snd_seq_t*>(&storage);
}

void resetSequencer()
{
	const std::lock_guard<std::mutex> lock(g_eventsMutex);
	g_events.clear();
	g_endlessInput = false;
	g_openResult = 0;
	g_queueResult = 1;
	g_nextPort = 10;
	g_lastPort = -1;
	g_closeCalls = 0;
	g_notesOut = 0;
	g_notesOffOut = 0;
	g_workerRunningAtClose = false;
	g_client = nullptr;
}

//! Hands the worker a note-on addressed to a port, waking it the way a device would.
void queueNoteOn(int destPort)
{
	snd_seq_event_t ev{};
	ev.type = SND_SEQ_EVENT_NOTEON;
	ev.dest.port = static_cast<unsigned char>(destPort);
	ev.source.port = 200;
	ev.data.note.note = 60;
	ev.data.note.velocity = 100;
	{
		const std::lock_guard<std::mutex> lock(g_eventsMutex);
		g_events.push_back(ev);
	}
	const char wake = 1;
	QCOMPARE(::write(g_eventPipe[1], &wake, 1), ssize_t{1});
}

} // namespace

extern "C" {

int snd_seq_open(snd_seq_t** handle, const char*, int, int)
{
	if (g_openResult.load() < 0) { return g_openResult.load(); }
	*handle = fakeHandle();
	return 0;
}

int snd_seq_close(snd_seq_t*)
{
	++g_closeCalls;
	if (g_client != nullptr && g_client->isRunning()) { g_workerRunningAtClose = true; }
	return 0;
}

int snd_seq_set_client_name(snd_seq_t*, const char*) { return 0; }
int snd_seq_alloc_queue(snd_seq_t*) { return g_queueResult.load(); }
int snd_seq_free_queue(snd_seq_t*, int) { return 0; }
int snd_seq_set_queue_tempo(snd_seq_t*, int, snd_seq_queue_tempo_t*) { return 0; }
int snd_seq_control_queue(snd_seq_t*, int, int, int, snd_seq_event_t*) { return 0; }
int snd_seq_get_queue_timer(snd_seq_t*, int, snd_seq_queue_timer_t*) { return -ENOENT; }
int snd_seq_set_queue_timer(snd_seq_t*, int, snd_seq_queue_timer_t*) { return 0; }
int snd_seq_drain_output(snd_seq_t*) { return 0; }
int snd_seq_query_next_client(snd_seq_t*, snd_seq_client_info_t*) { return -ENOENT; }
int snd_seq_query_next_port(snd_seq_t*, snd_seq_port_info_t*) { return -ENOENT; }
int snd_seq_get_port_info(snd_seq_t*, int, snd_seq_port_info_t*) { return 0; }
int snd_seq_set_port_info(snd_seq_t*, int, snd_seq_port_info_t*) { return 0; }
int snd_seq_delete_simple_port(snd_seq_t*, int) { return 0; }

int snd_seq_create_simple_port(snd_seq_t*, const char*, unsigned int, unsigned int)
{
	const int port = g_nextPort++;
	g_lastPort = port;
	return port;
}

int snd_seq_poll_descriptors_count(snd_seq_t*, short) { return 1; }

int snd_seq_poll_descriptors(snd_seq_t*, pollfd* descriptors, unsigned int space, short)
{
	if (space >= 1) { descriptors[0] = {g_eventPipe[0], POLLIN, 0}; }
	return 1;
}

int snd_seq_event_input_pending(snd_seq_t*, int)
{
	if (g_endlessInput.load()) { return 1; }
	const std::lock_guard<std::mutex> lock(g_eventsMutex);
	return static_cast<int>(g_events.size());
}

int snd_seq_event_input(snd_seq_t*, snd_seq_event_t** event)
{
	if (g_endlessInput.load())
	{
		g_current = {};
		g_current.type = SND_SEQ_EVENT_NOTEON;
		g_current.dest.port = 250; // no port of ours: the worker only loops
		*event = &g_current;
		return 1;
	}
	const std::lock_guard<std::mutex> lock(g_eventsMutex);
	if (g_events.empty()) { return -EAGAIN; }
	g_current = g_events.front();
	g_events.pop_front();
	char byte = 0;
	[[maybe_unused]] const ssize_t consumed = ::read(g_eventPipe[0], &byte, 1);
	*event = &g_current;
	return 1;
}

int snd_seq_event_output(snd_seq_t*, snd_seq_event_t* event)
{
	++g_notesOut;
	if (event->type == SND_SEQ_EVENT_NOTEOFF) { ++g_notesOffOut; }
	return 1;
}

} // extern "C"

namespace
{

//! A port's event processor that holds the worker inside the dispatch until released.
class GatedProcessor final : public lmms::MidiEventProcessor
{
public:
	void processInEvent(const lmms::MidiEvent&, const lmms::TimePos&, lmms::f_cnt_t) override
	{
		++calls;
		entered.release();
		gate.acquire();
	}
	void processOutEvent(const lmms::MidiEvent&, const lmms::TimePos&, lmms::f_cnt_t) override {}

	std::atomic<int> calls{0};
	QSemaphore entered;
	QSemaphore gate;
};

} // namespace

class MidiAlsaSeqShutdownTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		resetSequencer();
		QVERIFY(::pipe(g_eventPipe) == 0);
		QVERIFY(::fcntl(g_eventPipe[0], F_SETFL, ::fcntl(g_eventPipe[0], F_GETFL) | O_NONBLOCK) == 0);
	}

	void cleanup()
	{
		for (int& fd : g_eventPipe)
		{
			if (fd >= 0) { ::close(fd); }
			fd = -1;
		}
	}

	//! A worker blocked waiting for input stops within the bound, repeatedly, and twice over.
	//! The bound: the wake byte ends the poll at once, so the join takes no real time;
	//! 500 ms is the budget this destructor used to have (EventPollTimeOut * 2), and leaves
	//! room for a loaded machine. Only after the join is the sequencer closed, once.
	void stoppingAWorkerBlockedInPollIsBoundedAndRepeatable()
	{
		lmms::Engine::init(true); // the client reads the song's tempo
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		for (int round = 0; round < 5; ++round)
		{
			resetSequencer();
			{
				lmms::MidiAlsaSeq client;
				g_client = &client;
				QVERIFY(client.isRunning());
				QTest::qWait(50); // let the worker reach its blocking poll()
				const auto started = std::chrono::steady_clock::now();
				client.stopInput();
				const auto took = std::chrono::steady_clock::now() - started;
				QVERIFY(took < std::chrono::milliseconds(500));
				QVERIFY(!client.isRunning());
				client.stopInput();
				QCOMPARE(g_closeCalls.load(), 0); // stopping releases nothing; the destructor does
			}
			QCOMPARE(g_closeCalls.load(), 1);
			QVERIFY(!g_workerRunningAtClose.load());
		}
	}

	//! A device that never stops sending must not hold the join back: the worker's batch
	//! loop used to run until no input was pending, so this stop would never have returned.
	void endlessInputCannotHoldTheJoinBack()
	{
		lmms::Engine::init(true);
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		lmms::MidiAlsaSeq client;
		g_client = &client;
		g_endlessInput = true;
		const char wake = 1;
		QCOMPARE(::write(g_eventPipe[1], &wake, 1), ssize_t{1});
		QTest::qWait(50);
		std::atomic<bool> stopped{false};
		std::thread stopper([&] { client.stopInput(); stopped = true; });
		QTRY_VERIFY_WITH_TIMEOUT(stopped.load(), 2000);
		const bool stoppedInTime = stopped.load();
		g_endlessInput = false; // lets the old loop end, so a failure here fails and does not hang
		stopper.join();
		QVERIFY(stoppedInTime);
	}

	//! Partial startup: nothing is started when the sequencer cannot be opened or configured;
	//! stopping that client is harmless, and what was opened is closed exactly once.
	void partialStartupFailureIsSafeToStop_data()
	{
		QTest::addColumn<int>("openResult");
		QTest::addColumn<int>("queueResult");
		QTest::addColumn<int>("closes");
		QTest::newRow("open fails") << -ENOENT << 1 << 0;
		QTest::newRow("queue fails") << 0 << -ENOMEM << 1;
	}

	void partialStartupFailureIsSafeToStop()
	{
		QFETCH(int, openResult);
		QFETCH(int, queueResult);
		QFETCH(int, closes);
		lmms::Engine::init(true);
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		g_openResult = openResult;
		g_queueResult = queueResult;
		{
			lmms::MidiAlsaSeq client;
			QVERIFY(!client.isRunning());
			client.stopInput();
			client.stopInput();
		}
		QCOMPARE(g_closeCalls.load(), closes);
	}

	//! Input after the stop reaches nobody, and the dispatch in flight is waited for.
	void stopInputWaitsForAnInFlightDispatch()
	{
		lmms::Engine::init(true);
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		lmms::MidiAlsaSeq client;
		g_client = &client;
		GatedProcessor processor;
		lmms::MidiPort port(QStringLiteral("q050-488"), &client, &processor, nullptr, lmms::MidiPort::Mode::Input);
		const int portId = g_lastPort.load();
		queueNoteOn(portId);
		queueNoteOn(portId); // still pending when the stop arrives: it must not be dispatched
		QVERIFY(processor.entered.tryAcquire(1, 5000));

		std::atomic<bool> stopped{false};
		std::thread stopper([&] { client.stopInput(); stopped = true; });
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		const bool returnedDuringDispatch = stopped.load();
		processor.gate.release(8);
		stopper.join();
		QVERIFY2(!returnedDuringDispatch, "stopInput() returned while a dispatch to the port was in flight");
		QVERIFY(!client.isRunning());
		QCOMPARE(processor.calls.load(), 1);
	}

	//! Delivery is kept: with the worker gone, a note-off a track sends while it is being
	//! destroyed still leaves, before its port does. Queued to a removed port it would be dropped.
	void outputQueuedAfterTheStopIsSentBeforeThePortGoes()
	{
		lmms::Engine::init(true);
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		lmms::MidiAlsaSeq client;
		g_client = &client;
		auto* port = new lmms::MidiPort(QStringLiteral("q050-488"), &client, nullptr, nullptr, lmms::MidiPort::Mode::Output);
		client.stopInput();
		client.processOutEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 60, 0), lmms::TimePos(), port);
		QCOMPARE(g_notesOffOut.load(), 0); // the worker is gone: nothing else can have sent it
		delete port;
		QCOMPARE(g_notesOffOut.load(), 1);
	}

	//! The review's finding, through the real shutdown: a sequencer event is held inside a
	//! dispatch to a port a track owns while Engine::destroy() runs on this thread. The port
	//! must outlive the dispatch, and the worker must be joined when the port goes. A second
	//! event, pending at that moment, must not be dispatched at all. Deterministic: the gate
	//! fixes the interleaving, and only a joining stopInput() can make the checks hold.
	void engineShutdownJoinsTheWorkerBeforePortsAreDestroyed()
	{
		using namespace lmms;
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), AudioDummy::name());
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("mididev"), MidiAlsaSeq::name());
		Engine::init(false);
		auto* client = dynamic_cast<MidiAlsaSeq*>(Engine::audioEngine()->midiClient());
		QVERIFY(client != nullptr);
		QVERIFY(client->isRunning());
		g_client = client;

		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		GatedProcessor processor;
		// Child of the track: clearProject() destroys it with the track.
		auto* port = new MidiPort(QStringLiteral("q050-488"), client, &processor, track, MidiPort::Mode::Input);
		const int portId = g_lastPort.load();
		std::atomic<bool> portDestroyed{false};
		std::atomic<bool> workerRunningAtPortTeardown{false};
		QObject::connect(port, &QObject::destroyed, [&] {
			portDestroyed = true;
			if (client->isRunning()) { workerRunningAtPortTeardown = true; }
		});

		queueNoteOn(portId);
		queueNoteOn(portId);
		QVERIFY(processor.entered.tryAcquire(1, 5000)); // the worker is inside the port's dispatch

		std::atomic<bool> destroyedDuringDispatch{false};
		std::thread releaser([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			destroyedDuringDispatch = portDestroyed.load();
			processor.gate.release(8);
		});
		Engine::destroy();
		releaser.join();
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("mididev"), QString());
		ConfigManager::inst()->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), QString());

		QVERIFY2(!destroyedDuringDispatch.load(), "clearProject() destroyed a port while the sequencer worker was dispatching to it");
		QVERIFY(portDestroyed.load());
		QVERIFY2(!workerRunningAtPortTeardown.load(), "the sequencer worker was still running while a port was destroyed");
		QCOMPARE(processor.calls.load(), 1);
		QCOMPARE(g_closeCalls.load(), 1);
		QVERIFY(!g_workerRunningAtClose.load());
	}
};

QTEST_GUILESS_MAIN(MidiAlsaSeqShutdownTest)
#include "MidiAlsaSeqShutdownTest.moc"
