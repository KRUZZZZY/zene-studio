/*
 * FrozenTakeHandoffTest.cpp - the frozen take's state is changed under the render lock
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

// Cluster 5415 (M0 blockers): Track::playFrozenTake() runs on the render thread and reads the take's
// buffer and window; freezeTo(), unfreeze() and clearFrozenTake() run on the control thread and write
// them. Those writes must happen under the engine's change lock, which the render thread holds for the
// whole period (AudioEngine::renderNextPeriod), so a control-thread change waits for the period to end.

#include <QtTest>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioDevice.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MetronomeOptions.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "Track.h"
#include "AllocationProbe.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

std::mutex s_probeMutex;
std::condition_variable s_probeCv;
bool s_probeReached = false;
bool s_allowLockAttempt = false;

void pauseBeforeFrozenTakeLock()
{
	std::unique_lock lock(s_probeMutex);
	s_probeReached = true;
	s_probeCv.notify_all();
	s_probeCv.wait(lock, [] { return s_allowLockAttempt; });
}

std::span<const SampleFrame> renderPeriod()
{
	return Engine::audioEngine()->renderNextPeriod();
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString prepareFrozenTake(QTemporaryDir& scratch, double frequency, SampleTrack** track,
	std::shared_ptr<const SampleBuffer>* source = nullptr, tick_t takeEndTicks = 1920)
{
	const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("take.wav")), frequency);
	if (wav.isEmpty()) { return QStringLiteral("could not write a frozen-take fixture"); }
	Song* const song = Engine::getSong();
	song->clearProject();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	timeline.setLoopEnabled(false);
	timeline.setLoopPoints(TimePos(0), TimePos(DefaultTicksPerBar));
	if (!run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok)
	{
		return QStringLiteral("could not add the sample track");
	}
	*track = dynamic_cast<SampleTrack*>(song->tracks().back());
	if (*track == nullptr) { return QStringLiteral("track.add returned a non-sample track"); }
	QString reason;
	if (!(*track)->freezeTo(wav, 0, takeEndTicks, {}, &reason)) { return reason; }
	if (source != nullptr)
	{
		*source = SampleBuffer::fromFile(wav);
		if (*source == nullptr) { return QStringLiteral("could not load the frozen-take fixture"); }
	}
	for (int i = 0; i < 8; ++i) { renderPeriod(); }
	return {};
}

std::vector<f_cnt_t> frozenPassLengths(AudioEngine* audio, Track* track)
{
	std::vector<f_cnt_t> lengths;
	for (PlayHandle* handle : audio->playHandles())
	{
		if (handle->type() == PlayHandle::Type::SamplePlayHandle && handle->isFromTrack(track))
		{
			lengths.push_back(static_cast<SamplePlayHandle*>(handle)->totalFrames());
		}
	}
	return lengths;
}

double maxMixDifference(const std::vector<SampleFrame>& left, const std::vector<SampleFrame>& right)
{
	if (left.size() != right.size()) { return 1.0; }
	double maximum = 0.0;
	for (std::size_t frame = 0; frame < left.size(); ++frame)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			maximum = std::max(maximum, std::abs(static_cast<double>(left[frame][channel] - right[frame][channel])));
		}
	}
	return maximum;
}

/*! Holds the engine's change lock, waits until @a work reaches the lock boundary (after any file load),
 *  then gives it a chance to finish while the lock is held. True only when the synchronized probe was
 *  reached and the change stayed blocked until the simulated render period ended. */
