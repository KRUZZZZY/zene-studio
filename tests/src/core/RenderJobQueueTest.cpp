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
//
// Two functions of this file's own test harness are also load-bearing (report:
// docs/TEARDOWN-ABORT-SWEEP.md):
//
//  * `poolModeStillRunsEveryJobExactlyOnce` counted the jobs it ran in a plain `int&`
//    that BOTH the pool worker and the calling thread increment. Two threads
//    incrementing one int lose an update whenever they overlap - measured 3 aborts in
//    10 runs on the release line's instrumented coverage configuration, 6 in 50 runs
//    with this case's object built the way that configuration builds it, and 3 in 68
//    runs in the release configuration while sibling lanes were compiling (0 in 205
//    once the box went quiet), so the rate is a property of the box, not of the code.
//    The lost update was NOT a lost job: the failure is reported by the aggregate after
//    all 64 per-job `runs` assertions passed, so every job ran exactly once and only
//    the counter lost an increment. It is atomic now, which makes the aggregate a real
//    cross-check of the per-job observations.
//  * a failed QCOMPARE/QVERIFY returns from the case before `stopWorker()` runs, so the
//    case's own AudioEngineWorkerThread was destroyed while its thread was still parked
//    in the wait condition. Qt answers that with
//        qFatal("QThread: Destroyed while thread is still running")
//    i.e. SIGABRT / exit 134 - which hides the assertion that actually failed and throws
//    away the whole test binary's results. Every case here now observes, joins, and only
//    then asserts.

#include "AudioEngineWorkerThread.h"
#include "ThreadableJob.h"

#include <QtTest>

#include <atomic>
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
//!
//! The counter it feeds is atomic and shared: in pool mode the calling thread and the
//! pool worker both run these jobs, so `++total` has two writers. As a plain int it
//! lost an increment in about one run in three on a loaded box, and a lost increment
//! is indistinguishable from a dropped job unless the per-job observations are also
//! exact (see the file comment).
class CountingJob : public ThreadableJob
{
public:
	explicit CountingJob(std::atomic<int>& total, std::chrono::milliseconds settle = std::chrono::milliseconds{0})
		: m_total(total), m_settle(settle)
	{
	}

	bool requiresProcessing() const override { return !m_done; }

	//! Pretend an earlier period already ran this job, so it must not be enqueued.
	void markDone() { m_done = true; }

	CountingJob* followUp = nullptr;
	//! Per-job observations: written once by the thread that ran the job, read by the
	//! test only after the worker has been joined.
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
	std::atomic<int>& m_total;
	std::chrono::milliseconds m_settle;
	bool m_done = false;
};

//! Stop a test worker the way AudioEngine's own shutdown does (src/core/AudioEngine.cpp:
//! quit(), then one startAndWaitForJobs() to wake the condition, then join()).
//!
//! The switch is cleared first on purpose: a deterministic render never wakes the pool, so
//! a worker left blocked by one can only be released once the switch is off.
//!
//! Returns whether the thread ended within the budget. A caller that gets false must NOT
//! let the worker be destroyed: destroying a running QThread is qFatal (SIGABRT), which
//! would hide the assertion that has already failed, so it releases the pointer instead -
//! the same deliberate leak as AudioEngineTeardownTest's stranded-worker case.
bool stopWorker(AudioEngineWorkerThread& worker)
{
	AudioEngineWorkerThread::setDeterministicProcessing(false);
	worker.quit();
	AudioEngineWorkerThread::startAndWaitForJobs();
	return worker.wait(5000) && !worker.isRunning();
}

