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
#include "Track.h"
#include "AllocationProbe.h"

#define private public
#include "AudioBusHandle.h"
#undef private

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
};

QTEST_GUILESS_MAIN(AudioBusPlayHandleQueueTest)
#include "AudioBusPlayHandleQueueTest.moc"
