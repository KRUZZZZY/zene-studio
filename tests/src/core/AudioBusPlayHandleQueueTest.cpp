/*
 * AudioBusPlayHandleQueueTest.cpp - play-handle list edits reach the bus through its queue (cluster 5467)
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

// Cluster 5467 (M0 blockers): off-stage edits use a bounded queue, while render-stage setup edits the list
// directly before the bus worker stage. The white-box access below checks both paths and the full-queue case.

#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <new>
#include <span>
#include <thread>

// The test reads the bus's private list and queue. Everything the header needs is included first.
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioDevice.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "MpscQueue.h"
#include "PlayHandle.h"
#include "RealtimeScope.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"
#include "AllocationProbe.h"

#define private public
#include "AudioBusHandle.h"
#undef private
#include "SampleTrack.h"

using namespace lmms;

namespace
{

//! A play handle that owns no buffer and has nothing to render: a placeholder the bus can list.
class IdlePlayHandle : public PlayHandle
{
public:
	IdlePlayHandle() : PlayHandle(Type::SamplePlayHandle) {}
	void play(std::span<SampleFrame>) override {}
	bool isFinished() const override { return true; }
	bool isFromTrack(const Track*) const override { return false; }
};

class SoundingNoteHandle : public PlayHandle
{
public:
	SoundingNoteHandle() : PlayHandle(Type::NotePlayHandle) {}
	void play(std::span<SampleFrame> buffer) override
	{
		for (SampleFrame& frame : buffer) { frame[0] = 0.25f; frame[1] = 0.25f; }
	}
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track*) const override { return false; }
};

class CapacityOccupantHandle : public PlayHandle
{
public:
	explicit CapacityOccupantHandle(AudioBusHandle* bus) : PlayHandle(Type::InstrumentPlayHandle)
	{
		setAudioBusHandle(bus);
		setUsesBuffer(false);
	}
	void play(std::span<SampleFrame>) override {}
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track*) const override { return false; }
};

class DeferredTrackHandle : public PlayHandle
{
public:
	DeferredTrackHandle(Track* track, AudioBusHandle* bus, std::atomic<int>& played,
		std::atomic<int>& destroyed, bool affinityMatters)
		: PlayHandle(Type::SamplePlayHandle), m_track(track), m_played(played), m_destroyed(destroyed),
		  m_affinityMatters(affinityMatters)
	{
		setAudioBusHandle(bus);
		setUsesBuffer(false);
	}
	~DeferredTrackHandle() override { m_destroyed.fetch_add(1, std::memory_order_relaxed); }
	bool affinityMatters() const override { return m_affinityMatters; }
	void play(std::span<SampleFrame>) override { m_played.fetch_add(1, std::memory_order_relaxed); }
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track* track) const override { return m_track == track; }

private:
	Track* m_track;
	std::atomic<int>& m_played;
	std::atomic<int>& m_destroyed;
	bool m_affinityMatters;
};

struct DeferredScenario
{
	std::unique_ptr<SampleTrack> track;
	std::vector<std::unique_ptr<CapacityOccupantHandle>> occupants;
	std::atomic<int> played{0};
	std::atomic<int> destroyed{0};
	DeferredTrackHandle* handle = nullptr;
};

class AddPlayHandleJob : public ThreadableJob
{
public:
	AddPlayHandleJob(AudioBusHandle& bus, SoundingNoteHandle& handle) : m_bus(bus), m_handle(handle) {}
	bool requiresProcessing() const override { return true; }

protected:
	void doProcessing() override
	{
		m_bus.addPlayHandle(&m_handle);
		m_handle.doProcessing();
	}

private:
	AudioBusHandle& m_bus;
	SoundingNoteHandle& m_handle;
};

} // namespace

class AudioBusPlayHandleQueueTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! An off-stage producer posts the edit; the list changes when the bus consumes it.
	void postingAnEditDoesNotTouchTheListUntilTheBusProcesses()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle handle;

		std::thread poster([&bus, &handle] { bus.addPlayHandle(&handle); });
		poster.join();

		// The posting thread wrote nothing: the edit waits for the bus's own processing.
		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 0);
		bus.doProcessing();
		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 1);

		bus.removePlayHandle(&handle);
		bus.doProcessing();
		QVERIFY(bus.m_playHandles.isEmpty());
	}

	//! Note setup owns the list before the bus stage starts. It appends directly even when the off-stage edit
	//! queue is full, returns without waiting for the consumer, and preserves every queued control edit.
	void audioThreadAddsDuringOwnedRenderStageWhenQueueIsFull()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle queued;
		IdlePlayHandle audioThreadHandle;
		for (std::size_t i = 0; i < PlayHandle::MaxNumber; ++i)
		{
			QVERIFY(bus.m_playHandleEdits.push(AudioBusHandle::PlayHandleEdit{&queued, true}));
		}

		std::atomic<bool> returned{false};
		std::thread producer([&] {
			const RealtimeScope realtime;
			const AudioBusHandle::RenderStageScope ownsList;
			bus.addPlayHandle(&audioThreadHandle);
			returned.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
		while (!returned.load(std::memory_order_acquire)
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::yield();
		}
		const bool returnedBeforeDrain = returned.load(std::memory_order_acquire);

		// Free the old full-queue producer after measuring; the pre-drain result still records red.
		bus.applyPlayHandleEdits();
		producer.join();
		bus.applyPlayHandleEdits();

		QVERIFY2(returnedBeforeDrain,
			"audio-thread producer waited for the bus consumer while the queue was full");
		QVERIFY(std::find(bus.m_playHandles.begin(), bus.m_playHandles.end(), &audioThreadHandle)
			!= bus.m_playHandles.end());
		QCOMPARE(bus.droppedPlayHandleEdits(), std::uint64_t{0});
		bus.m_playHandles.clear();
	}

	//! At capacity, an owner-stage append enters fixed deferral storage and does not grow QList on the audio thread.
	void audioThreadAppendAtCapacityDoesNotAllocate()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle occupied;
		IdlePlayHandle deferred;
		const auto capacity = bus.m_playHandles.capacity();
		for (qsizetype i = 0; i < capacity; ++i) { bus.m_playHandles.append(&occupied); }

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			const AudioBusHandle::RenderStageScope ownsList;
			bus.addPlayHandle(&deferred);
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);
		QCOMPARE(bus.m_playHandles.capacity(), capacity);
		QCOMPARE(bus.capacityDeferredPlayHandleEdits(), std::uint64_t{1});
		QCOMPARE(bus.droppedPlayHandleEdits(), std::uint64_t{0});
	}

	//! A render-stage low-water request is serviced by the main-thread timer before the list fills.
	void mainThreadTopsUpListCapacityAtLowWater()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle occupied;
		const auto initialCapacity = bus.m_playHandles.capacity();
		const auto lowWater = static_cast<qsizetype>(PlayHandle::MaxNumber / 4);
		{
			const RealtimeScope realtime;
			const AudioBusHandle::RenderStageScope ownsList;
			for (qsizetype i = 0; i < initialCapacity - lowWater; ++i)
			{
				QVERIFY(bus.appendPlayHandleWithoutGrowth(&occupied));
			}
		}

		QTRY_VERIFY_WITH_TIMEOUT(bus.m_playHandles.capacity() > initialCapacity, 1000);
		bus.m_playHandles.clear();
	}

	//! A bus born without control-thread reserve accepts its first handle in fixed inline storage.
	void realtimeBusAppendUsesInlineSlotWithoutAllocation()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		bus.m_playHandles.squeeze();
		QCOMPARE(bus.m_playHandles.capacity(), qsizetype{0});
		SoundingNoteHandle handle;
		handle.doProcessing();

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			const AudioBusHandle::RenderStageScope ownsList;
			bus.addPlayHandle(&handle);
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);
		QCOMPARE(bus.m_playHandles.capacity(), qsizetype{0});
		bus.doProcessing();
		QVERIFY2(bus.m_buffer.front()[0] > 0.0f,
			"the first handle on a zero-capacity realtime bus was not mixed");
		QCOMPARE(bus.m_inlinePlayHandle, &handle);
	}

	//! Worker-stage note stacking bypasses a saturated off-stage queue through its preallocated worker buffer.
	void workerSubnoteSoundsWhenEditQueueIsFull()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle queued;
		SoundingNoteHandle subnote;
		for (std::size_t i = 0; i < PlayHandle::MaxNumber; ++i)
		{
			QVERIFY(bus.m_playHandleEdits.push(AudioBusHandle::PlayHandleEdit{&queued, true}));
		}

		AddPlayHandleJob workerJob{bus, subnote};
		const std::array<ThreadableJob*, 1> jobs{&workerJob};
		AudioEngineWorkerThread::fillJobQueue(jobs);
		AudioEngineWorkerThread::startAndWaitForJobs();

		bus.doProcessing();
		QVERIFY2(bus.m_buffer.front()[0] > 0.0f,
			"worker-stage subnote did not reach the bus output when the edit queue was full");
		QVERIFY(std::find(bus.m_playHandles.begin(), bus.m_playHandles.end(), &subnote) != bus.m_playHandles.end());
		QCOMPARE(bus.droppedPlayHandleEdits(), std::uint64_t{0});
	}

	void capacityDeferredHandleIsCanceledByClearWithoutAllocating()
	{
		DeferredScenario scenario;
		arrangeDeferredHandle(scenario, true);
		auto* engine = Engine::audioEngine();

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			const AudioBusHandle::RenderStageScope ownsBusPlayHandleLists;
			engine->clearInternal();
		}
		lmms::test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), 0ull);
		verifyDeferredHandleStayedCanceled(scenario);
	}

	void capacityDeferredHandleIsCanceledByRemovePlayHandle()
	{
		DeferredScenario scenario;
		arrangeDeferredHandle(scenario, true);
		Engine::audioEngine()->removePlayHandle(scenario.handle);
		verifyDeferredHandleStayedCanceled(scenario);
	}

	void queuedRemovePlayHandleCancelsCapacityDeferredHandle()
	{
		DeferredScenario scenario;
		arrangeDeferredHandle(scenario, false);
		auto* engine = Engine::audioEngine();
		engine->removePlayHandle(scenario.handle);
		engine->renderStageNoteSetup();
		verifyDeferredHandleStayedCanceled(scenario);
	}

	void capacityDeferredHandleIsCanceledByTypeRemoval()
	{
		DeferredScenario scenario;
		arrangeDeferredHandle(scenario, true);
		auto* engine = Engine::audioEngine();
		engine->removePlayHandlesOfTypes(scenario.track.get(), PlayHandle::Type::SamplePlayHandle);
		verifyDeferredHandleStayedCanceled(scenario);
	}

	void capacityDeferredHandleIsCanceledBeforeItsTrackDies()
	{
		DeferredScenario scenario;
		arrangeDeferredHandle(scenario, true);
		scenario.track.reset();
		verifyDeferredHandleStayedCanceled(scenario);
	}

	//! Edits posted from another thread, including a remove and a re-add of the same handle, land in posting
	//! order when the bus next processes: the list ends with the handle it was last given.
	void editsFromAnotherThreadApplyInPostingOrderAtTheNextProcessing()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle first;
		IdlePlayHandle second;

		std::thread poster([&bus, &first, &second] {
			bus.addPlayHandle(&first);
			bus.addPlayHandle(&second);
			bus.removePlayHandle(&first);
			bus.addPlayHandle(&first);
		});
		poster.join();

		bus.doProcessing();

		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 2);
		QVERIFY(bus.m_playHandles.at(0) == &second);
		QVERIFY(bus.m_playHandles.at(1) == &first);

		bus.removePlayHandle(&first);
		bus.removePlayHandle(&second);
		bus.doProcessing();
		QVERIFY(bus.m_playHandles.isEmpty());
	}

	//! Cluster new-p15-253 (Luna r6): constructing a bus on the audio path must not allocate or free.
	void realtimeBusConstructionAllocatesNothing()
	{
		auto* engine = Engine::audioEngine();
		const QString name = QStringLiteral("RealtimeCtorBus");
		alignas(AudioBusHandle) std::byte storage[sizeof(AudioBusHandle)];
		AudioBusHandle* bus = nullptr;
		// Cleans up on every return path once the placement construction has succeeded: the
		// control thread registers a pending bus, then the bus is destroyed in its storage.
		// A failing allocation assertion therefore cannot leave a queued, dangling bus.
		struct PlacedBusGuard
		{
			AudioBusHandle*& placed;
			~PlacedBusGuard()
			{
				if (placed == nullptr) { return; }
				auto* engine = Engine::audioEngine();
				engine->serviceAudioBusHandlesOnControlThread();
				engine->renderNextPeriod();
				engine->serviceAudioBusHandlesOnControlThread();
				placed->~AudioBusHandle();
				placed = nullptr;
			}
		};
		PlacedBusGuard placedGuard{bus};
		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		bool threw = false;
		try
		{
			const RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			bus = new (storage) AudioBusHandle(name, true);
		}
		catch (const std::bad_alloc&) { threw = true; }
		lmms::test::tlCountAllocations = false;
		allocations = lmms::test::tlAllocationCount;
		frees = lmms::test::tlFreeCount;

		if (threw)
		{
			QFAIL("the realtime bus constructor threw std::bad_alloc before the control-thread service ran");
		}

		const auto& handles = engine->audioBusHandles();
		const bool listedBeforeService = std::find(handles.begin(), handles.end(), bus) != handles.end();

		// The constructor leaves the storage empty: the control-thread service sizes it.
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(frees, std::uint64_t{0});
		QVERIFY(bus->buffer().empty());
		QVERIFY(!listedBeforeService);
		QVERIFY(bus->effects() == nullptr);

		// The control-thread service is the only activation point; the render owner is not asked to promote.
		engine->serviceAudioBusHandlesOnControlThread();
		const bool listed = std::find(handles.begin(), handles.end(), bus) != handles.end();
		const bool periodSized = bus->buffer().size() == static_cast<std::size_t>(engine->framesPerPeriod());
		const bool hasEffects = bus->effects() != nullptr;

		// Destroyed here, so the guard above must not destroy it again.
		AudioBusHandle* destroyed = bus;
		bus = nullptr;
		destroyed->~AudioBusHandle();
		const bool stillListed = std::find(handles.begin(), handles.end(), destroyed) != handles.end();

		QVERIFY(listed);
		QVERIFY(periodSized);
		QVERIFY(hasEffects);
		QVERIFY(!stillListed);
	}

	//! Cluster new-p15-253: the realtime constructor must not take the engine's change lock.
	//! This checks a forced lock order, not a race: the main thread holds the model lock,
	//! a worker constructs a bus on the audio path, and the constructor must return without
	//! waiting for the lock. The bounded wait below is a diagnostic window, not a
	//! synchronisation point: if the constructor blocks on the held lock, the window expires,
	//! the lock is released, the worker is joined, and the test fails.
	//! Regression guard: the constructor already defers the lock at this HEAD, so the lock
	//! half passes. The RED part is the service-activation assertion at the end.
	void realtimeConstructorDoesNotTakeEngineChangeLockHeldByAnotherThread()
	{
		using namespace lmms;
		using State = AudioBusHandle::RegistrationState;

		struct BusDeleter
		{
			void operator()(AudioBusHandle* busHandle) const
			{
				Engine::audioEngine()->destroyAudioBusHandle(busHandle);
			}
		};
		using BusPtr = std::unique_ptr<AudioBusHandle, BusDeleter>;

		auto* engine = Engine::audioEngine();
		std::atomic<AudioBusHandle*> published{nullptr};
		std::promise<void> constructed;
		std::future<void> constructedFuture = constructed.get_future();

		// The main thread holds the engine's model lock for the whole window.
		engine->requestChangeInModel();
		std::thread worker{[&] {
			{
				const RealtimeScope realtime;
				published.store(new AudioBusHandle(QStringLiteral("HeldLockBus"), false), std::memory_order_release);
			}
			constructed.set_value();
		}};

		// Bounded diagnostic window: the future is set once the constructor has returned; no polling.
		const bool finished = constructedFuture.wait_for(std::chrono::seconds{2}) == std::future_status::ready;

		// Release the lock in both outcomes, and join before any assertion can return.
		engine->doneChangeInModel();
		worker.join();

		// Owns the bus from here on, so every return path hands it to the engine.
		BusPtr bus{published.load(std::memory_order_acquire)};
		if (!finished)
		{
			QFAIL("the realtime constructor did not return within 2 s: it blocked on the change lock held by another thread");
		}
		QVERIFY(bus != nullptr);
		QCOMPARE(bus->registrationState(), State::Pending);

		engine->serviceAudioBusHandlesOnControlThread();
		QCOMPARE(bus->registrationState(), State::Registered);
	}

private:
	void arrangeDeferredHandle(DeferredScenario& scenario, bool affinityMatters)
	{
		auto* engine = Engine::audioEngine();
		QVERIFY(engine->m_playHandles.isEmpty());
		QCOMPARE(engine->m_capacityDeferredPlayHandleCount, std::size_t{0});
		scenario.track = std::make_unique<SampleTrack>(Engine::getSong());

		const auto guard = engine->requestChangesGuard();
		engine->m_playHandleCapacityLow.store(false, std::memory_order_relaxed);
		engine->m_playHandleCapacityRefreshRequested.store(false, std::memory_order_relaxed);
		const qsizetype capacity = engine->m_playHandles.capacity();
		QVERIFY(capacity > 0);
		scenario.occupants.reserve(static_cast<std::size_t>(capacity));
		for (qsizetype i = 0; i < capacity; ++i)
		{
			auto occupant = std::make_unique<CapacityOccupantHandle>(scenario.track->audioBusHandle());
			engine->m_playHandles.append(occupant.get());
			scenario.occupants.push_back(std::move(occupant));
		}

		scenario.handle = new DeferredTrackHandle(scenario.track.get(), scenario.track->audioBusHandle(),
			scenario.played, scenario.destroyed, affinityMatters);
		QVERIFY(engine->addPlayHandle(scenario.handle));
		engine->renderStageNoteSetup();
		QCOMPARE(engine->m_capacityDeferredPlayHandleCount, std::size_t{1});
		QVERIFY(engine->m_newPlayHandles.first() == nullptr);
	}

	void verifyDeferredHandleStayedCanceled(DeferredScenario& scenario)
	{
		auto* engine = Engine::audioEngine();
		const bool canceledBeforeRetry = scenario.destroyed.load(std::memory_order_relaxed) == 1;
		engine->m_playHandles.clear();
		scenario.occupants.clear();
		engine->renderNextPeriod();
		const bool appended = engine->m_playHandles.contains(scenario.handle);
		const int played = scenario.played.load(std::memory_order_relaxed);

		// Keep the red run safe and leak-free before reporting its failed assertion.
		if (scenario.destroyed.load(std::memory_order_relaxed) == 0)
		{
			engine->m_playHandles.removeAll(scenario.handle);
			for (std::size_t i = 0; i < engine->m_capacityDeferredPlayHandleCount; ++i)
			{
				if (engine->m_capacityDeferredPlayHandles[i] != scenario.handle) { continue; }
				for (std::size_t move = i + 1; move < engine->m_capacityDeferredPlayHandleCount; ++move)
				{
					engine->m_capacityDeferredPlayHandles[move - 1] = engine->m_capacityDeferredPlayHandles[move];
				}
				--engine->m_capacityDeferredPlayHandleCount;
				break;
			}
			if (scenario.track)
			{
				scenario.handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(scenario.handle);
			}
			delete scenario.handle;
		}
		scenario.handle = nullptr;

		QVERIFY2(canceledBeforeRetry, "the deferred handle was not destroyed by the removal path");
		QVERIFY2(!appended, "the deferred handle was appended after removal or track teardown");
		QCOMPARE(played, 0);
		QCOMPARE(scenario.destroyed.load(std::memory_order_relaxed), 1);
		QCOMPARE(engine->m_capacityDeferredPlayHandleCount, std::size_t{0});
	}
};

QTEST_GUILESS_MAIN(AudioBusPlayHandleQueueTest)
#include "AudioBusPlayHandleQueueTest.moc"