//! `count` jobs, each sleeping `settle`, plus the pointer list the queue takes.
std::vector<std::unique_ptr<CountingJob>> makeJobs(std::atomic<int>& total, int count,
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
		m_poolWorker = std::make_unique<AudioEngineWorkerThread>(nullptr);
		m_poolWorker->start();
	}

	void cleanupTestCase()
	{
		// Joined the same way the cases join their own workers, and not destroyed if
		// the join failed: ~QThread on a running thread is qFatal, i.e. an abort that
		// would be attributed to whichever case ran last.
		const bool joined = stopWorker(*m_poolWorker);
		if (!joined)
		{
			m_poolWorker.release();
		}
		QVERIFY2(joined, "the fixture's worker thread did not stop");
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

		std::atomic<int> total{0};
		auto first = std::make_unique<CountingJob>(total, std::chrono::milliseconds{2});
		auto second = std::make_unique<CountingJob>(total, std::chrono::milliseconds{2});
		first->followUp = second.get();

		AudioEngineWorkerThread::resetJobQueue(
			AudioEngineWorkerThread::JobQueue::OperationMode::Dynamic);
		AudioEngineWorkerThread::addJob(first.get());
		AudioEngineWorkerThread::startAndWaitForJobs();

		QCOMPARE(first->runs, 1);
		QCOMPARE(second->runs, 1);
		QCOMPARE(total.load(), 2);
		const auto caller = std::this_thread::get_id();
		QVERIFY2(first->runner == caller, "the first job did not run on the calling thread");
		QVERIFY2(second->runner == caller, "the mid-drain job did not run on the calling thread");
	}

	//! Static mode (fillJobQueue, the play-handle and bus stages) behaves the same.
	void inlineModeDrainsAStaticQueueOnTheCallingThread()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		std::atomic<int> total{0};
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 16, std::chrono::milliseconds{1}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		expectAllRanOn(jobs, std::this_thread::get_id());
		QCOMPARE(total.load(), 16);
	}

	//! THE regression case. With the pool awake and jobs that take real time, a render that
	//! let the pool take the work would process some job on another thread - which is
	//! exactly the variability the export switch exists to remove.
	void inlineModeNeverLetsThePoolTakeAJob()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		auto poolWorker = std::make_unique<AudioEngineWorkerThread>(nullptr);
		poolWorker->start();

		std::atomic<int> total{0};
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 16, std::chrono::milliseconds{4}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		// Join before asserting: `expectAllRanOn` below can return early on a failed
		// comparison, and a return from this scope destroys `poolWorker` - whose thread
		// is parked in the wait condition. ~QThread on a running thread is qFatal
		// (SIGABRT), and that abort would replace the comparison that failed.
		const bool joined = stopWorker(*poolWorker);
		if (!joined)
		{
			poolWorker.release();
		}

		QVERIFY2(joined, "the test's own worker thread did not stop");
		expectAllRanOn(jobs, std::this_thread::get_id());
	}

	//! With the switch off the pool path is unchanged: a started worker may take jobs, and
	//! every job still runs exactly once. This is the live-playback contract, and it must
	//! hold with or without the export switch.
	void poolModeStillRunsEveryJobExactlyOnce()
	{
		// Held by pointer, not on the stack: the assertions below come after the join,
		// so nothing this case does can destroy the QThread while its thread runs.
		auto poolWorker = std::make_unique<AudioEngineWorkerThread>(nullptr);
		poolWorker->start();

		std::atomic<int> total{0};
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, 64, std::chrono::milliseconds{1}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		const bool joined = stopWorker(*poolWorker);
		if (!joined)
		{
			poolWorker.release();
		}

		QVERIFY2(joined, "the test's own worker thread did not stop");
		for (const auto& job : jobs)
		{
			QCOMPARE(job->runs, 1);
		}
		QCOMPARE(total.load(), 64);
	}

	//! The same contract at a scale where a counter shared by two threads cannot quietly
	//! lose an increment. The case above paces its jobs 1 ms apart, so the calling thread
	//! and the pool worker overlap only by accident; here 512 jobs with no settle have
	//! both threads draining the same queue at once, which is what a non-atomic counter
	//! cannot survive (measured red 36 of 40 runs with a plain int, and green 40 of 40
	//! with the atomic). The per-job assertions stay: the aggregate is a cross-check of
	//! them, never a replacement.
	void poolModeAccountingIsExactWhenBothThreadsDrain()
	{
		auto poolWorker = std::make_unique<AudioEngineWorkerThread>(nullptr);
		poolWorker->start();

		constexpr int kJobs = 512;
		std::atomic<int> total{0};
		std::vector<ThreadableJob*> pointers;
		const auto jobs = makeJobs(total, kJobs, std::chrono::milliseconds{0}, &pointers);

		AudioEngineWorkerThread::fillJobQueue(pointers);
		AudioEngineWorkerThread::startAndWaitForJobs();

		const bool joined = stopWorker(*poolWorker);
		if (!joined)
		{
			poolWorker.release();
		}

		QVERIFY2(joined, "the test's own worker thread did not stop");
		int observed = 0;
		for (const auto& job : jobs)
		{
			QCOMPARE(job->runs, 1);
			observed += job->runs;
		}
		QCOMPARE(observed, kJobs);
		QCOMPARE(total.load(), kJobs);
	}

	//! A job that says it does not need processing must not be enqueued at all
	//! (ThreadableJob::requiresProcessing() is the pool's and the inline path's shared
	//! filter; a change that bypassed it would run every job every period).
	void jobsThatDoNotRequireProcessingAreNotRun()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(true);

		std::atomic<int> total{0};
		auto job = std::make_unique<CountingJob>(total);
		job->markDone();

		AudioEngineWorkerThread::resetJobQueue();
		AudioEngineWorkerThread::addJob(job.get());
		AudioEngineWorkerThread::startAndWaitForJobs();

		QCOMPARE(job->runs, 0);
		QCOMPARE(total.load(), 0);
	}

private:
	std::unique_ptr<AudioEngineWorkerThread> m_poolWorker;
};

QTEST_GUILESS_MAIN(RenderJobQueueTest)
#include "RenderJobQueueTest.moc"
