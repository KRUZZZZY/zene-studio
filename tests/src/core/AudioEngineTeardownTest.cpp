/*
 * AudioEngineTeardownTest.cpp - the engine's worker pool must not outlive its
 * QThread objects (test hygiene: a test must not abort because of teardown)
 *
 * Copyright (c) 2026 Zene Studio developers
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

//! Regression test for the teardown abort that made the suite unreadable under
//! load. Observed as
//!
//!   QFATAL : <AnyEngineTest>::cleanupTestCase() QThread: Destroyed while thread
//!            is still running
//!   Received signal 6 (SIGABRT)
//!
//! i.e. exit code 134, on whichever test happened to be running when it landed -
//! 13 of 64 PdcMixerTest runs, and three different tests across a suite sweep,
//! with no change to the code under test. docs/TEST-HYGIENE.md carries the
//! reproduction, the gdb backtrace and the load measurements.
//!
//! The defect, in two halves:
//!
//!  1. `AudioEngineWorkerThread::run()` reads `m_quit` at the top of its loop,
//!     outside the mutex, and then blocks in `QWaitCondition::wait()`. Setting the
//!     flag (`quit()`) and waking the condition (`startAndWaitForJobs()`) are two
//!     separate steps, so a worker preempted between the read and the wait misses
//!     the wake that was meant to release it and sleeps for good.
//!  2. `~AudioEngine` joined each worker with a fixed `wait(500)`. When that budget
//!     expired the worker was still running, and because the workers are QObject
//!     children of the engine (~QObject deletes them) the QThread object was
//!     destroyed while its thread ran - which Qt answers with `qFatal`, i.e. an
//!     unconditional SIGABRT of the whole test process.
//!
//! Case `quitWithoutAWakeupStopsAParkedWorker` is the red/green pair: it fails
//! (deterministically) on the tree without half 1, because nobody ever wakes the
//! parked worker. Case `quitThenWakeAndJoinStopsEveryWorker` pins the product's own
//! destroy sequence.

#include <QtTest>

#include <QThread>

#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "Engine.h"

using namespace lmms;

namespace
{

//! Long enough that a worker started by this process has certainly reached the
//! wait condition even on a loaded box - its run() does nothing else before it
//! blocks - and short enough to keep the suite fast.
constexpr int kParkSettleMs = 750;

//! Budget for a worker to leave its loop. The product's quit-recheck interval is
//! 100 ms, so this is 20x that: the case is about the contract ("a quit ends the
//! loop"), not about scheduler luck.
constexpr int kExitBudgetMs = 2000;

//! Parked workers per case. More than one so a case does not hinge on a single
//! thread's scheduling.
constexpr int kWorkers = 8;

} // namespace

class AudioEngineTeardownTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(Engine::audioEngine()->audioDev() != nullptr);

		// Stop the dummy device before the cases: each case observes workers in
		// their parked state, and a device that renders periods wakes them ~350
		// times a second (which is also how the same defect hides in a test that
		// leaves the device running - see docs/TEST-HYGIENE.md).
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		// The defect's original site. It must not abort here any more.
		Engine::destroy();
	}

	//! A worker parked in the wait condition must end its loop when it is asked to
	//! quit, without anyone calling startAndWaitForJobs() to wake it. On the tree
	//! without the bounded re-check it never does: it blocks forever and the QThread
	//! object is then destroyed while running.
	void quitWithoutAWakeupStopsAParkedWorker()
	{
		std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back(new AudioEngineWorkerThread(Engine::audioEngine()));
			workers.back()->start();
		}

		// Nothing wakes a worker of this pool: the engine's own device is stopped and
		// no render is in flight, so a started worker is parked by now.
		QThread::msleep(kParkSettleMs);

		for (auto& w : workers)
		{
			w->quit();
		}

		int stranded = 0;
		for (auto& w : workers)
		{
			if (!w->wait(kExitBudgetMs))
			{
				// Deliberately leak the running QThread rather than destroying it:
				// destroying a running QThread is the very thing under test (Qt
				// aborts the process for it) and an abort would hide the assertion
				// that has already failed.
				w.release();
				++stranded;
			}
		}
		QCOMPARE(stranded, 0);
	}

	//! The product's own destroy sequence, in its own order: quit every worker,
	//! wake the condition once (as ~AudioEngine does through
	//! startAndWaitForJobs()), then join. Every worker must be joined.
	void quitThenWakeAndJoinStopsEveryWorker()
	{
		std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back(new AudioEngineWorkerThread(Engine::audioEngine()));
			workers.back()->start();
		}
		QThread::msleep(kParkSettleMs);

		for (auto& w : workers)
		{
			w->quit();
		}
		AudioEngineWorkerThread::startAndWaitForJobs();

		int stranded = 0;
		for (auto& w : workers)
		{
			if (!w->wait(kExitBudgetMs))
			{
				w.release();
				++stranded;
			}
		}
		QCOMPARE(stranded, 0);
	}
};

QTEST_GUILESS_MAIN(AudioEngineTeardownTest)
#include "AudioEngineTeardownTest.moc"
