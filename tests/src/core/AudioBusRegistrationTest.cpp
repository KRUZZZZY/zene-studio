/*
 * AudioBusRegistrationTest.cpp - bus registration is safe on the realtime path
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program (if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA)
 *
 */

/*! BUG 3 (cluster new-p12-313): audio bus registration could allocate or lock
 *  on the realtime path.
 *
 *  `AudioBusHandle`'s constructor registers itself with
 *  `AudioEngine::addAudioBusHandle()`, and the class explicitly allows a
 *  standalone bus to be built on the audio path. The registration did:
 *
 *     requestChangeInModel();                 // takes m_changeMutex -> can block
 *     m_audioBusHandles.push_back(handle);    // may grow -> allocates
 *
 *  So a bus created during realtime processing could allocate (no spare
 *  capacity) and one created on a worker thread could block behind the render
 *  period's model lock.
 *
 *  Two cases, each deterministic:
 *
 *   1. REALTIME ALLOCATION. The bus is CONSTRUCTED up front (its own model
 *      allocation is not the subject); the registration is then called again
 *      inside a RealtimeScope with the allocation probe armed and the engine's
 *      bus list shrunk so it has no spare capacity. Allocations and frees must
 *      both be zero; the unfixed push_back() grew the vector on that very call.
 *   2. NO WAIT FOR THE RENDER LOCK. The test holds the model lock, exactly as a
 *      render period does, and asks the engine to register a bus from another
 *      thread. The contention is deliberate - the test holds the lock - so the
 *      registration must DEFER instead of blocking. The engine's own deferral
 *      counter is the handshake: it is incremented before the registration
 *      returns, and the unfixed code cannot increment it at all (it blocked on
 *      the lock this thread holds). No clock decides the verdict; the bounded
 *      wait only exists so a regressed build reports a failure instead of
 *      hanging the suite.
 *
 *  Both cases also pin that a deferred registration is PUBLISHED at the next
 *  period boundary, so the fix cannot "pass" by losing the bus.
 */

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "AllocationProbe.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "RealtimeScope.h"

#define private public
#include "AudioBusHandle.h"
#undef private

using namespace lmms;

class AudioBusRegistrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(Engine::audioEngine()->audioDev() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	/*! Realtime registration allocates nothing and frees nothing, even when the
	 *  engine's bus list has no spare capacity left. */
	void registeringBusesInsideRealtimeScopeDoesNotAllocate()
	{
		AudioEngine* const engine = Engine::audioEngine();
		// Build the bus first: the probe below measures the REGISTRATION, which is
		// the realtime-path function under test, not the bus's own construction. Its
		// constructor already registered it, so take it back out - the case is a bus
		// that is not yet in the list and is registered while a period renders.
		AudioBusHandle bus{QStringLiteral("realtime"), false};
		engine->removeAudioBusHandle(&bus);
		QVERIFY2(std::find(engine->audioBusHandles().begin(), engine->audioBusHandles().end(), &bus)
				== engine->audioBusHandles().end(),
			"the bus is still registered, so the case would not exercise a fresh registration");

		// Fill the list, then drop its slack, so the unfixed push_back() has to grow.
		std::vector<std::unique_ptr<AudioBusHandle>> holders;
		const std::size_t target = engine->audioBusHandles().size() + 8;
		while (engine->audioBusHandles().size() < target)
		{
			holders.emplace_back(std::make_unique<AudioBusHandle>(QStringLiteral("filler"), false));
		}
		engine->m_audioBusHandles.shrink_to_fit();
		QCOMPARE(engine->m_audioBusHandles.capacity(), engine->audioBusHandles().size());

		const std::uint64_t deferralsBefore = engine->audioBusRegistrationDeferrals();
		test::resetAllocationCount();
		{
			const RealtimeScope realtime;
			test::tlCountAllocations = true;
			engine->addAudioBusHandle(&bus);
			test::tlCountAllocations = false;
		}
		const std::uint64_t allocations = test::tlAllocationCount;
		const std::uint64_t frees = test::tlFreeCount;
		QVERIFY2(allocations == 0,
			qPrintable(QStringLiteral("a bus registered on the realtime path allocated %1 times")
				.arg(allocations)));
		QVERIFY2(frees == 0,
			qPrintable(QStringLiteral("a bus registered on the realtime path freed %1 times").arg(frees)));
		QCOMPARE(engine->audioBusRegistrationDeferrals(), deferralsBefore + 1);

		// The registration is not lost: the next period boundary publishes it.
		engine->renderNextPeriod();
		const auto& list = engine->audioBusHandles();
		QVERIFY2(std::find(list.begin(), list.end(), &bus) != list.end(),
			"the realtime bus was never registered - the deferral dropped it");
		QCOMPARE(engine->audioBusRegistrationOverflows(), std::uint64_t{0});
	}

	/*! A registration while another thread holds the model lock must defer, not
	 *  wait for it: the deferral happens on the registration thread and the bus is
	 *  published once the lock is free and a period boundary comes. */
	void registrationWhileTheModelLockIsHeldDefersInsteadOfWaiting()
	{
		AudioEngine* const engine = Engine::audioEngine();
		AudioBusHandle bus{QStringLiteral("deferred"), false};
		const std::uint64_t deferralsBefore = engine->audioBusRegistrationDeferrals();

		// Hold the model lock on THIS thread, exactly as a render period does. Any
		// registration that tries to take it would block until this scope ends.
		engine->requestChangeInModel();

		std::atomic<bool> attempted{false};
		std::atomic<bool> returned{false};
		std::thread worker([&] {
			attempted.store(true, std::memory_order_release);
			engine->addAudioBusHandle(&bus);
			returned.store(true, std::memory_order_release);
		});

		// The worker proves it reached the call before this thread lets go of the lock.
		const auto reachedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!attempted.load(std::memory_order_acquire)
			&& std::chrono::steady_clock::now() < reachedDeadline)
		{
			std::this_thread::yield();
		}
		QVERIFY2(attempted.load(std::memory_order_acquire),
			"the worker never reached the registration, so the case proved nothing");

		// The deferred count is published BEFORE the registration returns, so waiting
		// for it is waiting for the registration itself - the seam, not a clock.
		const auto deferralDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		std::uint64_t deferrals = deferralsBefore;
		while (deferrals == deferralsBefore && std::chrono::steady_clock::now() < deferralDeadline)
		{
			deferrals = engine->audioBusRegistrationDeferrals();
			std::this_thread::yield();
		}
		// Release the lock whatever happened, so the join can never hang.
		engine->doneChangeInModel();
		worker.join();

		QVERIFY2(deferrals == deferralsBefore + 1,
			qPrintable(QStringLiteral("the registration did not defer while the model lock was held "
				"by another thread (deferrals %1, expected %2)")
				.arg(deferrals).arg(deferralsBefore + 1)));
		QVERIFY2(returned.load(std::memory_order_acquire),
			"the registration did not return after the lock was released");

		// Still no lost registration: the deferred bus is published by the next period.
		engine->renderNextPeriod();
		const auto& list = engine->audioBusHandles();
		QVERIFY2(std::find(list.begin(), list.end(), &bus) != list.end(),
			"the deferred bus was never published - the registration was dropped");
	}
};

QTEST_GUILESS_MAIN(AudioBusRegistrationTest)
#include "AudioBusRegistrationTest.moc"
