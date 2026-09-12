/*
 * RenderJobQueueTest.cpp - the offline render's job-queue contract
 *
 * Copyright (c) 2026 LMMS developers
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

// Regression test for the offline render's determinism switch
// (docs/RENDER-DETERMINISM.md). The engine spreads each period's play-handles, bus effects
// and mixer channels over a worker pool; which thread runs which job is a scheduling
// decision, and on a project whose effect chains amplify a one-ULP difference the exported
// file stops being reproducible. ProjectRenderer therefore sets
// AudioEngineWorkerThread::setDeterministicProcessing(true) for the duration of an export,
// which must mean: every job is processed exactly once, on the calling thread.
//
// The cases are deterministic by construction, except `inlineModeNeverLetsThePoolTakeAJob`
// which starts a real worker thread and gives it a real opportunity to steal work - so
// deleting the inline branch in AudioEngineWorkerThread::startAndWaitForJobs() turns that
// case red on purpose instead of leaving a test that asserts nothing.

#include "AudioEngineWorkerThread.h"
#include "ThreadableJob.h"

#include <QtTest>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace lmms;

namespace
{

//! A job that records where it ran, and can queue a follow-up from inside doProcessing
//! (the pattern MixerChannel uses: a channel whose dependencies are met queues its
//! receivers while the queue is draining).
//!
//! Held by pointer in the tests: ThreadableJob holds an atomic state, so it is neither
//! copyable nor movable and std::vector<CountingJob> does not compile.
class CountingJob : public ThreadableJob
{
public:
	explicit CountingJob(int& total, std::chrono::milliseconds settle = std::chrono::milliseconds{0})
		: m_total(total), m_settle(settle)
	{
	}

	bool requiresProcessing() const override { return !m_done; }

	//! Pretend an earlier period already ran this job, so it must not be enqueued.
	void markDone() { m_done = true; }

	CountingJob* followUp = nullptr;
	int runs = 0;
	std::thread::id runner;

protected:
	void doProcessing() override
	{
		++runs;
		++m_total;
		runner = std::this_thread::get_id();
		if (m_settle.count() > 0)
		{
			std::this_thread::sleep_for(m_settle);
		}
		m_done = true;
		if (followUp != nullptr)
		{
			AudioEngineWorkerThread::addJob(followUp);
		}
	}

private:
	int& m_total;
	std::chrono::milliseconds m_settle;
	bool m_done = false;
};

//! Stop a test worker the way AudioEngine's own shutdown does (src/core/AudioEngine.cpp:
//! quit(), then one startAndWaitForJobs() to wake the condition, then wait()).
//!
//! The switch is cleared first on purpose: a deterministic render never wakes the pool, so
//! a worker left blocked by one can only be released once the switch is off.
void stopWorker(AudioEngineWorkerThread& worker)
{
	AudioEngineWorkerThread::setDeterministicProcessing(false);
	worker.quit();
	AudioEngineWorkerThread::startAndWaitForJobs();
	worker.wait(5000);
	QVERIFY2(!worker.isRunning(), "the test's own worker thread did not stop");
}

//! `count` jobs, each sleeping `settle`, plus the pointer list the queue takes.
std::vector<std::unique_ptr<CountingJob>> makeJobs(int& total, int count,
	std::chrono::milliseconds settle = std::chrono::milliseconds{0},
	std::vector<ThreadableJob*>* pointers = nullptr)
{
	std::vector<std::unique_ptr<CountingJob>> jobs;
	for (int i = 0; i < count; ++i)
	{
		jobs.push_back(std::make_unique<CountingJob>(total, settle));
		if (pointers != nullptr)
		{
			pointers->push_back(jobs.back().get());
		}
	}
	return jobs;
}

//! Every job ran exactly once, on `expected`.
void expectAllRanOn(const std::vector<std::unique_ptr<CountingJob>>& jobs,
	const std::thread::id& expected)
{
	for (const auto& job : jobs)
	{
		QCOMPARE(job->runs, 1);
		QVERIFY2(job->runner == expected, "a job ran on a thread other than the expected one");
	}
}

} // namespace


class RenderJobQueueTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// One pool worker for the whole run. It exists for two reasons: it makes
		// queueReadyWaitCond non-null, exactly as it always is under a real AudioEngine
		// (a case that calls startAndWaitForJobs() with no worker ever constructed would
		// otherwise dereference a null condition), and it is the thread that steals a job
		// from a render that lets the pool take the work - which is the failure the
		// regression case below is looking for.
		m_poolWorker = new AudioEngineWorkerThread(nullptr);
		m_poolWorker->start();
	}

	void cleanupTestCase()
	{
		stopWorker(*m_poolWorker);
		delete m_poolWorker;
		m_poolWorker = nullptr;
	}

	void init()
	{
		// Every case starts from the engine's own default.
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		QVERIFY(!AudioEngineWorkerThread::deterministicProcessing());
	}

	//! The renderer's switch must not leak into live playback.
	void deterministicProcessingIsOffByDefaultAndResettable()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		QVERIFY(AudioEngineWorkerThread::deterministicProcessing());
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		QVERIFY(!AudioEngineWorkerThread::deterministicProcessing());
	}

	//! Inline mode: every job once, on this thread, including a job queued mid-drain.
	void inlineModeRunsEveryJobOnceOnTheCallingThread()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		int total = 0;
		auto first = std::make_unique<CountingJob>(total, std::chrono::milliseconds{2});
		auto second = std::make_unique<CountingJob>(total, std::chrono::milliseconds{2});
		first->followUp = second.get();

		AudioEngineWorkerThread::resetJobQueue(
			AudioEngineWorkerThread::JobQueue::OperationMode::Dynamic);
		AudioEngineWorkerThread::addJob(first.get());
		AudioEngineWorkerThread::startAndWaitForJobs();

		QCOMPARE(first->runs, 1);
		QCOMPARE(second->runs, 1);
		QCOMPARE(total, 2);
		const auto caller = std::this_thread::get_id();
		QVERIFY2(first->runner == caller, "the first job did not run on the calling thread");
		QVERIFY2(second->runner == caller, "the mid-drain job did not run on the calling thread");
	}

	//! Static mode (fillJobQueue, the play-handle and bus stages) behaves the same.
	void inlineModeDrainsAStaticQueueOnTheCallingThread()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		int total = 0;
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 16, std::chrono::milliseconds{1}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		expectAllRanOn(jobs, std::this_thread::get_id());
		QCOMPARE(total, 16);
	}

	//! THE regression case. With the pool awake and jobs that take real time, a render that
	//! let the pool take the work would process some job on another thread - which is
	//! exactly the variability the export switch exists to remove.
	void inlineModeNeverLetsThePoolTakeAJob()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		AudioEngineWorkerThread poolWorker(nullptr);
		poolWorker.start();

		int total = 0;
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 16, std::chrono::milliseconds{4}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		expectAllRanOn(jobs, std::this_thread::get_id());

		stopWorker(poolWorker);
	}

	//! With the switch off the pool path is unchanged: a started worker may take jobs, and
	//! every job still runs exactly once. This is the live-playback contract, and it must
	//! hold with or without the export switch.
	void poolModeStillRunsEveryJobExactlyOnce()
	{
		AudioEngineWorkerThread poolWorker(nullptr);
		poolWorker.start();

		int total = 0;
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 64, std::chrono::milliseconds{1}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		for (const auto& job : jobs)
		{
			QCOMPARE(job->runs, 1);
		}
		QCOMPARE(total, 64);

		stopWorker(poolWorker);
	}

	//! A job that says it does not need processing must not be enqueued at all
	//! (ThreadableJob::requiresProcessing() is the pool's and the inline path's shared
	//! filter; a change that bypassed it would run every job every period).
	void jobsThatDoNotRequireProcessingAreNotRun()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		int total = 0;
		auto job = std::make_unique<CountingJob>(total);
		job->markDone();

		AudioEngineWorkerThread::resetJobQueue();
		AudioEngineWorkerThread::addJob(job.get());
		AudioEngineWorkerThread::startAndWaitForJobs();

		QCOMPARE(job->runs, 0);
		QCOMPARE(total, 0);
	}

private:
	AudioEngineWorkerThread* m_poolWorker = nullptr;
};

QTEST_GUILESS_MAIN(RenderJobQueueTest)
#include "RenderJobQueueTest.moc"
