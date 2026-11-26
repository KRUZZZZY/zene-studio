/*
 * SampleClipRecordTeardownTest.cpp - cluster new-p15-177: destroying an armed clip and the
 *                                     record-handle slots the audio thread claims
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

/*! A clip keeps two record handles ready (m_preparedRecord and m_spareRecord, built by
 *  SampleClip::prepareRecordTake). The audio thread takes them through
 *  SampleClip::claimRecordHandle() while it renders a period, and the render period owns the
 *  engine's change lock for its whole duration. Every path that touches those slots - prepare,
 *  release, claim - does so under that lock, EXCEPT the destructor, which used to delete the two
 *  pointers directly: deleting an armed clip while a period was claiming a handle freed a handle
 *  the period could still take, a use-after-free (and a double delete, because the destructor free
 *  and the claim's hand-over own the same pointer).
 *
 *  Two cases:
 *    * deleting an armed clip still hands BOTH prepared takes to the main-thread reaper - the
 *      teardown neither leaks a handle nor drops a take on the way out;
 *    * the destructor waits for the change lock before it frees anything. The second case decides
 *      on a STATE - AudioEngine::modelChangeWaiters() reports a control thread parked on the
 *      change lock, and SampleRecordHandle::takesReaped() reports the handles whose take was
 *      queued - never on elapsed time; the deadline only turns a missing lock into a failure
 *      instead of a hang.
 */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SampleRecordHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace
{

//! An armed clip with both record-handle slots filled, taken out of its track's clip list so the
//! test alone deletes it (the same removal Track::loadTrack performs when an armed clip is rebuilt),
//! plus the track it belonged to.
lmms::SampleClip* armedClipOutsideItsTrack(lmms::SampleTrack** trackOut)
{
	auto* track = new lmms::SampleTrack(lmms::Engine::getSong());
	auto* clip = dynamic_cast<lmms::SampleClip*>(track->createClip(lmms::TimePos(0)));
	if (clip == nullptr) { delete track; *trackOut = nullptr; return nullptr; }
	// The arm transition: armRecordTake() -> prepareRecordTake() fills m_preparedRecord AND
	// m_spareRecord, so both slots hold a handle.
	clip->setRecord(true);
	track->removeClip(clip);
	*trackOut = track;
	return clip;
}

} // namespace


class SampleClipRecordTeardownTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The cases below need a real AudioEngine (change lock, play-handle lists). Qt needs a
		// platform plugin for that even though the test is guiless, which tests/CMakeLists.txt
		// arranges with QT_QPA_PLATFORM=offscreen, as for RecordingRealtimeTest.
		lmms::Engine::init(true);
		QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	}

	//! The teardown completes the takes the clip was carrying: an unclaimed handle queues its
	//! take when it is deleted, and the main-thread reaper counts it. Nothing is leaked, and no
	//! take is silently dropped.
	void deletingAnArmedClipQueuesBothPreparedTakes()
	{
		lmms::SampleTrack* track = nullptr;
		lmms::SampleClip* clip = armedClipOutsideItsTrack(&track);
		QVERIFY(clip != nullptr);
		QVERIFY2(clip->isRecord(), "the clip did not arm, so it carries no prepared takes");

		// A clean queue: what the reaper counts below is this clip's own two handles.
		lmms::SampleRecordHandle::pumpTakes();
		const qulonglong reapedBefore = lmms::SampleRecordHandle::takesReaped();

		delete clip;
		lmms::SampleRecordHandle::reapRetiredTakes();

		const qulonglong queued = lmms::SampleRecordHandle::takesReaped() - reapedBefore;
		QCOMPARE(queued, qulonglong{2});

		lmms::SampleRecordHandle::pumpTakes();  // forgets the destroyed clip
		delete track;
	}

	//! Cluster new-p15-177: the destructor frees its handles only after the render period that
	//! may claim them has finished. A render period owns the engine's change lock for its whole
	//! duration (AudioEngine::renderNextPeriod), and SampleTrack::play() claims a take under it,
	//! so a destructor that takes the lock cannot free a handle a period is taking.
	void clipDestructionWaitsForTheChangeLockBeforeFreeingHandles()
	{
		lmms::AudioEngine* engine = lmms::Engine::audioEngine();
		QVERIFY(engine != nullptr);
		lmms::SampleTrack* track = nullptr;
		lmms::SampleClip* clip = armedClipOutsideItsTrack(&track);
		QVERIFY(clip != nullptr);
		QVERIFY2(clip->isRecord(), "the clip did not arm, so it carries no prepared takes");

		// A clean queue: a take the reaper counts below was queued by this destruction.
		lmms::SampleRecordHandle::pumpTakes();
		const qulonglong reapedBefore = lmms::SampleRecordHandle::takesReaped();

		std::atomic<bool> deleted{false};
		std::thread deleter;
		int freedWhileThePeriodHeldTheModel = -1;
		bool reachedTheLock = false;
		{
			// What renderNextPeriod() holds while SampleTrack::play() claims a take.
			const auto renderPeriod = engine->requestChangesGuard();
			deleter = std::thread([clip, &deleted] {
				delete clip;
				deleted.store(true, std::memory_order_release);
			});
			// The decision point: the destructor has parked on the change lock (modelChangeWaiters()
			// reports it), so everything it freed before asking for the lock is already done.
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (engine->modelChangeWaiters() == 0 && !deleted.load(std::memory_order_acquire)
				&& std::chrono::steady_clock::now() < deadline)
			{
			}
			reachedTheLock = engine->modelChangeWaiters() > 0;
			// The frees that happened while the period still owned the model.
			lmms::SampleRecordHandle::reapRetiredTakes();
			freedWhileThePeriodHeldTheModel = static_cast<int>(
				lmms::SampleRecordHandle::takesReaped() - reapedBefore);
		}
		deleter.join();
		const bool finished = deleted.load(std::memory_order_acquire);

		QVERIFY2(finished, "deleting the clip never finished, although the period released the model");
		QVERIFY2(reachedTheLock, "the clip's destructor never asked for the audio engine's change lock");
		QVERIFY2(freedWhileThePeriodHeldTheModel == 0, qPrintable(QStringLiteral(
			"delete clip freed %1 record handle(s) before it waited for the change lock held by the "
			"render period").arg(freedWhileThePeriodHeldTheModel)));

		// And the teardown still hands both takes over, once the period is over.
		lmms::SampleRecordHandle::reapRetiredTakes();
		QCOMPARE(lmms::SampleRecordHandle::takesReaped() - reapedBefore, qulonglong{2});
		lmms::SampleRecordHandle::pumpTakes();
		delete track;
	}
};


QTEST_MAIN(SampleClipRecordTeardownTest)

#include "SampleClipRecordTeardownTest.moc"