template <typename Work>
bool workWaitsWhileRenderLockHeld(Work work)
{
	std::mutex mutex;
	std::condition_variable cv;
	bool held = false;
	bool release = false;

	std::thread holder([&] {
		const auto renderPeriod = Engine::audioEngine()->requestChangesGuard();
		{
			std::lock_guard lock(mutex);
			held = true;
		}
		cv.notify_all();
		std::unique_lock lock(mutex);
		cv.wait(lock, [&] { return release; });
	});
	{
		std::unique_lock lock(mutex);
		cv.wait(lock, [&] { return held; });
	}

	{
		std::lock_guard lock(s_probeMutex);
		s_probeReached = false;
		s_allowLockAttempt = false;
	}
	Track::setFrozenTakeLockProbeForTest(&pauseBeforeFrozenTakeLock);
	std::mutex finishedMutex;
	std::condition_variable finishedCv;
	bool finished = false;
	std::thread worker([&] {
		work();
		{
			std::lock_guard lock(finishedMutex);
			finished = true;
		}
		finishedCv.notify_all();
	});
	bool probeReached = false;
	{
		std::unique_lock lock(s_probeMutex);
		probeReached = s_probeCv.wait_for(lock, std::chrono::seconds(5), [] { return s_probeReached; });
		s_allowLockAttempt = true;
	}
	s_probeCv.notify_all();
	bool finishedWhileHeld = false;
	if (probeReached)
	{
		std::unique_lock lock(finishedMutex);
		finishedWhileHeld = finishedCv.wait_for(lock, std::chrono::milliseconds(300), [&] { return finished; });
	}

	{
		std::lock_guard lock(mutex);
		release = true;
	}
	cv.notify_all();
	holder.join();
	worker.join();
	Track::setFrozenTakeLockProbeForTest(nullptr);
	return probeReached && !finishedWhileHeld;
}

} // namespace

class FrozenTakeHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		Engine::destroy();
	}

	//! 5415: a freeze that changes the take the render thread reads waits for the render lock.
	void freezeWaitsForTheRenderLock()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("take.wav")), 440.0);
		QVERIFY(!wav.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		Track* track = song->tracks().back();
		QVERIFY(track != nullptr);
		QVERIFY(!track->isFrozen());

		bool freezeOk = false;
		const bool waited = workWaitsWhileRenderLockHeld([&] {
			QString reason;
			freezeOk = track->freezeTo(wav, 0, 1920, {}, &reason);
		});
		QVERIFY2(waited, "freezeTo finished while the render thread held the change lock: it did not wait for the period");
		QVERIFY(freezeOk);
		QVERIFY(track->isFrozen());
	}

	//! 5415: unfreeze() and clearFrozenTake() write the same state, so they wait for the render lock too.
	void unfreezeWaitsForTheRenderLock()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("take.wav")), 330.0);
		QVERIFY(!wav.isEmpty());

		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		Track* track = song->tracks().back();
		QVERIFY(track != nullptr);
		QString reason;
		QVERIFY2(track->freezeTo(wav, 0, 1920, {}, &reason), qPrintable(reason));
		QVERIFY(track->isFrozen());

		const bool waited = workWaitsWhileRenderLockHeld([&] { track->unfreeze(); });
		QVERIFY2(waited, "unfreeze finished while the render thread held the change lock: it did not wait for the period");
		QVERIFY(!track->isFrozen());
	}

	//! Cluster 5127: clicks claim prepared handles, match fresh audio and retire without render-path allocation.
	void pooledClickSchedulingAndRetirementAreAllocationFree()
	{
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		for (int i = 0; i < 8; ++i) { renderPeriod(); }
		metronomeoptions::setOptions({0.75f, true, false, 0});
		const auto source = SampleBuffer::fromFile(QStringLiteral("misc/metronome01.ogg"));
		QVERIFY(source != nullptr);
		auto* const pooled = static_cast<SamplePlayHandle*>(metronomeoptions::click(false, 7));
		QVERIFY(pooled != nullptr);
		Sample referenceSample(source);
		referenceSample.setAmplification(0.75f);
		SamplePlayHandle fresh(&referenceSample, false);
		fresh.setOffset(7);
		pooled->doProcessing();
		fresh.doProcessing();
		bool exact = pooled->buffer().size() == fresh.buffer().size();
		for (std::size_t frame = 0; exact && frame < pooled->buffer().size(); ++frame)
		{
			for (int channel = 0; channel < 2; ++channel)
			{
				exact &= std::bit_cast<std::uint32_t>(pooled->buffer()[frame][channel])
					== std::bit_cast<std::uint32_t>(fresh.buffer()[frame][channel]);
			}
		}
		QVERIFY(exact);
		audio->recyclePooledSampleVoice(pooled, false);
		QTest::qWait(130);

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		auto* const click = static_cast<SamplePlayHandle*>(metronomeoptions::click(false, 0));
		if (click != nullptr) { audio->addPlayHandle(click); }
		test::tlCountAllocations = false;
		QVERIFY(click != nullptr);
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		const int clickPeriods = static_cast<int>(click->totalFrames() / audio->framesPerPeriod()) + 3;
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < clickPeriods; ++i) { renderPeriod(); }
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		QVERIFY(click->framesDone() >= click->totalFrames());
		QTest::qWait(130);
	}


	//! Cluster 5414: a re-armed voice matches a fresh handle for distinct nonzero source windows.
	void pooledFrozenVoiceRearmMatchesFreshAudio()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		std::shared_ptr<const SampleBuffer> source;
		const QString error = prepareFrozenTake(scratch, 440.0, &track, &source);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		const auto rendersLikeFresh = [&](int sourceIn) {
			auto* const pooled = audio->acquirePooledSampleVoice(source, sourceIn, sourceIn + 4096,
				0.75f, 7, nullptr, false);
			if (pooled == nullptr) { return false; }
			Sample reference(source);
			reference.setStartFrame(sourceIn);
			reference.setEndFrame(sourceIn + 4096);
			reference.setAmplification(0.75f);
			SamplePlayHandle fresh(&reference, false);
			fresh.setOffset(7);
			pooled->doProcessing();
			fresh.doProcessing();
			bool exact = pooled->buffer().size() == fresh.buffer().size();
			for (std::size_t frame = 0; exact && frame < pooled->buffer().size(); ++frame)
			{
				for (int channel = 0; channel < 2; ++channel)
				{
					exact &= std::bit_cast<std::uint32_t>(pooled->buffer()[frame][channel])
						== std::bit_cast<std::uint32_t>(fresh.buffer()[frame][channel]);
				}
			}
			audio->recyclePooledSampleVoice(pooled, false);
			return exact;
		};
		QVERIFY(rendersLikeFresh(1024));
		QTest::qWait(130);
		QVERIFY(rendersLikeFresh(2048));
	}

	//! Cluster 5414: each overlapping pass keeps its own sourceIn and pass length.
	void frozenPassesPreserveIndependentSourceWindows()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, 440.0, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(track->isFrozen());
		QVERIFY(!track->play(TimePos(-1), audio->framesPerPeriod(), 0, -1));
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		const bool firstStarted = track->play(TimePos(100), audio->framesPerPeriod(), 0, -1);
		test::tlCountAllocations = false;
		QVERIFY(firstStarted);
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		renderPeriod();
		QCOMPARE(frozenPassLengths(audio, track).size(), std::size_t{1});

		QVERIFY(!track->play(TimePos(1920), audio->framesPerPeriod(), 0, -1));
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		const bool secondStarted = track->play(TimePos(150), audio->framesPerPeriod(), 0, -1);
		test::tlCountAllocations = false;
		QVERIFY(secondStarted);
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		renderPeriod();
		const auto lengths = frozenPassLengths(audio, track);
		QCOMPARE(lengths.size(), std::size_t{2});
		QVERIFY(lengths[0] != lengths[1]);
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::SamplePlayHandle);
		QTest::qWait(130);
	}


	//! Cluster 5127: the click and frozen reserves stay separate and both capacities report overflow.
	void voicePoolCapacityIsPartitionedAndCounted()
	{
		AudioEngine* const audio = Engine::audioEngine();
		metronomeoptions::setOptions({1.0f, true, false, 0});
		const auto source = SampleBuffer::fromFile(QStringLiteral("misc/metronome01.ogg"));
		QVERIFY(source != nullptr);
		std::array<SamplePlayHandle*, 480> frozen{};
		for (SamplePlayHandle*& voice : frozen)
		{
			voice = audio->acquirePooledSampleVoice(source, 0, static_cast<int>(source->size()), 1.0f, 0, nullptr, false);
			QVERIFY(voice != nullptr);
		}
		const auto frozenOverflow = audio->sampleVoiceOverflows();
		QVERIFY(audio->acquirePooledSampleVoice(source, 0, static_cast<int>(source->size()), 1.0f, 0,
			nullptr, false) == nullptr);
		QVERIFY(audio->sampleVoiceOverflows() > frozenOverflow);

		// Keep all frozen slots claimed while proving the reserved click partition still has all 32 voices.
		std::array<PlayHandle*, 32> clicks{};
		const auto clickOverflow = audio->sampleVoiceOverflows();
		for (PlayHandle*& voice : clicks)
		{
			voice = metronomeoptions::click(false, 0);
			QVERIFY(voice != nullptr);
			QVERIFY(audio->addPlayHandle(voice));
		}
		QVERIFY(metronomeoptions::click(false, 0) == nullptr);
		QVERIFY(audio->sampleVoiceOverflows() > clickOverflow);
		for (PlayHandle* voice : clicks) { audio->removePlayHandle(voice); }
		for (SamplePlayHandle* voice : frozen) { audio->recyclePooledSampleVoice(voice, false); }
		QTest::qWait(130);
		QCOMPARE(audio->sampleVoiceQueueInvariantFailures(), std::uint64_t{0});
	}

	//! Cluster 5414: summing the shared voice bus matches the old per-handle bus topology.
	void pooledOverlapMixMatchesLegacyBuses()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		std::shared_ptr<const SampleBuffer> source;
		const QString error = prepareFrozenTake(scratch, 440.0, &track, &source);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		const std::array<int, 2> passTicks{100, 150};
		std::array<SamplePlayHandle*, 2> pooled{};
		for (std::size_t i = 0; i < pooled.size(); ++i)
		{
			const int sourceIn = static_cast<int>(std::llround(passTicks[i] * Engine::framesPerTick(source->sampleRate())));
			pooled[i] = audio->acquirePooledSampleVoice(source, sourceIn, static_cast<int>(source->size()),
				1.0f, 0, nullptr, false);
			QVERIFY(pooled[i] != nullptr);
			pooled[i]->doProcessing();
			pooled[i]->audioBusHandle()->addPlayHandle(pooled[i]);
		}
		pooled[0]->audioBusHandle()->doProcessing();
		const auto mixedPoolBus = pooled[0]->audioBusHandle()->buffer();
		const std::vector<SampleFrame> pooledMix(mixedPoolBus.begin(), mixedPoolBus.end());
		for (SamplePlayHandle* voice : pooled) { voice->audioBusHandle()->removePlayHandle(voice); }
		for (SamplePlayHandle* voice : pooled) { audio->recyclePooledSampleVoice(voice, false); }
		QTest::qWait(130);

		std::array<SamplePlayHandle*, 2> legacy{};
		for (std::size_t i = 0; i < legacy.size(); ++i)
		{
			const int sourceIn = static_cast<int>(std::llround(passTicks[i] * Engine::framesPerTick(source->sampleRate())));
			Sample* const sample = new Sample(source);
			sample->setStartFrame(sourceIn);
			sample->setEndFrame(static_cast<int>(source->size()));
			legacy[i] = new SamplePlayHandle(sample);
			legacy[i]->setOffset(0);
			legacy[i]->doProcessing();
			legacy[i]->audioBusHandle()->addPlayHandle(legacy[i]);
		}
		std::vector<SampleFrame> legacyMix(audio->framesPerPeriod(), SampleFrame{});
		for (SamplePlayHandle* voice : legacy)
		{
			voice->audioBusHandle()->doProcessing();
			const auto bus = voice->audioBusHandle()->buffer();
			for (std::size_t frame = 0; frame < legacyMix.size(); ++frame)
			{
				legacyMix[frame][0] += bus[frame][0];
				legacyMix[frame][1] += bus[frame][1];
			}
		}
		const double difference = maxMixDifference(pooledMix, legacyMix);
		QVERIFY2(difference <= 1.0e-6, qPrintable(QStringLiteral("overlap mix error %1").arg(difference, 0, 'g', 17)));
		for (SamplePlayHandle* voice : legacy) { voice->audioBusHandle()->removePlayHandle(voice); delete voice; }
		QTest::qWait(130);
	}

};

QTEST_GUILESS_MAIN(FrozenTakeHandoffTest)
#include "FrozenTakeHandoffTest.moc"
