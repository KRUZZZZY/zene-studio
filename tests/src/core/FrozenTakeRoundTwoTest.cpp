/*
 * FrozenTakeRoundTwoTest.cpp - pooled voice lifecycle, capacity and thread-affinity regressions
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioDevice.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MetronomeOptions.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Timeline.h"
#include "Track.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

std::span<const SampleFrame> renderPeriod()
{
	return Engine::audioEngine()->renderNextPeriod();
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

} // namespace

class FrozenTakeRoundTwoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		Engine::destroy();
	}

	//! Cluster 5127 round 2: handles made on the control thread retire after rendering on another thread.
	void pooledMetronomeVoicesRetireOnTheRenderThread()
	{
		AudioEngine* const audio = Engine::audioEngine();
		Engine::getSong()->clearProject();
		const std::vector<SampleFrame> frames(64, SampleFrame{0.01f, -0.01f});
		const auto source = std::make_shared<const SampleBuffer>(frames, audio->outputSampleRate());
		QVERIFY(source != nullptr);
		renderPeriod(); // prime the engine's output double buffer before checking a click's audible period

		for (int clickNumber = 0; clickNumber < 40; ++clickNumber)
		{
			auto* const click = audio->acquirePooledSampleVoice(source, 0,
				static_cast<int>(source->size()), 1.0f, 0, nullptr, true);
			QVERIFY2(click != nullptr, "the metronome pool ran dry before 40 completed clicks");
			QVERIFY(audio->addPlayHandle(click));

			std::atomic<bool> sounded{false};
			std::thread renderThread([&] {
				audio->renderNextPeriod();
				const auto output = audio->renderNextPeriod();
				for (const SampleFrame& frame : output)
				{
					if (frame[0] != 0.0f || frame[1] != 0.0f)
					{
						sounded.store(true, std::memory_order_relaxed);
						break;
					}
				}
			});
			renderThread.join();
			QVERIFY2(sounded.load(std::memory_order_relaxed), "a pooled metronome voice rendered silence");
			QTest::qWait(110); // let the control-thread recycler return the finished slot
		}
	}

	//! Cluster 5414: the derived per-track allowance is accepted and the next pass is counted and refused.
	void frozenPassLimitIsCounted()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, 440.0, &track, nullptr, 192);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(24));
		timeline.setLoopEnabled(true);
		for (int pass = 0; pass < 8; ++pass)
		{
			if (pass > 0) { QMetaObject::invokeMethod(Engine::getSong(), "playbackPositionJumped", Qt::DirectConnection); }
			QVERIFY(track->play(TimePos(0), audio->framesPerPeriod(), 0, -1));
			renderPeriod();
		}
		const auto before = run(QStringLiteral("control.ping"));
		QVERIFY(before.ok);
		const int overflowBefore = before.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("sample_voice_overflow")).toInt();
		QMetaObject::invokeMethod(Engine::getSong(), "playbackPositionJumped", Qt::DirectConnection);
		QVERIFY(!track->play(TimePos(0), audio->framesPerPeriod(), 0, -1));
		const auto after = run(QStringLiteral("control.ping"));
		QVERIFY(after.ok);
		const int overflowAfter = after.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("sample_voice_overflow")).toInt();
		QVERIFY(overflowAfter > overflowBefore);
		QCOMPARE(frozenPassLengths(audio, track).size(), std::size_t{8});
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::SamplePlayHandle);
		QTest::qWait(130);
	}

	//! Cluster 5414 round 2: a 2-second take looping every 4 ticks can hold more than eight live passes.
	void longFrozenTakeOverShortLoopGetsTheRequiredCapacity()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, 440.0, &track, nullptr, 192);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(4));
		timeline.setLoopEnabled(true);

		for (int pass = 0; pass < 48; ++pass)
		{
			if (pass > 0) { QMetaObject::invokeMethod(Engine::getSong(), "playbackPositionJumped", Qt::DirectConnection); }
			QVERIFY2(track->play(TimePos(0), audio->framesPerPeriod(), 0, -1),
				qPrintable(QStringLiteral("pass %1 was refused before the take's 48-pass overlap bound").arg(pass)));
			renderPeriod();
		}
		QCOMPARE(frozenPassLengths(audio, track).size(), std::size_t{48});

		const auto before = run(QStringLiteral("control.ping"));
		QVERIFY(before.ok);
		const int overflowBefore = before.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("sample_voice_overflow")).toInt();
		QMetaObject::invokeMethod(Engine::getSong(), "playbackPositionJumped", Qt::DirectConnection);
		QVERIFY(!track->play(TimePos(0), audio->framesPerPeriod(), 0, -1));
		const auto after = run(QStringLiteral("control.ping"));
		QVERIFY(after.ok);
		const int overflowAfter = after.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("sample_voice_overflow")).toInt();
		QVERIFY(overflowAfter > overflowBefore);
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::SamplePlayHandle);
		QTest::qWait(130);
	}

	//! Cluster 5414 round 2: deleting a SampleTrack during a frozen voice removes its handle synchronously.
	void sampleTrackDeletionCancelsPooledVoice()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, 330.0, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(track->play(TimePos(10), audio->framesPerPeriod(), 0, -1));
		renderPeriod();
		QCOMPARE(frozenPassLengths(audio, track).size(), std::size_t{1});

		const QString id = QStringLiteral("trk-%1").arg(track->id());
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QTest::qWait(130);
		const auto hasFrozenVoice = [](PlayHandle* handle) {
			return handle->type() == PlayHandle::Type::SamplePlayHandle
				&& static_cast<SamplePlayHandle*>(handle)->isFrozenTakePoolVoice();
		};
		QVERIFY(std::none_of(audio->playHandles().begin(), audio->playHandles().end(), hasFrozenVoice));
		QCOMPARE(audio->sampleVoiceQueueInvariantFailures(), std::uint64_t{0});
	}

	//! Cluster 5414 round 2: deleting an InstrumentTrack retires its frozen voice before Track base destruction.
	void instrumentTrackDeletionCancelsPooledVoice()
	{
		AudioEngine* const audio = Engine::audioEngine();
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("instrument-take.wav")), 220.0);
		QVERIFY(!wav.isEmpty());
		Song* const song = Engine::getSong();
		song->clearProject();
		const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")} });
		QVERIFY(added.ok);
		auto* const track = dynamic_cast<InstrumentTrack*>(song->tracks().back());
		QVERIFY(track != nullptr);
		QString reason;
		QVERIFY2(track->freezeTo(wav, 0, 192, {}, &reason), qPrintable(reason));
		QVERIFY(track->play(TimePos(10), audio->framesPerPeriod(), 0, -1));
		renderPeriod();
		QCOMPARE(frozenPassLengths(audio, track).size(), std::size_t{1});

		const QString id = QStringLiteral("trk-%1").arg(track->id());
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QTest::qWait(130);
		const auto hasFrozenVoice = [](PlayHandle* handle) {
			return handle->type() == PlayHandle::Type::SamplePlayHandle
				&& static_cast<SamplePlayHandle*>(handle)->isFrozenTakePoolVoice();
		};
		QVERIFY(std::none_of(audio->playHandles().begin(), audio->playHandles().end(), hasFrozenVoice));
		QCOMPARE(audio->sampleVoiceQueueInvariantFailures(), std::uint64_t{0});
	}

	//! Cluster 5414 round 2: more frozen voices than the 480-slot partition retire from a worker thread.
	void pooledFrozenVoicesRetireOnTheRenderThread()
	{
		AudioEngine* const audio = Engine::audioEngine();
		Engine::getSong()->clearProject();
		const std::vector<SampleFrame> frames(64, SampleFrame{0.01f, -0.01f});
		const auto source = std::make_shared<const SampleBuffer>(frames, audio->outputSampleRate());
		QVERIFY(source != nullptr);
		renderPeriod();
		const std::uint64_t overflowBefore = audio->sampleVoiceOverflows();

		std::mutex renderMutex;
		std::condition_variable renderRequest;
		std::condition_variable renderComplete;
		bool shouldRender = false;
		bool shouldStop = false;
		bool renderDone = false;
		bool sounded = false;
		std::thread renderThread([&] {
			std::unique_lock lock(renderMutex);
			for (;;)
			{
				renderRequest.wait(lock, [&] { return shouldRender || shouldStop; });
				if (shouldStop) { return; }
				shouldRender = false;
				lock.unlock();
				audio->renderNextPeriod();
				const auto output = audio->renderNextPeriod();
				const bool hasSignal = std::any_of(output.begin(), output.end(), [](const SampleFrame& frame) {
					return frame[0] != 0.0f || frame[1] != 0.0f;
				});
				lock.lock();
				sounded = hasSignal;
				renderDone = true;
				renderComplete.notify_one();
			}
		});

		int rendered = 0;
		bool everyVoiceSounded = true;
		for (; rendered < 500; ++rendered)
		{
			auto* const voice = audio->acquirePooledSampleVoice(source, 0,
				static_cast<int>(source->size()), 1.0f, 0, nullptr, false);
			if (voice == nullptr || !audio->addPlayHandle(voice)) { break; }

			std::unique_lock lock(renderMutex);
			shouldRender = true;
			renderRequest.notify_one();
			renderComplete.wait(lock, [&] { return renderDone; });
			everyVoiceSounded &= sounded;
			renderDone = false;
			lock.unlock();
			if ((rendered + 1) % 240 == 0) { QTest::qWait(110); }
		}
		{
			std::lock_guard lock(renderMutex);
			shouldStop = true;
		}
		renderRequest.notify_one();
		renderThread.join();
		QVERIFY2(rendered == 500, "the frozen voice partition ran dry before 500 worker-rendered passes");
		QVERIFY2(everyVoiceSounded, "at least one worker-rendered frozen voice was silent");
		QCOMPARE(audio->sampleVoiceOverflows(), overflowBefore);
		QTest::qWait(130);
		QCOMPARE(audio->sampleVoiceQueueInvariantFailures(), std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(FrozenTakeRoundTwoTest)
#include "FrozenTakeRoundTwoTest.moc"
