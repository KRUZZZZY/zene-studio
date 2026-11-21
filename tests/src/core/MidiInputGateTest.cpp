/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

/*! Q050-488: JACK, WinMM and CoreMIDI call a client's input code on a thread the client
 *  cannot join; MidiInputGate is what stopInput() waits on there. The dispatch thread
 *  below plays that foreign callback thread, held at a known point by a semaphore, so
 *  this mocks the blocking case on every platform. The real WinMM and CoreMIDI callbacks
 *  are not driven here; see docs/050-features/Q050-488.md. */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <QSemaphore>

#include "AllocationProbe.h"
#include "MidiInputGate.h"
#include "lmmsconfig.h"

#ifdef LMMS_HAVE_JACK
#include "Engine.h"
#include "MidiJack.h"
#endif

using lmms::MidiInputGate;

class MidiInputGateTest : public QObject
{
	Q_OBJECT

private slots:
	//! close() is the point after which the owner may go: it must outlast the dispatch inside.
	void closeWaitsForTheDispatchInside()
	{
		MidiInputGate gate;
		QSemaphore entered;
		QSemaphore release;
		std::thread callback([&] {
			const MidiInputGate::Scope dispatch(gate);
			QVERIFY(static_cast<bool>(dispatch));
			entered.release();
			release.acquire();
		});
		QVERIFY(entered.tryAcquire(1, 5000));

		std::atomic<bool> closed{false};
		std::thread closer([&] { gate.close(); closed = true; });
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		const bool closedDuringDispatch = closed.load();
		release.release();
		callback.join();
		closer.join();
		QVERIFY2(!closedDuringDispatch, "close() returned while a dispatch was inside");
		QVERIFY(closed.load());
	}

	void closedGateRefusesAndCloseIsIdempotent()
	{
		MidiInputGate gate;
		QVERIFY(gate.enter());
		gate.leave();
		gate.close();
		gate.close();
		QVERIFY(!gate.enter());
		QVERIFY(!gate.enter());
		const MidiInputGate::Scope dispatch(gate);
		QVERIFY(!dispatch);
		gate.close(); // a refused entry left nothing inside, or this would never return
	}

	//! No callback runs after close() returns, whatever the interleaving; WinMM may call
	//! from one thread per device, so several callers run at once.
	void noDispatchStartsAfterClose()
	{
		for (int round = 0; round < 50; ++round)
		{
			MidiInputGate gate;
			std::atomic<bool> closed{false};
			std::atomic<int> dispatchedAfterClose{0};
			std::atomic<bool> running{true};
			std::vector<std::thread> callbacks;
			for (int i = 0; i < 4; ++i)
			{
				callbacks.emplace_back([&] {
					while (running.load())
					{
						const MidiInputGate::Scope dispatch(gate);
						if (dispatch && closed.load()) { ++dispatchedAfterClose; }
					}
				});
			}
			std::this_thread::sleep_for(std::chrono::microseconds(200));
			gate.close();
			closed = true;
			std::this_thread::sleep_for(std::chrono::microseconds(200));
			running = false;
			for (auto& callback : callbacks) { callback.join(); }
			QCOMPARE(dispatchedAfterClose.load(), 0);
		}
	}

	//! The JACK process callback enters this gate, so it must stay realtime-safe.
	void enterAndLeaveDoNotAllocate()
	{
		MidiInputGate gate;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			const MidiInputGate::Scope dispatch(gate);
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{0});
	}

#ifdef LMMS_HAVE_JACK
	//! The JACK client's wiring. No server is needed: the port is null without a client,
	//! so a JackMidiRead() past a closed gate would hand JACK a null port. Duplicate stop too.
	void jackReadAfterStopInputTouchesNothing()
	{
		lmms::Engine::init(true);
		auto engineGuard = qScopeGuard([] { lmms::Engine::destroy(); });
		lmms::MidiJack client;
		client.stopInput();
		client.stopInput();
		client.JackMidiRead(64);
		QVERIFY2(client.inputReadsForTesting() == 0, "JackMidiRead went past a closed gate");
	}
#endif
};

QTEST_GUILESS_MAIN(MidiInputGateTest)
#include "MidiInputGateTest.moc"
