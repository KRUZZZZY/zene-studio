/*
 * RecordingRealtimeTest.cpp - realtime-safety evidence for the recording and
 *                              capture paths: D9b (no lock on the capture
 *                              thread), D9c (no audio-thread allocation in the
 *                              clip record path), D9a (no unbounded growth)
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>
#include <utility>

#include "AllocationProbe.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlCompSupport.h"
#include "ControlRegistry.h"
#include "ProjectJournal.h"
#include "RecordOptions.h"
#include "RealtimeScope.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleFrameRingBuffer.h"
#include "SampleRecordAccumulator.h"
#include "SampleBuffer.h"
#include "SampleRecordHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SampleWindow.h"
#include "TakeLane.h"
#include "TimePos.h"
#include "Track.h"

using lmms::AudioEngine;
using lmms::f_cnt_t;
using lmms::sample_rate_t;
using lmms::sample_t;
using lmms::SampleFrame;
using lmms::SampleFrameRingBuffer;
using lmms::SampleRecordAccumulator;
using lmms::SampleRecordGaps;

namespace
{

// Exact float values (multiples of 1/64) so comparisons are exact.
sample_t testValue(std::uint64_t index)
{
	return static_cast<sample_t>(static_cast<long long>(index % 61) - 30) / 64.0f;
}

SampleFrame frameAt(std::uint64_t index)
{
	return SampleFrame(testValue(index), -testValue(index));
}

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

} // namespace


class RecordingRealtimeTest : public QObject
{
	Q_OBJECT

private:
	//! Feed \a periods periods of input and render them, the song recording.
	void driveRecordPeriods(int periods, float level = 0.5f, bool processEvents = false)
	{
		auto* engine = lmms::Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp), SampleFrame(level, level));
		for (int i = 0; i < periods; ++i)
		{
			engine->pushInputFrames(block.data(), fpp);
			engine->renderNextPeriod();
			// Paced like a device: the drain thread polls every 2 ms and must keep up.
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
			// Lets the main-thread reaper run between periods, as the event loop does in the app.
			if (processEvents) { QCoreApplication::processEvents(); }
		}
	}

	//! Feed captured input to a handle directly with one render per period, without transport timing.
	void feedInputToHandle(lmms::SampleRecordHandle* handle, int periods)
	{
		auto* engine = lmms::Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp), SampleFrame(0.5f, 0.5f));
		std::span<SampleFrame> emptyBuffer;
		for (int i = 0; i < periods; ++i)
		{
			engine->pushInputFrames(block.data(), fpp);
			engine->renderNextPeriod();
			handle->play(emptyBuffer);
		}
	}

	// ---- Q050-094: the record mode applied to an audio pass ------------------------------------------
	//! Frames of earlier audio the clip holds: longer than a pass, so a Replace shows as a shorter buffer.
	static constexpr std::size_t EarlierFrames = 20000;

	struct ArmedClip
	{
		lmms::SampleTrack* track = nullptr;
		lmms::SampleClip* clip = nullptr;
		int trackId = 0;
	};

	//! A track with one clip armed to record; with \a earlier it already holds EarlierFrames frames of that level.
	ArmedClip armedClip(bool hasEarlier, float earlier = 0.25f)
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		ArmedClip armed;
		armed.track = new lmms::SampleTrack(song);
		armed.trackId = armed.track->id();
		armed.clip = dynamic_cast<lmms::SampleClip*>(armed.track->createClip(lmms::TimePos(0)));
		if (hasEarlier)
		{
			std::vector<SampleFrame> old(EarlierFrames, SampleFrame(earlier, earlier));
			armed.clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(old.data(), old.size(),
				static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		}
		armed.clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		armed.clip->setAutoResize(false);
		armed.clip->setRecord(true);
		return armed;
	}

	//! One recording pass of constant \a level into the armed clip, ended by Stop and installed.
	void recordPass(lmms::SampleClip* clip, float level)
	{
		auto* song = lmms::Engine::getSong();
		song->playAndRecord();
		driveRecordPeriods(30, level);
		song->stop();
		QTRY_VERIFY2_WITH_TIMEOUT(!clip->isRecord(), "the pass did not end", 3000);
		QTest::qWait(50);
	}

	//! The clips of \a track other than \a shell, in track order.
	static std::vector<lmms::SampleClip*> others(lmms::Track* track, const lmms::Clip* shell)
	{
		std::vector<lmms::SampleClip*> out;
		for (lmms::Clip* c : track->getClips())
		{
			if (c != shell) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { out.push_back(sc); } }
		}
		return out;
	}

	static lmms::SampleTrack* trackById(int id)
	{
		for (lmms::Track* t : lmms::Engine::getSong()->tracks()) { if (t->id() == id) { return dynamic_cast<lmms::SampleTrack*>(t); } }
		return nullptr;
	}

	//! Highest and lowest left sample of \a clip's buffer.
	static std::pair<float, float> extremes(lmms::SampleClip& clip)
	{
		float high = -2.f, low = 2.f;
		const auto* data = clip.sample().buffer()->data();
		for (std::size_t i = 0; i < clip.sample().sampleSize(); ++i) { high = std::max(high, data[i].left()); low = std::min(low, data[i].left()); }
		return {high, low};
	}

private slots:
	void initTestCase()
	{
		// The engine-level cases below need a real AudioEngine. Qt needs a
		// platform plugin for that even though the test is guiless, which
		// tests/CMakeLists.txt arranges with QT_QPA_PLATFORM=offscreen.
		lmms::Engine::init(true);
		QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
		lmms::ControlRegistry::setReady(true);  // the agent surface, for the record.get_state test
		// Nothing may consume the capture staging ring while the bound and the
		// lock-independence of pushInputFrames() are measured.
		if (auto dev = lmms::Engine::audioEngine()->audioDev()) { dev->stopProcessing(); }
	}

	void cleanupTestCase()
	{
		lmms::ControlRegistry::setReady(false);
	}

	// -----------------------------------------------------------------------
	// D9a / staging ring: the input path is bounded and counts what it drops.
	// -----------------------------------------------------------------------
	void ringBufferIsBoundedAndCountsOverflow()
	{
		constexpr std::size_t capacity = 4096;
		constexpr std::size_t overflowBy = 1000;
		SampleFrameRingBuffer ring{capacity};
		QCOMPARE(asNumber(ring.capacity()), asNumber(capacity));

		std::vector<SampleFrame> block;
		for (std::size_t i = 0; i < capacity + overflowBy; ++i) { block.push_back(frameAt(i)); }

		const auto written = ring.writeBlock(block.data(), block.size());
		QCOMPARE(asNumber(written), asNumber(capacity));
		// Drop-newest: the unread data is never overwritten, and the drops are
		// counted rather than silently absorbed into unbounded storage.
		QCOMPARE(asNumber(ring.available()), asNumber(capacity));
		QCOMPARE(static_cast<qulonglong>(ring.overflowCount()), static_cast<qulonglong>(overflowBy));

		std::vector<SampleFrame> out(capacity);
		QCOMPARE(asNumber(ring.read(out.data(), out.size())), asNumber(capacity));
		for (std::size_t i = 0; i < capacity; ++i)
		{
			QCOMPARE(out[i].left(), block[i].left());
			QCOMPARE(out[i].right(), block[i].right());
		}
		QCOMPARE(asNumber(ring.available()), asNumber(0));

		// Once drained, the producer can fill it again - the capacity is
		// spatial, the counters are monotonic.
		QCOMPARE(asNumber(ring.writeBlock(block.data(), block.size())), asNumber(capacity));
	}

	//! The producer/consumer contract pushInputFrames()/drainInputStage() rely on.
	void ringBufferIsSpscUnderConcurrency()
	{
		constexpr std::size_t capacity = 1024;
		constexpr std::size_t totalFrames = 500000;
		SampleFrameRingBuffer ring{capacity};

		std::vector<SampleFrame> out;
		out.reserve(totalFrames);
		std::vector<SampleFrame> block(64);

		// Drop-newest overflow policy: a producer that must not lose frames
		// retries the part that did not fit, exactly as a capture backend does.
		std::thread producer([&] {
			std::size_t produced = 0;
			while (produced < totalFrames)
			{
				const auto batch = std::min(block.size(), totalFrames - produced);
				for (std::size_t i = 0; i < batch; ++i) { block[i] = frameAt(produced + i); }
				const auto written = ring.writeBlock(block.data(), batch);
				if (written == 0) { std::this_thread::yield(); }
				produced += written;
			}
		});

		std::vector<SampleFrame> scratch(128);
		while (out.size() < totalFrames)
		{
			const auto got = ring.read(scratch.data(), scratch.size());
			for (std::size_t i = 0; i < got; ++i) { out.push_back(scratch[i]); }
		}
		producer.join();

		// Nothing was lost and nothing was reordered across the handoff. The
		// producer retried the writes that did not fit, so the ring's
		// "rejected" counter is allowed to be non-zero here - it counts writes
		// the ring refused, not frames the take lost.
		QVERIFY2(out.size() >= totalFrames, "the consumer read fewer frames than were produced");
		for (std::size_t i = 0; i < totalFrames; ++i)
		{
			QVERIFY2(out[i].left() == testValue(i),
				qPrintable(QString("frame %1 out of order after the SPSC handoff").arg(i)));
		}
	}

	// -----------------------------------------------------------------------
	// D9c: the audio-thread entry point of the clip record path allocates
	//      nothing and never grows without bound.
	// -----------------------------------------------------------------------
	void accumulatorAppendPerformsNoAllocation()
	{
		std::vector<SampleFrame> block;
		for (std::size_t i = 0; i < 256; ++i) { block.push_back(frameAt(i)); }

		// Built before the probe is armed: the single ring allocation and the
		// drain thread belong to construction, not to the audio-thread path.
		SampleRecordAccumulator accum{48000};

		// A burst that fits in the ring, so this measures the append path and
		// not the drop policy (which the ring tests above cover).
		constexpr std::size_t blocks = 32;
		const auto expected = static_cast<std::uint64_t>(blocks) * block.size();
		QVERIFY(expected <= SampleRecordAccumulator::DefaultRingFrames);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (std::size_t i = 0; i < blocks; ++i)
		{
			accum.append(block.data(), static_cast<f_cnt_t>(block.size()));
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
		QCOMPARE(accum.framesStaged(), expected);
	}

	//! The take the drain thread assembles is complete, ordered and carries the
	//! recording sample rate - i.e. moving the work off the audio thread did
	//! not change what is recorded.
	void accumulatorTakeIsCompleteAndOrdered()
	{
		constexpr sample_rate_t rate = 44100;
		constexpr std::size_t blockFrames = 256;
		// Exactly one ring full, so it all fits and nothing is dropped.
		constexpr std::size_t blocks = SampleRecordAccumulator::DefaultRingFrames / blockFrames;

		SampleRecordAccumulator accum{rate, SampleRecordAccumulator::DefaultRingFrames};
		std::vector<SampleFrame> block(blockFrames);
		for (std::size_t b = 0; b < blocks; ++b)
		{
			for (std::size_t i = 0; i < blockFrames; ++i)
			{
				block[i] = frameAt(b * blockFrames + i);
			}
			accum.append(block.data(), static_cast<f_cnt_t>(blockFrames));
		}

		auto take = accum.finish();
		QVERIFY2(take != nullptr, "the accumulator produced no take");
		const auto expected = blocks * blockFrames;
		QCOMPARE(static_cast<qulonglong>(take->size()), static_cast<qulonglong>(expected));
		QCOMPARE(static_cast<int>(take->sampleRate()), static_cast<int>(rate));
		QCOMPARE(static_cast<qulonglong>(accum.framesDropped()), static_cast<qulonglong>(0));
		for (std::size_t i = 0; i < expected; ++i)
		{
			QVERIFY2(take->data()[i].left() == testValue(i),
				qPrintable(QString("frame %1 out of order in the assembled take").arg(i)));
		}
	}

	// -----------------------------------------------------------------------
	// D9b: the capture thread must not block behind the model lock.
	// -----------------------------------------------------------------------
	void pushInputFramesDoesNotBlockOnTheModelLock()
	{
		auto* engine = lmms::Engine::audioEngine();
		QVERIFY(engine != nullptr);

		// Hold the model lock (what a GUI thread does) for a bounded time on
		// another thread, so the measurement below cannot deadlock whatever the
		// result.
		constexpr int holdMs = 600;
		std::atomic<bool> holding{false};
		std::thread holder([&] {
			auto guard = engine->requestChangesGuard();
			holding.store(true, std::memory_order_release);
			std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
		});
		while (!holding.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		std::vector<SampleFrame> block(256, SampleFrame(0.25f, -0.25f));
		const auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < 64; ++i)
		{
			engine->pushInputFrames(block.data(), static_cast<f_cnt_t>(block.size()));
		}
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		holder.join();

		// With requestChangeInModel() still in pushInputFrames() this loop
		// blocks for the whole holdMs, because m_changeMutex is held by the
		// other thread. Without it the 64 pushes complete immediately.
		QVERIFY2(elapsedMs < holdMs / 2,
			qPrintable(QString("pushInputFrames took %1 ms while the model lock was held "
				"for %2 ms: the capture thread is still blocking on m_changeMutex")
				.arg(elapsedMs).arg(holdMs)));
	}

	//! D9a: a stopped device keeps pushing input, but the input path must not
	//! grow without bound. Every frame is either staged or counted as dropped,
	//! and the ring never holds more than its fixed capacity - a million
	//! offered frames do not become a million frames of storage.
	void inputStagingIsBoundedWhileTheDeviceIsStopped()
	{
		auto* engine = lmms::Engine::audioEngine();
		QVERIFY(engine != nullptr);

		constexpr std::size_t blockFrames = 1024;
		constexpr std::size_t pushes = 1000;
		std::vector<SampleFrame> block(blockFrames, SampleFrame(0.25f, -0.25f));

		// Relative to whatever the ring already holds: earlier cases in this
		// suite push into the same engine instance.
		const auto stagedBefore = static_cast<std::uint64_t>(engine->inputFramesStaged());
		const auto droppedBefore = engine->inputFramesDropped();
		for (std::size_t i = 0; i < pushes; ++i)
		{
			engine->pushInputFrames(block.data(), static_cast<f_cnt_t>(blockFrames));
		}
		const auto stagedAfter = static_cast<std::uint64_t>(engine->inputFramesStaged());
		const auto dropped = engine->inputFramesDropped() - droppedBefore;
		const auto total = static_cast<std::uint64_t>(pushes) * blockFrames;

		qInfo("pushed %llu frames: staged %llu -> %llu, dropped %llu",
			static_cast<unsigned long long>(total),
			static_cast<unsigned long long>(stagedBefore),
			static_cast<unsigned long long>(stagedAfter),
			static_cast<unsigned long long>(dropped));

		// Bounded: the capture path holds at most one fixed-capacity ring,
		// whatever is offered. The pre-fix code doubled its input buffer and
		// kept every frame, so this was unbounded growth.
		QVERIFY2(stagedAfter <= static_cast<std::uint64_t>(AudioEngine::InputStageCapacityFrames),
			"the input path holds more than its fixed staging capacity");
		QVERIFY2(dropped > 0, "the ring reported no overflow although far more was pushed than fits");
		// Conservation: no frame vanished silently, and none was stored.
		QCOMPARE(stagedAfter - stagedBefore + dropped, total);
	}

	// -----------------------------------------------------------------------
	// Clip take ownership: the audio thread neither builds a take's threads
	// and ring nor tears them down or writes the clip.
	// -----------------------------------------------------------------------

	//! Engine removes a record handle on the audio thread: its destructor must
	//! allocate nothing and must not write the clip (that is the main thread's
	//! job, done by the reaper). The clip still leaves record mode afterwards.
	void recordHandleDestructionPerformsNoAllocation()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);

		// Built on the main thread, as SampleClip does before the transport starts, and claimed
		// as the audio thread does when it starts the pass.
		auto* handle = new lmms::SampleRecordHandle(clip);
		handle->setArmGeneration(clip->armGeneration());  // as prepareRecordTake tags it
		handle->markClaimed();

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		delete handle;
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
		// The main thread installs the (empty) take and disarms the clip.
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 2000);
		delete track;
	}

	//! A claimed take is installed by the main-thread reaper, never by handle destruction.
	void claimedTakeCompletionIsDeferredToTheReaper()
	{
		lmms::SampleRecordHandle::pumpTakes();
		lmms::Engine::getSong()->stop();
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		std::vector<SampleFrame> original(8, SampleFrame(0.25f, 0.25f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(original,
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		clip->setRecord(true);
		auto* handle = clip->claimRecordHandle();
		QVERIFY(handle != nullptr);
		feedInputToHandle(handle, 40);
		const auto beforeSize = clip->sample().sampleSize();
		QSignalSpy changed(clip, &lmms::SampleClip::sampleChanged);

		delete handle;
		const bool untouchedByDestructor = clip->isRecord() && clip->sample().sampleSize() == beforeSize
			&& changed.isEmpty();
		lmms::SampleRecordHandle::reapRetiredTakes();
		const bool installedByReaper = !clip->isRecord();
		delete track;

		QVERIFY2(untouchedByDestructor,
			"destroying a claimed take changed clip state or emitted sampleChanged before reaping");
		QVERIFY2(installedByReaper, "the main-thread reaper did not install the completed take");
	}

	//! Arming a clip prepares its take on the main thread, so the audio thread
	//! only claims it: the claim allocates nothing.
	void armedClipClaimsItsPreparedTakeWithoutAllocation()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);

		clip->setRecord(true);

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		lmms::SampleRecordHandle* handle = clip->claimRecordHandle();
		lmms::test::tlCountAllocations = false;

		QVERIFY2(handle != nullptr, "the armed clip had no prepared take to claim");
		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
		// A standby take is ready for the next pass; after both are claimed there is none.
		lmms::SampleRecordHandle* standby = clip->claimRecordHandle();
		QVERIFY2(standby != nullptr, "no standby take was ready for the next pass");
		QVERIFY(clip->claimRecordHandle() == nullptr);
		delete standby;

		delete handle;
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 2000);
		delete track;
	}

	//! Starting an armed recording: the arming builds the take's accumulator and drain thread on the
	//! main thread, so the render callback that starts the pass only claims the prepared take and
	//! allocates nothing. The take still records the input the pass is given.
	//! Cluster 3508 regression: on the old code SampleTrack::play constructed the
	//! SampleRecordHandle (accumulator ring, scratch, shared state and std::thread) inside the
	//! render callback, which the allocation probe below catches.
	void recorderStartIsPreparedBeforeTheCallbackPath()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);

		// Arming on the main thread, before any playback: the accumulator (with its ring) and its
		// drain thread are built here, and both takes are ready before the transport ever rolls.
		clip->setRecord(true);
		QCOMPARE(clip->recordTakesReady(), 2);
		const auto unpreparedBefore = lmms::SampleRecordHandle::unpreparedTakes();

		// Playback starts only now, so the setup above provably precedes it. The first probed
		// period starts the pass (SampleTrack::play claims the prepared take and schedules the
		// handle): the callback path must allocate nothing.
		song->playAndRecord();

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		{
			const lmms::RealtimeScope realtime;
			for (int i = 0; i < 8; ++i) { song->processNextBuffer(); }
		}
		const auto counted = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(asNumber(counted), asNumber(0));
		// And the pass found the prepared take: no unprepared-take fallback was taken.
		QCOMPARE(lmms::SampleRecordHandle::unpreparedTakes(), unpreparedBefore);

		// And the take still records: drive real periods and stop.
		driveRecordPeriods(20);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QVERIFY2(clip->sample().sampleSize() > 0,
			qPrintable(QStringLiteral("the prepared take recorded nothing")));
		delete track;
	}

	// -----------------------------------------------------------------------
	// Behaviour of recording takes end to end (the engine is driven period by period).
	// -----------------------------------------------------------------------

	//! A claimed take records the input it is given, and the take lands in the clip.
	void claimedTakeRecordsItsInput()
	{
		lmms::Engine::getSong()->stop();
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(lmms::Engine::getSong()->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		lmms::Engine::getSong()->playAndRecord();
		driveRecordPeriods(40);
		lmms::Engine::getSong()->stop();

		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QVERIFY2(clip->sample().sampleSize() >= 10 * 256,
			qPrintable(QStringLiteral("the take holds %1 frames").arg(clip->sample().sampleSize())));
		delete track;
	}

	//! Recording pressed, then Play: the pass records (BUGS P2 - the press was lost on Play).
	void recordPressedThenPlayRecords()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		song->record();
		song->playSong();
		QVERIFY2(song->isRecording(), "Play after Record did not start a record pass");
		song->stop();
		song->playSong();
		QVERIFY2(!song->isRecording(), "a plain Play after a stop recorded");
		song->stop();
	}

	//! A clip armed while the transport already rolls takes its take on the next pass, instead of
	//! waiting for the playhead to leave the span (BUGS P3, the playing latch).
	void clipArmedWhileRollingRecordsOnTheNextPass()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		std::vector<lmms::SampleFrame> sample(100, lmms::SampleFrame(0.25f, 0.25f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(sample.data(), sample.size(),
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);

		song->playAndRecord();
		driveRecordPeriods(4);
		clip->setRecord(true);  // armed inside the span, while rolling
		driveRecordPeriods(40);
		song->stop();

		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QVERIFY2(clip->sample().sampleSize() > 100,
			qPrintable(QStringLiteral("the armed clip recorded nothing: %1 frames").arg(clip->sample().sampleSize())));
		delete track;
	}

	//! A loop wrap ends a pass: that pass becomes a take on a lane, and the clip stays armed for
	//! the next pass, which becomes a take too (BUGS P6 - loop record kept only the first pass).
	void loopWrapMakesEachPassATakeOnALane()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(20);
		clip->playbackPositionChanged();  // the playhead wraps: the live take ends here
		QTest::qWait(50);                 // the main thread installs the pass
		QVERIFY2(clip->isRecord(), "a loop wrap disarmed the clip");
		QCOMPARE(track->takeLanes().laneCount(), 1);

		driveRecordPeriods(20);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		// One lane per pass: the wrap's pass and the final pass.
		QCOMPARE(track->takeLanes().laneCount(), 2);
		int laneTakes = 0;
		for (lmms::Clip* c : track->getClips())
		{
			if (c != clip) { ++laneTakes; }
		}
		QCOMPARE(laneTakes, 2);
		delete track;
	}

	//! Disarming releases the clip's standby takes, so no drain thread is left polling.
	void disarmReleasesThePreparedTakes()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);
		QCOMPARE(clip->recordTakesReady(), 2);
		clip->setRecord(false);
		QCOMPARE(clip->recordTakesReady(), 0);
		delete track;
	}

	//! A take the clip never claimed is discarded: it does not disarm the clip that armed it.
	void unclaimedTakeLeavesTheClipArmed()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);
		auto* handle = new lmms::SampleRecordHandle(clip);
		delete handle;  // never claimed
		QTest::qWait(50);
		QVERIFY2(clip->isRecord(), "an unclaimed take disarmed the clip");
		delete track;
	}

	//! A clip deleted while its take is queued: the take is dropped and nothing is written to it.
	void clipDeletedBeforeTheTakeIsInstalledIsSafe()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);
		auto* handle = clip->claimRecordHandle();
		QVERIFY(handle != nullptr);
		handle->markClaimed();
		const auto reapedBefore = lmms::SampleRecordHandle::takesReaped();
		delete handle;   // queued for the main thread
		QPointer<lmms::SampleClip> gone(clip);
		delete track;    // the clip goes before the reaper runs
		QVERIFY(gone.isNull());
		// The reaper takes the queued take (deleting the track also queues the clip's standby takes,
		// so the count is not exactly one) and finds its clip gone: nothing is written to it.
		QTRY_VERIFY_WITH_TIMEOUT(lmms::SampleRecordHandle::takesReaped() > reapedBefore, 3000);
		QVERIFY(gone.isNull());
	}

	//! A take overflows the capture ring: the take keeps the length of the input it was given, with
	//! silence where the ring could not take it.
	void droppedFramesArePaddedWithSilenceInTheTake()
	{
		constexpr std::size_t ringFrames = 1024;
		constexpr std::size_t offered = 4096;
		SampleRecordAccumulator accum{48000, ringFrames};
		std::vector<SampleFrame> block(offered);
		for (std::size_t i = 0; i < offered; ++i)
		{
			const auto v = static_cast<sample_t>(1 + i % 50) / 64.0f;  // never zero
			block[i] = SampleFrame(v, v);
		}
		accum.append(block.data(), static_cast<f_cnt_t>(offered));
		const auto dropped = accum.framesDropped();
		QVERIFY2(dropped > 0, "the ring took everything; the test needs an overflow");

		auto take = accum.finish();
		QVERIFY(take != nullptr);
		QCOMPARE(static_cast<qulonglong>(take->size()), static_cast<qulonglong>(offered));
		const std::size_t written = offered - static_cast<std::size_t>(dropped);
		for (std::size_t i = 0; i < written; ++i)
		{
			QVERIFY2(take->data()[i].left() == block[i].left(),
				qPrintable(QString("frame %1 of the take is not the input").arg(i)));
		}
		for (std::size_t i = written; i < offered; ++i)
		{
			QVERIFY2(take->data()[i].left() == 0.0f && take->data()[i].right() == 0.0f,
				qPrintable(QString("frame %1 of the padded gap is not silence").arg(i)));
		}
	}

	//! A take whose handle cannot be queued for the main thread is counted, not lost silently.
	void queueFullIsCounted()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		// Start from an empty queue, whatever earlier tests left in it.
		lmms::SampleRecordHandle::pumpTakes();
		const auto before = lmms::SampleRecordHandle::queueOverflows();
		// The main thread reaps nothing in this loop: the queue holds 256 takes, the 257th overflows.
		for (int i = 0; i < 257; ++i)
		{
			auto* handle = new lmms::SampleRecordHandle(clip);
			delete handle;
		}
		QCOMPARE(static_cast<qulonglong>(lmms::SampleRecordHandle::queueOverflows() - before), static_cast<qulonglong>(1));
		QTest::qWait(50);  // the queued takes are discarded: none was claimed
		delete track;
	}

	//! A claimed take beyond the primary queue capacity still reaches its clip on reaping.
	void fullRetiredTakeQueueStillInstallsClaimedTake()
	{
		lmms::SampleRecordHandle::pumpTakes();
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		auto* fillerClip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(fillerClip != nullptr);
		std::vector<SampleFrame> original(8, SampleFrame(0.25f, 0.25f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(original,
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		lmms::Engine::getSong()->stop();
		clip->setRecord(true);
		auto* claimed = clip->claimRecordHandle();
		QVERIFY(claimed != nullptr);
		feedInputToHandle(claimed, 40);
		const auto overflowsBefore = lmms::SampleRecordHandle::queueOverflows();

		// Do not pump during this loop: the retired-take ring has 256 slots.
		for (int i = 0; i < 256; ++i)
		{
			auto* unclaimed = new lmms::SampleRecordHandle(fillerClip);
			delete unclaimed;
		}
		delete claimed;
		const auto overflowCount = lmms::SampleRecordHandle::queueOverflows() - overflowsBefore;

		lmms::SampleRecordHandle::reapRetiredTakes();
		const bool installed = !clip->isRecord() && clip->sample().sampleSize() > original.size();
		delete track;
		lmms::SampleRecordHandle::pumpTakes();

		QCOMPARE(static_cast<qulonglong>(overflowCount), static_cast<qulonglong>(1));
		QVERIFY2(installed, "the claimed take that exceeded the 256-slot queue was lost");
	}

	//! A pass that starts more clips than the per-pass cap defers the rest, and counts them.
	void clipStartsBeyondThePerPassCapAreDeferredAndCounted()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto before = lmms::SampleTrack::deferredClipStarts();
		std::vector<lmms::SampleFrame> sample(100, lmms::SampleFrame(0.25f, 0.25f));
		for (int i = 0; i < 65; ++i)
		{
			auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
			clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(sample.data(), sample.size(),
				static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
			clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		}
		track->play(lmms::TimePos(0), lmms::Engine::audioEngine()->framesPerPeriod(), 0, -1);
		QCOMPARE(static_cast<qulonglong>(lmms::SampleTrack::deferredClipStarts() - before), static_cast<qulonglong>(1));
		lmms::Engine::audioEngine()->clear();
		delete track;
	}

	//! The scheduling pass uses bounded inline storage even when a clip starts but is muted.
	void mutedSampleClipSchedulingDoesNotAllocate()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		std::vector<lmms::SampleFrame> sample(100, lmms::SampleFrame(0.25f, 0.25f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(sample.data(), sample.size(),
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setMuted(true); // schedule the clip without constructing a play handle.

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		{
			const lmms::RealtimeScope realtime;
			track->play(lmms::TimePos(0), lmms::Engine::audioEngine()->framesPerPeriod(), 0, -1);
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
		QVERIFY(clip->isPlaying());
		QVERIFY(track->isPlaying());
		delete track;
	}

	//! The render path publishes atomically; the GUI poll delivers playingChanged outside it.
	void sampleTrackPlayingChangeIsDeliveredByGuiPoll()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		std::vector<lmms::SampleFrame> sample(100, lmms::SampleFrame(0.25f, 0.25f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(sample.data(), sample.size(),
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setMuted(true);
		QSignalSpy playingChanged(track, SIGNAL(playingChanged()));
		QVERIFY(playingChanged.isValid());

		{
			const lmms::RealtimeScope realtime;
			track->play(lmms::TimePos(0), lmms::Engine::audioEngine()->framesPerPeriod(), 0, -1);
		}
		QVERIFY(track->isPlaying());
		QCOMPARE(playingChanged.count(), 0);
		QVERIFY(QMetaObject::invokeMethod(track, "notifyPlayingChanged", Qt::DirectConnection));
		QCOMPARE(playingChanged.count(), 1);
		QVERIFY(QMetaObject::invokeMethod(track, "notifyPlayingChanged", Qt::DirectConnection));
		QCOMPARE(playingChanged.count(), 1);

		track->setPlaying(false);
		QCOMPARE(playingChanged.count(), 1);
		QVERIFY(QMetaObject::invokeMethod(track, "notifyPlayingChanged", Qt::DirectConnection));
		QCOMPARE(playingChanged.count(), 2);
		delete track;
	}

	//! A missing prepared take clears the latch, so the next pass retries the claim.
	void failedRecordHandleClaimLeavesClipRetryable()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);
		song->playAndRecord();
		auto* first = clip->claimRecordHandle();
		auto* second = clip->claimRecordHandle();
		QVERIFY(first != nullptr && second != nullptr);
		QCOMPARE(clip->recordTakesReady(), 0);

		const auto before = lmms::SampleRecordHandle::unpreparedTakes();
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		bool returnedNoHandleOnFirstPass = false;
		{
			const lmms::RealtimeScope realtime;
			returnedNoHandleOnFirstPass = !track->play(lmms::TimePos(0), frames, 0, -1);
		}
		const bool latchClearedAfterFirstPass = !clip->isPlaying();
		const auto countAfterFirstPass = lmms::SampleRecordHandle::unpreparedTakes() - before;
		bool returnedNoHandleOnSecondPass = false;
		{
			const lmms::RealtimeScope realtime;
			returnedNoHandleOnSecondPass = !track->play(lmms::TimePos(0), frames, 0, -1);
		}
		const bool latchClearedAfterSecondPass = !clip->isPlaying();
		const auto countAfterSecondPass = lmms::SampleRecordHandle::unpreparedTakes() - before;

		song->stop();
		delete first;
		delete second;
		lmms::SampleRecordHandle::pumpTakes();
		delete track;

		QVERIFY(returnedNoHandleOnFirstPass && returnedNoHandleOnSecondPass);
		QVERIFY(latchClearedAfterFirstPass && latchClearedAfterSecondPass);
		QCOMPARE(countAfterFirstPass, std::uint64_t{1});
		QCOMPARE(countAfterSecondPass, std::uint64_t{2});
	}

	//! The gap table keeps every lost frame once it overflows the bounded track: 600 losses into
	//! 256 primary + 256 overflow records merge only the remainder into the newest record.
	void gapTableKeepsEveryLostFrameWhenItOverflows()
	{
		SampleRecordGaps gaps;  // default 256 primary + 256 overflow records
		for (std::uint64_t i = 0; i < 600; ++i) { gaps.note(i, 1); }
		QCOMPARE(gaps.merged(), static_cast<std::uint64_t>(88));

		std::uint64_t records = 0;
		std::uint64_t lost = 0;
		std::uint64_t lastLength = 0;
		while (const auto gap = gaps.takeDue(1000, true))
		{
			QCOMPARE(gap->position, records);  // stored position (merged extras stay at the newest record's position)
			lost += gap->length;
			lastLength = gap->length;
			++records;
		}
		QCOMPARE(records, static_cast<std::uint64_t>(512));
		QCOMPARE(lost, static_cast<std::uint64_t>(600));
		QCOMPARE(lastLength, static_cast<std::uint64_t>(89));  // the newest overflow record took 1 + 88
	}

	//! Overflow into the preallocated track keeps each loss's position: a tiny table (2 primary +
	//! 2 overflow) plus one merged loss stores the first four loss positions unchanged.
	void gapOverflowPreservesLossPositions()
	{
		SampleRecordGaps gaps{2};
		gaps.note(0, 5);
		gaps.note(10, 3);
		gaps.note(11, 2);  // overflows the primary track into the preallocated overflow track
		gaps.note(20, 4);  // another overflow record, still bounded
		gaps.note(30, 1);  // beyond the bound: merges into the newest record

		QCOMPARE(gaps.merged(), static_cast<std::uint64_t>(1));

		std::vector<std::pair<std::uint64_t, std::uint64_t>> got;
		while (const auto gap = gaps.takeDue(1000, true))
		{
			got.emplace_back(gap->position, gap->length);
		}
		QCOMPARE(static_cast<qulonglong>(got.size()), static_cast<qulonglong>(4));
		// Positions are preserved across the primary -> overflow handover instead of being
		// pulled back to the first overflow's older position.
		QCOMPARE(static_cast<qulonglong>(got[0].first), static_cast<qulonglong>(0));
		QCOMPARE(static_cast<qulonglong>(got[0].second), static_cast<qulonglong>(5));
		QCOMPARE(static_cast<qulonglong>(got[1].first), static_cast<qulonglong>(10));
		QCOMPARE(static_cast<qulonglong>(got[1].second), static_cast<qulonglong>(3));
		QCOMPARE(static_cast<qulonglong>(got[2].first), static_cast<qulonglong>(11));
		QCOMPARE(static_cast<qulonglong>(got[2].second), static_cast<qulonglong>(2));
		QCOMPARE(static_cast<qulonglong>(got[3].first), static_cast<qulonglong>(20));
		QCOMPARE(static_cast<qulonglong>(got[3].second), static_cast<qulonglong>(5));  // 4 + 1 merged
	}

	//! A recording whose gap table keeps each separated loss on its own record preserves the
	//! exact nonzero/silence ordering end to end, instead of shifting a later accepted block.
	void accumulatorPreservesSampleOrderAcrossSeparatedLosses()
	{
		constexpr std::size_t ringFrames = 8;
		constexpr std::size_t gapSlots = 1;  // 1 primary + 1 preallocated overflow record
		SampleRecordAccumulator accum{48000, ringFrames, gapSlots};

		std::vector<SampleFrame> expected;
		std::uint64_t nextValue = 1;
		auto pushBurst = [&]() {
			constexpr std::size_t burstFrames = ringFrames + 1;  // one frame past the ring's capacity
			std::vector<SampleFrame> block(burstFrames);
			for (std::size_t i = 0; i < burstFrames; ++i)
			{
				block[i] = SampleFrame(static_cast<sample_t>(nextValue + i),
					static_cast<sample_t>(nextValue + i));
			}
			nextValue += burstFrames;
			for (std::size_t i = 0; i < ringFrames; ++i) { expected.push_back(block[i]); }
			expected.emplace_back();  // the single dropped frame is padded as silence
			accum.append(block.data(), static_cast<f_cnt_t>(burstFrames));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		};

		// Two losses exceed the primary track (1 slot) but fit the combined table (2 slots).
		for (int i = 0; i < 2; ++i) { pushBurst(); }

		QVERIFY2(accum.framesDropped() == 2, "each burst should have dropped exactly one frame");

		auto take = accum.finish();
		QVERIFY2(take != nullptr, "the accumulator produced no take");
		QCOMPARE(static_cast<qulonglong>(take->size()), static_cast<qulonglong>(expected.size()));
		for (std::size_t i = 0; i < expected.size(); ++i)
		{
			QVERIFY2(take->data()[i].left() == expected[i].left() && take->data()[i].right() == expected[i].right(),
				qPrintable(QString("frame %1 of the take does not match the expected sequence").arg(i)));
		}
	}

	//! A take whose gap table overflows still has its full length, and every dropped frame is
	//! silence in it: the table never turns a loss into a shorter take.
	void droppedFramesArePaddedEvenWhenTheGapTableOverflows()
	{
		constexpr std::size_t ringFrames = 1024;
		constexpr std::size_t gapSlots = 2;
		constexpr std::size_t bursts = 200;
		SampleRecordAccumulator accum{48000, ringFrames, gapSlots};
		std::vector<SampleFrame> block(ringFrames + 1);  // one frame more than the ring holds
		for (std::size_t i = 0; i < block.size(); ++i)
		{
			const auto v = static_cast<sample_t>(1 + i % 50) / 64.0f;  // never zero
			block[i] = SampleFrame(v, v);
		}
		for (std::size_t b = 0; b < bursts; ++b)
		{
			accum.append(block.data(), static_cast<f_cnt_t>(block.size()));
		}
		const auto dropped = accum.framesDropped();
		QVERIFY2(dropped >= bursts, "every burst should have overflowed the ring");

		auto take = accum.finish();
		QVERIFY(take != nullptr);
		const auto offered = bursts * block.size();
		QCOMPARE(static_cast<qulonglong>(take->size()), static_cast<qulonglong>(offered));
		std::size_t silent = 0;
		for (std::size_t i = 0; i < take->size(); ++i)
		{
			if (take->data()[i].left() == 0.0f && take->data()[i].right() == 0.0f) { ++silent; }
		}
		QCOMPARE(static_cast<qulonglong>(silent), static_cast<qulonglong>(dropped));
	}

	//! A take of an earlier arming reaches the main thread after the clip was disarmed and armed
	//! again: it is installed without disarming the new arming (the race within the reaper's 10 ms).
	void reArmedClipIsNotDisarmedByAnEarlierTake()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);
		lmms::SampleRecordHandle* earlier = clip->claimRecordHandle();
		QVERIFY2(earlier != nullptr, "the first arming had no take");
		earlier->markClaimed();

		clip->setRecord(false);  // the first arming ends...
		clip->setRecord(true);   // ...and a second one starts before the take is reaped
		delete earlier;          // the first arming's take is queued now
		QTest::qWait(50);
		QVERIFY2(clip->isRecord(), "an earlier arming's take disarmed the re-armed clip");
		delete track;
	}

	//! The pass count belongs to the arming it counted: a clip armed again after a loop pass
	//! records its first take as the clip's own content, not as one more take on a lane
	//! (BUG 11, new-p12-206 - the old arming's count was carried into the new arming).
	void reArmingAfterALoopPassTakesItsFirstPassOverTheClip()
	{
		auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(lmms::Engine::getSong()->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		// A loop wrap ended a pass while the transport still recorded: it became a lane take,
		// and the clip stayed armed for the next pass.
		std::vector<lmms::SampleFrame> pass(64, lmms::SampleFrame(0.25f, 0.25f));
		clip->acceptRecordedTake(
			std::make_shared<lmms::SampleBuffer>(pass.data(), pass.size(),
				static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())),
			0, true, clip->armGeneration());
		QVERIFY2(clip->isRecord(), "a loop pass disarmed the clip");
		QCOMPARE(track->takeLanes().laneCount(), 1);

		// The user disarms after the loop pass and arms again.
		clip->setRecord(false);
		clip->setRecord(true);

		// The first take of this arming ends the recording (the transport stopped).
		std::vector<lmms::SampleFrame> first(96, lmms::SampleFrame(0.5f, 0.5f));
		clip->acceptRecordedTake(
			std::make_shared<lmms::SampleBuffer>(first.data(), first.size(),
				static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())),
			0, false, clip->armGeneration());

		QVERIFY2(!clip->isRecord(), "the recording pass did not disarm the clip");
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(96));
		QCOMPARE(track->takeLanes().laneCount(), 1);  // only the loop pass' lane
		delete track;
	}

	//! Three loop passes make three takes; the newest lane is active, and Play all takes sums them.
	void onlyTheNewestLoopPassIsAudible()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(30, 0.05f);
		clip->playbackPositionChanged();  // loop wrap 1 ends pass 1
		QTest::qWait(50);
		driveRecordPeriods(30, 0.1f);
		clip->playbackPositionChanged();  // loop wrap 2 ends pass 2
		QTest::qWait(50);
		driveRecordPeriods(30, 0.2f);
		song->stop();                     // pass 3 ends with the stop
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);

		QCOMPARE(track->takeLanes().laneCount(), 3);
		std::vector<lmms::Clip*> takes;
		for (lmms::Clip* c : track->getClips()) { if (c != clip) { takes.push_back(c); } }
		QCOMPARE(takes.size(), static_cast<std::size_t>(3));
		for (lmms::Clip* take : takes) { QVERIFY(!take->isMuted()); }
		QCOMPARE(takes[0]->takeGroupId(), clip->id());
		QCOMPARE(takes[1]->takeGroupId(), clip->id());
		QCOMPARE(takes[2]->takeGroupId(), clip->id());
		QCOMPARE(track->takeLanes().activeTakeLane(clip->id()), takes[2]->laneIndex());
		QVERIFY(!track->takeLanes().userChoseTake(clip->id()));

		auto* engine = lmms::Engine::audioEngine();
		const auto peak = [&] {
			float p = 0.0f;
			for (int i = 0; i < 10; ++i)  // skip the round trip, then measure
			{
				const auto out = engine->renderNextPeriod();
				if (i < 3) { continue; }
				for (const auto& f : out) { p = std::max(p, std::abs(f.left())); }
			}
			return p;
		};
		song->playSong();
		const float audibleOnly = peak();
		song->stop();
		QVERIFY2(audibleOnly > 0.0f, "the newest take was silent");

		const auto all = lmms::ControlRegistry::instance()->invoke(QStringLiteral("comp.play_all_takes"),
			{{QStringLiteral("track"), lmms::control::trackIdOf(track)},
			 {QStringLiteral("enabled"), true}});
		QVERIFY2(all.ok, qPrintable(all.errorMessage));
		song->playSong();
		const float allAudible = peak();
		song->stop();
		QVERIFY(track->takeLanes().playAllTakes());
		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("comp.play_all_takes"),
			{{QStringLiteral("track"), lmms::control::trackIdOf(track)},
			 {QStringLiteral("enabled"), false}}).ok);
		const float ratio = allAudible / audibleOnly;
		QVERIFY2(ratio > 1.6f && ratio < 1.9f,
			qPrintable(QStringLiteral("the earlier passes did not sound in the unmuted render: ratio %1").arg(ratio)));
		delete track;
	}

	//! The transport starts a pass on an armed clip whose takes are all in use: the pass is
	//! counted as unprepared and records nothing, end to end through SampleTrack::play.
	void unpreparedTakeIsCountedAndRecordsNothing()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		// Starting the transport re-prepares the takes, so they are used up after it starts.
		song->playAndRecord();
		lmms::SampleRecordHandle* first = clip->claimRecordHandle();
		lmms::SampleRecordHandle* second = clip->claimRecordHandle();
		QVERIFY(first != nullptr && second != nullptr);
		QCOMPARE(clip->recordTakesReady(), 0);

		const auto before = lmms::SampleRecordHandle::unpreparedTakes();
		driveRecordPeriods(10);
		song->stop();
		QVERIFY2(lmms::SampleRecordHandle::unpreparedTakes() > before,
			"a pass without a prepared take was not counted");
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(0));  // nothing was recorded
		QCOMPARE(track->getClips().size(), static_cast<std::size_t>(1));     // and no lane take

		delete first;   // they were claimed: their drain threads stop at the reaper
		delete second;
		QTest::qWait(50);
		delete track;
	}

	//! A take that lost frames reaches the user twice: as the Song's drop signal (the GUI shows
	//! its notice from it), and as the clip's count in the agent surface's record.get_state.
	void droppedFramesReachTheSongAndTheCommandSurface()
	{
		auto* song = lmms::Engine::getSong();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setRecord(true);

		QSignalSpy notices(song, SIGNAL(recordTakeDropped(qulonglong)));
		clip->acceptRecordedTake(nullptr, 5, false, clip->armGeneration());
		QCOMPARE(notices.count(), 1);
		QCOMPARE(notices.at(0).at(0).toULongLong(), static_cast<qulonglong>(5));
		QCOMPARE(clip->lastTakeDroppedFrames(), static_cast<std::uint64_t>(5));

		const auto reply = lmms::ControlRegistry::instance()->invoke(QStringLiteral("record.get_state"), {});
		QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
		const QJsonArray clips = reply.result.value(QStringLiteral("clip_takes")).toObject()
			.value(QStringLiteral("clips")).toArray();
		bool listed = false;
		for (const QJsonValue& entry : clips)
		{
			const QJsonObject row = entry.toObject();
			if (row.value(QStringLiteral("clip_id")).toInt() != clip->id()) { continue; }
			listed = true;
			QCOMPARE(row.value(QStringLiteral("last_take_dropped_frames")).toVariant().toULongLong(),
				static_cast<qulonglong>(5));
		}
		QVERIFY2(listed, "record.get_state does not list the clip whose take lost frames");
		delete track;
	}

	//! Three loop passes, each ended by a wrap while the reaper runs between periods: every pass
	//! keeps the length of the pass. A lane take must not cut the next pass short (BUGS P6, round 4).
	void loopPassesKeepTheirLengthWhileTheReaperRuns()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		constexpr int passPeriods = 40;
		const auto fpp = static_cast<std::size_t>(lmms::Engine::audioEngine()->framesPerPeriod());
		song->playAndRecord();
		for (int pass = 0; pass < 3; ++pass)
		{
			driveRecordPeriods(passPeriods, 0.25f, /*processEvents=*/true);
			if (pass < 2) { song->setPlayPos(0); }  // the loop wrap: the transport jumps back to the loop start
		}
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);

		std::vector<lmms::Clip*> takes;
		for (lmms::Clip* c : track->getClips()) { if (c != clip) { takes.push_back(c); } }
		QCOMPARE(takes.size(), static_cast<std::size_t>(3));
		// Every pass loses the same round trip at its start, so the three takes agree to within one
		// period; a cut pass is shorter than the others by the reaper's latency (about two periods).
		std::size_t shortest = ~std::size_t{0};
		std::size_t longest = 0;
		for (lmms::Clip* take : takes)
		{
			const auto length = static_cast<std::size_t>(dynamic_cast<lmms::SampleClip*>(take)->sample().sampleSize());
			qInfo("pass take lane %d: %llu frames", take->laneIndex(), static_cast<qulonglong>(length));
			shortest = std::min(shortest, length);
			longest = std::max(longest, length);
		}
		QVERIFY2(longest - shortest <= fpp,
			qPrintable(QStringLiteral("the passes differ by %1 frames: a pass was cut short")
				.arg(static_cast<qulonglong>(longest - shortest))));
		QVERIFY2(shortest + 4 * fpp >= passPeriods * fpp,
			qPrintable(QStringLiteral("a pass of %1 periods came out as %2 frames")
				.arg(passPeriods).arg(static_cast<qulonglong>(shortest))));
		delete track;
	}

	//! The take clips of a track, in lane order, without the armed clip itself.
	static std::vector<lmms::Clip*> laneTakesOf(lmms::SampleTrack* track, lmms::Clip* armed)
	{
		std::vector<lmms::Clip*> takes;
		for (lmms::Clip* c : track->getClips()) { if (c != armed) { takes.push_back(c); } }
		std::sort(takes.begin(), takes.end(), [](lmms::Clip* a, lmms::Clip* b) { return a->laneIndex() < b->laneIndex(); });
		return takes;
	}

	//! A user's take choice survives later loop passes; no take's mute flag is changed.
	void aUserChoiceSurvivesLaterLoopPasses()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(30, 0.05f);
		song->setPlayPos(0);
		QTest::qWait(50);
		driveRecordPeriods(30, 0.1f);
		song->setPlayPos(0);
		QTest::qWait(50);
		auto takes = laneTakesOf(track, clip);
		QCOMPARE(takes.size(), static_cast<std::size_t>(2));
		QVERIFY(!takes[0]->isMuted());
		const int chosenLane = takes[0]->laneIndex();
		const auto chosen = lmms::ControlRegistry::instance()->invoke(QStringLiteral("comp.take_select"),
			{{QStringLiteral("clip"), lmms::control::clipId(takes[0]->id())},
			 {QStringLiteral("lane"), chosenLane}});
		QVERIFY2(chosen.ok, qPrintable(chosen.errorMessage));
		QVERIFY(track->takeLanes().userChoseTake(clip->id()));
		QCOMPARE(track->takeLanes().activeTakeLane(clip->id()), chosenLane);

		driveRecordPeriods(30, 0.2f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		takes = laneTakesOf(track, clip);
		QCOMPARE(takes.size(), static_cast<std::size_t>(3));
		for (lmms::Clip* take : takes) { QVERIFY(!take->isMuted()); }
		QVERIFY2(track->takeLanes().userChoseTake(clip->id()), "a later pass cleared the user's take choice");
		QCOMPARE(track->takeLanes().activeTakeLane(clip->id()), chosenLane);
		delete track;
	}

	//! A second arming contributes newer takes without muting the first arming's takes.
	void aNewArmingLeavesEarlierTakesAlone()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);

		clip->setRecord(true);
		song->playAndRecord();
		driveRecordPeriods(30, 0.05f);
		song->setPlayPos(0);
		QTest::qWait(50);
		driveRecordPeriods(30, 0.1f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		auto takes = laneTakesOf(track, clip);
		QCOMPARE(takes.size(), static_cast<std::size_t>(2));
		QVERIFY(track->takeLanes().activeTakeLane(clip->id()) == takes[1]->laneIndex());

		clip->setRecord(true);          // a second arming
		song->playAndRecord();
		driveRecordPeriods(30, 0.15f);
		song->setPlayPos(0);
		QTest::qWait(50);
		driveRecordPeriods(30, 0.2f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		takes = laneTakesOf(track, clip);
		QCOMPARE(takes.size(), static_cast<std::size_t>(4));
		for (lmms::Clip* take : takes) { QVERIFY(!take->isMuted()); }
		QCOMPARE(track->takeLanes().activeTakeLane(clip->id()), takes[3]->laneIndex());
		delete track;
	}

	//! A comped take plays its comped span even when its mute flag is set: the comp decides.
	void aComposedMutedTakePlaysItsSpan()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);
		song->playAndRecord();
		driveRecordPeriods(30, 0.05f);
		song->setPlayPos(0);
		QTest::qWait(50);
		driveRecordPeriods(30, 0.2f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		auto takes = laneTakesOf(track, clip);
		QCOMPARE(takes.size(), static_cast<std::size_t>(2));
		takes[0]->setMuted(true); // comp playback remains authoritative over the normal active-take gate.

		// Comp the whole bar from the first take's lane, then from the second's, and compare the peaks.
		const auto bar = song->ticksPerBar();
		auto* engine = lmms::Engine::audioEngine();
		const auto peakOfComp = [&](int lane) {
			track->takeLanes().selectSegment(0, bar, lane);
			song->playSong();
			float p = 0.0f;
			for (int i = 0; i < 12; ++i)
			{
				const auto out = engine->renderNextPeriod();
				if (i < 4) { continue; }  // the round trip
				for (const auto& f : out) { p = std::max(p, std::abs(f.left())); }
			}
			song->stop();
			return p;
		};
		const float fromMuted = peakOfComp(takes[0]->laneIndex());
		const float fromNewest = peakOfComp(takes[1]->laneIndex());
		QVERIFY2(fromMuted > 0.0f, "the comp named a muted take and nothing played");
		const float ratio = fromMuted / fromNewest;
		QVERIFY2(ratio > 0.15f && ratio < 0.35f,
			qPrintable(QStringLiteral("the comped first take sounds %1 of the second (expected 0.25)").arg(ratio)));
		delete track;
	}

	//! One undo step takes back a whole recording: its lanes and take selection state.
	void oneUndoStepTakesBackARecording()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);
		song->playAndRecord();
		driveRecordPeriods(20, 0.05f);
		song->setPlayPos(0);
		QTest::qWait(50);
		driveRecordPeriods(20, 0.1f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		QCOMPARE(track->takeLanes().laneCount(), 2);

		const int depth = lmms::Engine::projectJournal()->undoDepth();
		lmms::Engine::projectJournal()->undo();  // one step
		QCOMPARE(lmms::Engine::projectJournal()->undoDepth(), depth - 1);
		lmms::SampleTrack* restored = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { restored = dynamic_cast<lmms::SampleTrack*>(t); } }
		QVERIFY2(restored != nullptr, "undo lost the track");
		QCOMPARE(restored->takeLanes().laneCount(), 0);
		int takes = 0;
		for (lmms::Clip* c : restored->getClips()) { if (c->laneIndex() == 0 && dynamic_cast<lmms::SampleClip*>(c) && static_cast<lmms::SampleClip*>(c)->sample().sampleSize() > 0) { ++takes; } }
		QCOMPARE(takes, 0);
		restored->deleteLater();
		QTest::qWait(10);
	}

	//! Record, then Stop while stopped, then Play: the Play is a plain play (no record latch left).
	void recordThenStopWhileStoppedLeavesNoLatch()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		song->record();
		song->stop();  // already stopped: the early return must not keep the latch
		song->playSong();
		QVERIFY2(!song->isRecording(), "a Play after Record and Stop started a record pass");
		song->stop();
	}

	//! A plain recording is one undo step: one control.undo puts the clip back as it was before the
	//! arm (empty audio, disarmed, no takes prepared), and control.redo brings the recording back.
	void aPlainRecordingIsOneUndoStep()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(0));

		song->playAndRecord();
		driveRecordPeriods(30, 0.1f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		const auto recorded = clip->sample().sampleSize();
		QVERIFY2(recorded > 0, "the plain recording recorded nothing");

		const auto undo = lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		lmms::SampleTrack* restored = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { restored = dynamic_cast<lmms::SampleTrack*>(t); } }
		QVERIFY2(restored != nullptr, "undo lost the track");
		lmms::SampleClip* back = nullptr;
		for (lmms::Clip* c : restored->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } }
		QVERIFY(back != nullptr);
		QCOMPARE(back->sample().sampleSize(), static_cast<std::size_t>(0));  // the pre-record audio
		QVERIFY2(!back->isRecord(), "undo left the clip armed");
		QCOMPARE(back->recordTakesReady(), 0);
		QCOMPARE(restored->takeLanes().laneCount(), 0);

		const auto redo = lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.redo"), {});
		QVERIFY2(redo.ok, qPrintable(redo.errorMessage));
		restored = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { restored = dynamic_cast<lmms::SampleTrack*>(t); } }
		QVERIFY(restored != nullptr);
		back = nullptr;
		for (lmms::Clip* c : restored->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } }
		QVERIFY(back != nullptr);
		QCOMPARE(back->sample().sampleSize(), recorded);  // the recording is back
		QVERIFY(!back->isRecord());
		delete restored;
		QTest::qWait(10);
	}

	//! An undo pressed while a loop recording is still armed restores the arm's state as one step:
	//! the lanes go, the clip is disarmed and no take stays prepared.
	void anUndoDuringALoopRecordingRestoresTheArmedState()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(20, 0.1f, true);
		song->setPlayPos(0);            // a loop wrap: one lane take
		QTest::qWait(50);
		driveRecordPeriods(20, 0.2f, true);
		QCOMPARE(track->takeLanes().laneCount(), 1);
		QVERIFY(clip->isRecord());      // still armed: the loop is still recording

		const auto undo = lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		// The undo rebuilds the track's clips: `clip` is gone, so look the restored clip up again.
		lmms::SampleTrack* restored = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { restored = dynamic_cast<lmms::SampleTrack*>(t); } }
		QVERIFY2(restored != nullptr, "undo lost the track");
		QCOMPARE(restored->takeLanes().laneCount(), 0);
		lmms::SampleClip* back = nullptr;
		for (lmms::Clip* c : restored->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } }
		QVERIFY(back != nullptr);
		QVERIFY2(!back->isRecord(), "one undo left the clip armed");
		QCOMPARE(back->recordTakesReady(), 0);

		song->stop();                   // the live take ends; the disarmed clip is not re-armed by it
		QTest::qWait(50);
		QCOMPARE(restored->takeLanes().laneCount(), 0);  // and it installed no lane take
		QVERIFY(!back->isRecord());
	}

	//! A recording over an existing take: one undo brings the earlier take back, and redo the recording.
	void aRecordingOverAnExistingTakeIsUndoneToIt()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		std::vector<SampleFrame> earlier(100, SampleFrame(0.375f, 0.375f));
		clip->setSampleBuffer(std::make_shared<lmms::SampleBuffer>(earlier.data(), earlier.size(),
			static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate())));
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(30, 0.1f);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!clip->isRecord(), 3000);
		QTest::qWait(50);
		const auto recorded = clip->sample().sampleSize();
		QVERIFY2(recorded > 100, "the recording did not replace the take");

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {}).ok);
		lmms::SampleClip* back = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { for (lmms::Clip* c : t->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } } } }
		QVERIFY(back != nullptr);
		QCOMPARE(back->sample().sampleSize(), static_cast<std::size_t>(100));  // the earlier take is back
		QVERIFY(!back->isRecord());

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.redo"), {}).ok);
		back = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { for (lmms::Clip* c : t->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } } } }
		QVERIFY(back != nullptr);
		QCOMPARE(back->sample().sampleSize(), recorded);  // and the recording is back
		delete back->getTrack();
		QTest::qWait(10);
	}

	//! An undo pressed mid-pass ends the live recording it disarms: the transport stops afterwards and
	//! nothing is installed - no lane take, no audio on the clip. The clip is in its pre-arm state.
	void anUndoMidPassInstallsNothingAfterwards()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		clip->setRecord(true);

		song->playAndRecord();
		driveRecordPeriods(20, 0.1f, true);
		song->setPlayPos(0);            // a loop wrap: one lane take
		QTest::qWait(50);
		driveRecordPeriods(20, 0.2f, true);  // mid-pass: the live take is recording
		QCOMPARE(track->takeLanes().laneCount(), 1);

		const auto undo = lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		song->stop();                   // the live take would be installed here, were it not discarded
		QTest::qWait(100);

		lmms::SampleTrack* restored = nullptr;
		for (lmms::Track* t : song->tracks()) { if (t->id() == trackId) { restored = dynamic_cast<lmms::SampleTrack*>(t); } }
		QVERIFY2(restored != nullptr, "undo lost the track");
		QCOMPARE(restored->takeLanes().laneCount(), 0);
		int clips = 0;
		for (lmms::Clip* c : restored->getClips()) { ++clips; QCOMPARE(c->laneIndex(), 0); }
		QCOMPARE(clips, 1);
		lmms::SampleClip* back = nullptr;
		for (lmms::Clip* c : restored->getClips()) { if (auto* sc = dynamic_cast<lmms::SampleClip*>(c)) { back = sc; } }
		QVERIFY(back != nullptr);
		QCOMPARE(back->sample().sampleSize(), static_cast<std::size_t>(0));
		QVERIFY(!back->isRecord());
		QCOMPARE(back->recordTakesReady(), 0);
		delete restored;
		QTest::qWait(10);
	}


	// ---- Q050-094: Overdub, Replace and Keep History for an audio pass ---------------------------------
	//! Overdub: the earlier audio stays, the pass is mixed into it (0.25 + 0.125 where both sound), and one
	//! undo brings the clip back as it was.
	void anOverdubPassIsMixedIntoTheEarlierAudio()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::Overdub});
		ArmedClip armed = armedClip(true);
		recordPass(armed.clip, 0.125f);
		QCOMPARE(armed.clip->sample().sampleSize(), EarlierFrames);  // the earlier audio's length
		QCOMPARE(armed.track->takeLanes().laneCount(), 0);
		const auto range = extremes(*armed.clip);
		QVERIFY2(range.second >= 0.25f - 1e-6f, "the earlier audio was lost where the pass sounds");
		QVERIFY2(std::abs(range.first - 0.375f) < 1e-6f, qPrintable(QStringLiteral("no frame holds earlier + pass: %1").arg(range.first)));

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {}).ok);
		lmms::SampleTrack* restored = trackById(armed.trackId);
		QVERIFY(restored != nullptr);
		auto* back = dynamic_cast<lmms::SampleClip*>(restored->getClips().front());
		QCOMPARE(back->sample().sampleSize(), EarlierFrames);
		const auto undone = extremes(*back);
		QVERIFY(std::abs(undone.first - 0.25f) < 1e-6f && std::abs(undone.second - 0.25f) < 1e-6f);  // exactly the earlier audio
		delete restored;
		lmms::recordoptions::setOptions({});
	}

	//! Replace: the pass is the clip's audio (shorter than the earlier audio, none of it left); undo restores it.
	void aReplacePassReplacesTheEarlierAudio()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::Replace});
		ArmedClip armed = armedClip(true);
		recordPass(armed.clip, 0.125f);
		QVERIFY2(armed.clip->sample().sampleSize() < EarlierFrames, "the pass did not replace the earlier audio");
		QVERIFY(armed.clip->sample().sampleSize() > 0);
		QCOMPARE(armed.track->takeLanes().laneCount(), 0);
		QVERIFY2(extremes(*armed.clip).first <= 0.125f + 1e-6f, "earlier audio is still in the clip");

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {}).ok);
		lmms::SampleTrack* restored = trackById(armed.trackId);
		QVERIFY(restored != nullptr);
		auto* back = dynamic_cast<lmms::SampleClip*>(restored->getClips().front());
		QCOMPARE(back->sample().sampleSize(), EarlierFrames);
		QVERIFY(std::abs(extremes(*back).first - 0.25f) < 1e-6f);
		delete restored;
		lmms::recordoptions::setOptions({});
	}

	//! Keep History: the earlier audio and the pass are two take lanes of the clip's group, the pass is active,
	//! the earlier one can be chosen again, and the next pass is active again; one undo restores the clip.
	void aKeepHistoryPassKeepsTheEarlierAudioAsATakeLane()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::KeepHistory});
		ArmedClip armed = armedClip(true);
		const int groupId = armed.clip->id();
		recordPass(armed.clip, 0.125f);

		QCOMPARE(armed.track->takeLanes().laneCount(), 2);
		QCOMPARE(armed.clip->sample().sampleSize(), std::size_t{0});  // the shell the lanes hang from
		QCOMPARE(armed.clip->length(), lmms::TimePos(lmms::Engine::getSong()->ticksPerBar()));  // and it keeps its length
		const auto takes = others(armed.track, armed.clip);
		QCOMPARE(takes.size(), std::size_t{2});
		lmms::SampleClip* history = nullptr;
		lmms::SampleClip* pass = nullptr;
		for (auto* take : takes)
		{
			QCOMPARE(take->takeGroupId(), groupId);
			QVERIFY(!take->isMuted());
			(take->sample().sampleSize() == EarlierFrames ? history : pass) = take;
		}
		QVERIFY2(history != nullptr && pass != nullptr, "the earlier audio and the pass are not two separate takes");
		QVERIFY(history->laneIndex() != pass->laneIndex());
		QVERIFY(std::abs(extremes(*history).first - 0.25f) < 1e-6f && std::abs(extremes(*history).second - 0.25f) < 1e-6f);
		QVERIFY(extremes(*pass).first <= 0.125f + 1e-6f);
		QCOMPARE(armed.track->takeLanes().activeTakeLane(groupId), pass->laneIndex());  // the new pass is active

		// The earlier audio is recoverable: choose its lane through the take-group command.
		const auto chosen = lmms::ControlRegistry::instance()->invoke(QStringLiteral("comp.take_select"),
			{{QStringLiteral("clip"), lmms::control::takeClipId(history)}, {QStringLiteral("lane"), history->laneIndex()}});
		QVERIFY2(chosen.ok, qPrintable(chosen.errorMessage));
		QCOMPARE(armed.track->takeLanes().activeTakeLane(groupId), history->laneIndex());

		// Arm and record again: a third lane, active even though the user chose the earlier one.
		armed.clip->setRecord(true);
		recordPass(armed.clip, 0.0625f);
		QCOMPARE(armed.track->takeLanes().laneCount(), 3);
		QCOMPARE(armed.clip->sample().sampleSize(), std::size_t{0});
		const auto all = others(armed.track, armed.clip);
		QCOMPARE(all.size(), std::size_t{3});
		int newest = -1;
		for (auto* take : all) { newest = std::max(newest, take->laneIndex()); }
		QCOMPARE(armed.track->takeLanes().activeTakeLane(groupId), newest);
		delete armed.track;
		lmms::recordoptions::setOptions({});
	}

	//! One undo takes a Keep History recording back to the clip as it was, and redo brings the lanes back.
	void oneUndoRestoresTheClipAKeepHistoryPassSplit()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::KeepHistory});
		ArmedClip armed = armedClip(true);
		recordPass(armed.clip, 0.125f);
		QCOMPARE(armed.track->takeLanes().laneCount(), 2);

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {}).ok);
		lmms::SampleTrack* restored = trackById(armed.trackId);
		QVERIFY(restored != nullptr);
		QCOMPARE(restored->takeLanes().laneCount(), 0);
		QCOMPARE(restored->getClips().size(), 1);
		auto* back = dynamic_cast<lmms::SampleClip*>(restored->getClips().front());
		QCOMPARE(back->sample().sampleSize(), EarlierFrames);
		QVERIFY(!back->isRecord());
		QVERIFY(std::abs(extremes(*back).first - 0.25f) < 1e-6f);

		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.redo"), {}).ok);
		restored = trackById(armed.trackId);
		QVERIFY(restored != nullptr);
		QCOMPARE(restored->takeLanes().laneCount(), 2);
		delete restored;
		lmms::recordoptions::setOptions({});
	}

	//! The kept lanes and the active take are in the project: save, reopen, and they are still two takes.
	void keepHistoryLanesSurviveSaveAndReopen()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::KeepHistory});
		ArmedClip armed = armedClip(true);
		const int groupId = armed.clip->id();
		recordPass(armed.clip, 0.125f);
		const int active = armed.track->takeLanes().activeTakeLane(groupId);
		QVERIFY(active >= 0);

		QCOMPARE(others(armed.track, armed.clip).size(), std::size_t{2});
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = dir.filePath(QStringLiteral("keep-history.mmp"));
		auto* song = lmms::Engine::getSong();
		QVERIFY2(song->saveProjectFile(project, false), "project save failed");
		song->loadProject(project);

		lmms::SampleTrack* reopened = trackById(armed.trackId);
		QVERIFY2(reopened != nullptr, "reopen lost the track");
		QCOMPARE(reopened->takeLanes().laneCount(), 2);
		QCOMPARE(reopened->takeLanes().activeTakeLane(groupId), active);
		std::size_t earlierFound = 0;
		int inGroup = 0;
		for (lmms::Clip* c : reopened->getClips())
		{
			auto* sc = dynamic_cast<lmms::SampleClip*>(c);
			if (sc == nullptr || sc->takeGroupId() != groupId) { continue; }
			++inGroup;
			if (sc->sample().sampleSize() == EarlierFrames) { ++earlierFound; }
		}
		QCOMPARE(inGroup, 2);
		QCOMPARE(earlierFound, std::size_t{1});  // the earlier audio is back, whole
		lmms::recordoptions::setOptions({});
	}

	//! Overdub over a clip that does not play as recorded (here a trimmed one) cannot be mixed in place: the
	//! clip is left exactly as it was and the pass sounds beside it as a clip of its own.
	void anOverdubOverATrimmedClipAddsTheLayerWithoutTouchingIt()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::Overdub});
		ArmedClip armed = armedClip(true);
		armed.clip->setSampleWindow(lmms::SampleWindow(10, 15000));
		recordPass(armed.clip, 0.125f);
		QCOMPARE(armed.clip->sample().sampleSize(), EarlierFrames);
		QVERIFY(std::abs(extremes(*armed.clip).first - 0.25f) < 1e-6f);  // untouched
		QCOMPARE(armed.track->takeLanes().laneCount(), 0);
		const auto layers = others(armed.track, armed.clip);
		QCOMPARE(layers.size(), std::size_t{1});
		QVERIFY(layers.front()->sample().sampleSize() > 0);
		QCOMPARE(layers.front()->takeGroupId(), 0);  // it always sounds, whatever take is active
		QVERIFY(!layers.front()->isMuted());
		delete armed.track;
		lmms::recordoptions::setOptions({});
	}

	//! Keep History on a clip with no audio yet: the first pass is a lane too (every kept pass is one).
	void aKeepHistoryPassOnAnEmptyClipIsALane()
	{
		lmms::recordoptions::setOptions({0, 0, lmms::recordoptions::Mode::KeepHistory});
		ArmedClip armed = armedClip(false);
		recordPass(armed.clip, 0.125f);
		QCOMPARE(armed.track->takeLanes().laneCount(), 1);
		QCOMPARE(armed.clip->sample().sampleSize(), std::size_t{0});
		const auto takes = others(armed.track, armed.clip);
		QCOMPARE(takes.size(), std::size_t{1});
		QVERIFY(takes.front()->sample().sampleSize() > 0);
		delete armed.track;
		lmms::recordoptions::setOptions({});
	}

	//! clip.set_record has its own undo step (an action keyed by the clip's id): control.undo disarms the
	//! clip it armed. (Its redo does not re-arm: the flag is not in the clip's serialised state - a known
	//! limit, recorded in the report, and not asserted here.)
	void clipSetRecordIsUndoneAndRedoneByControl()
	{
		auto* song = lmms::Engine::getSong();
		song->stop();
		auto* track = new lmms::SampleTrack(song);
		const auto trackId = track->id();
		auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(lmms::TimePos(song->ticksPerBar()));
		clip->setAutoResize(false);
		const QString id = QStringLiteral("clip-%1").arg(clip->id());
		const auto armedOf = [&]() {
			for (lmms::Track* t : song->tracks()) {
				if (t->id() != trackId) { continue; }
				for (lmms::Clip* c : t->getClips()) {
					if (auto* sc = dynamic_cast<lmms::SampleClip*>(c); sc != nullptr && clipIdOf(sc) == id) { return sc->isRecord(); }
				}
			}
			return false;
		};

		const auto set = lmms::ControlRegistry::instance()->invoke(QStringLiteral("clip.set_record"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("record"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(armedOf());

		const auto undo = lmms::ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), {});
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QVERIFY2(!armedOf(), "control.undo did not disarm the clip that clip.set_record armed");

		// Leave it disarmed for the next test.
		QVERIFY(lmms::ControlRegistry::instance()->invoke(QStringLiteral("clip.set_record"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("record"), false}}).ok);
		delete track;
	}

	//! The control id ("clip-N") of a clip.
	static QString clipIdOf(const lmms::Clip* clip)
	{
		return QStringLiteral("clip-%1").arg(clip->id());
	}
} ;


QTEST_MAIN(RecordingRealtimeTest)

#include "RecordingRealtimeTest.moc"
