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

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "AudioEngine.h"
#include "AudioDevice.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
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

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
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
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
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
};

QTEST_GUILESS_MAIN(FrozenTakeHandoffTest)
#include "FrozenTakeHandoffTest.moc"
