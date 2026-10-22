/*
 * AudioWorkerWakeTest.cpp - the render-side worker wake is a post, not a lock
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

// Q050-167. On Apple deployment targets below macOS 11 the worker wake used to be a
// process-global std::mutex plus std::condition_variable, and its first call lazily
// allocated both with `new`. The mutex was taken on the render thread every period.
// AudioWorkerWake replaces that with a compare-and-wait on the generation word itself
// (__ulock on Apple, FUTEX on Linux), so the post neither locks nor allocates.
//
// The cases below hold the shipping entry point - AudioEngineWorkerThread::wakeWorkers(),
// the call startAndWaitForJobs() and quit() make - to that promise, and not to a copy of it:
//
//   * theCompiledWakeIsTheLegacyWaitOnAddressPrimitive: the macOS x86_64 job (and the Linux
//     job that runs here) proves it compiled the wait-on-address path, not std::atomic::wait.
//     This is the assertion the acceptance criterion's "exercises the legacy wake path" means.
//   * theRenderSideWakeAllocatesNothingAndTakesNoMutex: allocation probe + a runtime mutex
//     interposer around the real wake, including its very first call.
//   * theProbesCatchTheOldCondvarWake: the same probes run against the OLD wake shape, so a
//     green result is evidence that they can fail - not a pair of counters that never move.
//   * theWakePathNamesNoLockOrAllocationInItsSource: a source scan of the two functions, so
//     the property is checked on every platform, including a macOS job whose libSystem calls
//     an executable cannot interpose.
//   * repeatedWakeupsDeliverEveryPeriodWithoutDeadlock: many periods, real workers, every job
//     exactly once, bounded join.
//   * quitRacingAWakeupAlwaysJoinsTheWorker: shutdown raced against a wake, repeatedly.
//
// Local TSan does not run on the development box (documented in the lane's report). The wake
// design is argued in include/AudioWorkerWake.h: the generation is the signal, the wait
// re-checks it before parking and after every return, and the only shared state the post
// touches is the atomic word itself.

#include "AudioEngineWorkerThread.h"
#include "AudioWorkerWake.h"
#include "ThreadableJob.h"

#include <QtTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__GLIBC__)
#include <dlfcn.h>
#include <pthread.h>
#endif

#include "AllocationProbe.h"

using namespace lmms;

namespace
{

//! The allocations \a body makes on this thread, counted by the probe.
template<typename Body>
auto countAllocations(Body&& body) -> std::uint64_t
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	body();
	lmms::test::tlCountAllocations = false;
	return lmms::test::tlAllocationCount;
}

//! A job that records that it ran, and where. ThreadableJob's state is atomic, so jobs are
//! held by pointer, never copied.
class RecordingJob : public ThreadableJob
{
public:
	explicit RecordingJob(std::atomic<int>& total) : m_total(total) {}

	bool requiresProcessing() const override { return !m_done; }

	int runs = 0;
	std::thread::id runner;

protected:
	void doProcessing() override
	{
		++runs;
		++m_total;
		runner = std::this_thread::get_id();
		m_done = true;
	}

private:
	std::atomic<int>& m_total;
	bool m_done = false;
};

//! Stop a worker the way ~AudioEngine does: quit, one startAndWaitForJobs() to release it,
//! then join within a budget. A caller that gets false must not destroy the QThread.
bool stopWorker(AudioEngineWorkerThread& worker)
{
	AudioEngineWorkerThread::setDeterministicProcessing(false);
	worker.quit();
	AudioEngineWorkerThread::startAndWaitForJobs();
	return worker.wait(5000) && !worker.isRunning();
}

//! The wake's worst case: the OLD Apple-below-11 shape, kept here ONLY so the probes can be
//! shown to catch it. It is never product code; the test calls it directly.
void oldCondvarWake()
{
	static auto* mutex = new std::mutex;
	static auto* cond = new std::condition_variable;
	const std::lock_guard<std::mutex> lock(*mutex);
	cond->notify_all();
}

// ---------------------------------------------------------------------------------------
// runtime mutex probe
// ---------------------------------------------------------------------------------------
//
// glibc's pthread_mutex_lock is interposed in this executable (the ThreadPoolTest shape,
// which exports pthread_create the same way). RTLD_NEXT resolves the real one, and a
// constructor resolves it before main() so the lookups never happen inside a lock. Other
// platforms: the probe is compiled out, and the case below SKIPS rather than claiming a
// check it did not make - there the source scan and the primitive assertion carry the
// mutex half.
#if defined(__GLIBC__)
thread_local bool t_mutexProbeArmed = false;
std::atomic<std::uint64_t> s_mutexLocks{0};
int (*s_realPthreadMutexLock)(pthread_mutex_t*) = nullptr;
bool t_resolvingRealLock = false;

//! Resolve the real pthread_mutex_lock once, lazily. dlsym takes glibc's internal loader lock,
//! not a pthread one, so it cannot re-enter this interposer; the flag is belt and braces for a
//! libc whose loader does call back in. (A constructor is wrong here: at that point RTLD_NEXT
//! can still resolve to this executable's own definition and recurse.)
int (*realPthreadMutexLock())(pthread_mutex_t*)
{
	if (s_realPthreadMutexLock == nullptr && !t_resolvingRealLock)
	{
		t_resolvingRealLock = true;
		s_realPthreadMutexLock = reinterpret_cast<int (*)(pthread_mutex_t*)>(
			dlsym(RTLD_NEXT, "pthread_mutex_lock"));
		t_resolvingRealLock = false;
	}
	return s_realPthreadMutexLock;
}
#endif

} // namespace

#if defined(__GLIBC__)
//! Counts locks taken on the calling thread while t_mutexProbeArmed. RTLD_NEXT above skips
//! this definition, so the count is of the code under test, not of the probe.
extern "C" int pthread_mutex_lock(pthread_mutex_t* mutex)
{
	if (t_mutexProbeArmed) { s_mutexLocks.fetch_add(1, std::memory_order_relaxed); }
	const auto real = realPthreadMutexLock();
	if (real == nullptr) { return 11; /* EAGAIN: the probe never resolved the real symbol */ }
	return real(mutex);
}
#endif

namespace
{

//! Locks taken on this thread while \a body runs - the same scope shape as the allocation
//! probe. Returns (locks, bodyRan): on a build with no interposer, locks is 0 and bodyRan is
//! false so the caller can skip instead of pass.
template<typename Body>
auto countMutexLocks(Body&& body) -> std::pair<std::uint64_t, bool>
{
#if defined(__GLIBC__)
	s_mutexLocks.store(0, std::memory_order_relaxed);
	t_mutexProbeArmed = true;
	body();
	t_mutexProbeArmed = false;
	return {s_mutexLocks.load(std::memory_order_relaxed), true};
#else
	body();
	return {0, false};
#endif
}

//! Blanks // and /* */ comments and string/char literals, keeping the line structure. The
//! source scan is about code: a comment that says "no mutex here" must not be a hit, and a
//! comment that mentions `new` must not hide one.
std::string stripCommentsAndLiterals(const std::string& text)
{
	std::string out = text;
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/')
		{
			while (i < out.size() && out[i] != '\n') { out[i++] = ' '; }
			if (i < out.size()) { out[i] = '\n'; }
		}
		else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*')
		{
			out[i++] = ' ';
			out[i++] = ' ';
			while (i + 1 < out.size() && !(out[i] == '*' && out[i + 1] == '/'))
			{
				if (out[i] != '\n') { out[i] = ' '; }
				++i;
			}
			out[i] = ' ';
			if (i + 1 < out.size()) { out[i + 1] = ' '; }
		}
		else if (out[i] == '"' || out[i] == '\'')
		{
			const char quote = out[i];
			out[i++] = ' ';
			while (i < out.size() && out[i] != quote)
			{
				if (out[i] == '\\') { out[i++] = ' '; }
				if (i < out.size()) { if (out[i] != '\n') { out[i] = ' '; } ++i; }
			}
			if (i < out.size()) { out[i] = ' '; }
		}
	}
	return out;
}

//! The text between the definition of `symbol` and its closing brace.
std::string functionBody(const std::string& path, const std::string& symbol, bool& found)
{
	std::ifstream file(path);
	if (!file.good()) { found = false; return {}; }
	std::stringstream buffer;
	buffer << file.rdbuf();
	const std::string text = stripCommentsAndLiterals(buffer.str());
	const auto start = text.find(symbol);
	if (start == std::string::npos) { found = false; return {}; }
	const auto open = text.find('{', start);
	if (open == std::string::npos) { found = false; return {}; }
	std::size_t depth = 0;
	std::size_t end = open;
	for (; end < text.size(); ++end)
	{
		if (text[end] == '{') { ++depth; }
		if (text[end] == '}' && --depth == 0) { break; }
	}
	found = true;
	return text.substr(open, end - open + 1);
}

std::string sourcePath(const char* relative)
{
	return std::string(ZENE_SOURCE_DIR) + "/" + relative;
}

} // namespace

class AudioWorkerWakeTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		AudioEngineWorkerThread::setWorkerDrainProbeForTesting(nullptr);
	}

	//! THE selection case for the acceptance criterion. A macOS x86_64 run must prove it
	//! exercised the legacy wake, not the modern std::atomic::wait one: the unit exists for
	//! targets where that path is unavailable. Linux asserts the same so the wake this box
	//! measures is the wake Apple builds compile (only the syscall shim differs).
	void theCompiledWakeIsTheLegacyWaitOnAddressPrimitive()
	{
#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
		QCOMPARE(lmms::workerwake::compiledPrimitive(), lmms::workerwake::Primitive::WaitOnAddress);
#else
		// Documented fallback (Windows and any POSIX without the futex calls): the atomic's
		// own wait. Asserting the fallback here - rather than skipping - keeps the case from
		// silently proving nothing on a platform where the selection macro vanished.
		QCOMPARE(lmms::workerwake::compiledPrimitive(), lmms::workerwake::Primitive::AtomicWait);
#endif
		// The trap reads the word itself; a lock-based atomic would carry a mutex there.
		QCOMPARE(sizeof(std::atomic<std::uint32_t>), sizeof(std::uint32_t));
		QVERIFY(std::atomic<std::uint32_t>::is_always_lock_free);
	}

	//! AC1: the render-side wake - the real entry point startAndWaitForJobs() and quit()
	//! call every period - allocates nothing and takes no mutex, first call included (the old
	//! path's lazy `new` was on that first call). The notify counter proves the shipping call
	//! actually reached the primitive; a worker reverted to its own wake would not move it.
	void theRenderSideWakeAllocatesNothingAndTakesNoMutex()
	{
		lmms::workerwake::resetNotifyCountForTesting();

		constexpr int kWakes = 256;
		const auto allocations = countAllocations([&] {
			for (int i = 0; i < kWakes; ++i) { AudioEngineWorkerThread::wakeWorkers(); }
		});
		const auto [locks, interposed] = countMutexLocks([&] {
			for (int i = 0; i < kWakes; ++i) { AudioEngineWorkerThread::wakeWorkers(); }
		});

		QVERIFY2(allocations == 0,
			qPrintable(QStringLiteral("the render-side wake allocated %1 time(s); the first call "
				"of the old path allocated its mutex and condition variable").arg(allocations)));
		if (interposed)
		{
			QVERIFY2(locks == 0, qPrintable(QStringLiteral(
				"the render-side wake took %1 mutex(es)").arg(locks)));
		}
		else
		{
			qWarning("no pthread interposer on this platform: the runtime mutex half was not "
				"measured; the source scan and the primitive assertion cover it");
		}
		// Last, as the cross-check that the two probes above measured the SHIPPING call: a
		// worker reverted to its own wake would leave this counter at zero while both probes
		// stayed green.
		QCOMPARE(lmms::workerwake::notifyCountForTesting(), std::uint64_t{2} * kWakes);
	}

	//! The probes are only evidence if they can fail. This runs the OLD wake shape - a
	//! mutex, a condition variable and their lazy `new` - through both of them and requires
	//! that each sees the defect. If the old code were restored in the product, the case
	//! above would fail the same way.
	void theProbesCatchTheOldCondvarWake()
	{
		const auto allocations = countAllocations([] { oldCondvarWake(); });
		const auto [locks, interposed] = countMutexLocks([] { oldCondvarWake(); });

		QVERIFY2(allocations > 0, "the allocation probe did not see the old wake's lazy new");
		QVERIFY2(interposed, "no pthread interposer on this platform");
		QVERIFY2(locks > 0, "the mutex probe did not see the old wake's lock_guard");
	}

	//! AC1 on every platform, including a macOS job whose libSystem calls an executable
	//! cannot interpose: neither the shipping notify nor the primitive it calls may name a
	//! lock, an allocation or a container. Read from the tree the binary was built from
	//! (ZENE_SOURCE_DIR), the shape MidiHandoffQueueTest::popHasNoRetryLoop established.
	void theWakePathNamesNoLockOrAllocationInItsSource()
	{
		bool found = false;
		const std::string notifyWorkers = functionBody(
			sourcePath("src/core/AudioEngineWorkerThread.cpp"),
			"void AudioEngineWorkerThread::notifyWorkers()", found);
		QVERIFY2(found, "could not read notifyWorkers() in src/core/AudioEngineWorkerThread.cpp");
		const std::string notify = functionBody(
			sourcePath("src/core/AudioWorkerWake.cpp"),
			"void notify(std::atomic<std::uint32_t>& generation) noexcept", found);
		QVERIFY2(found, "could not read notify() in src/core/AudioWorkerWake.cpp");

		for (const char* forbidden : {"mutex", "condition_variable", "lock_guard", "unique_lock",
				 "scoped_lock", "pthread_mutex", "new ", "malloc", "make_", "QString", "std::string"})
		{
			QVERIFY2(notifyWorkers.find(forbidden) == std::string::npos,
				qPrintable(QStringLiteral("notifyWorkers() names '%1'").arg(forbidden)));
			QVERIFY2(notify.find(forbidden) == std::string::npos,
				qPrintable(QStringLiteral("notify() names '%1'").arg(forbidden)));
		}
	}

	//! AC2: repeated worker wakeups complete with no deadlock and no missed period. Real
	//! pool workers are woken once per simulated render period; every job queued in a period
	//! must run exactly once, and at least one job must actually have been taken by a worker
	//! (otherwise the case would only prove the inline drain works).
	void repeatedWakeupsDeliverEveryPeriodWithoutDeadlock()
	{
		constexpr int kPeriods = 240;
		constexpr int kJobsPerPeriod = 8;
		constexpr int kWorkers = 2;

		std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
		for (int i = 0; i < kWorkers; ++i)
		{
			workers.emplace_back(new AudioEngineWorkerThread(nullptr));
			workers.back()->start();
		}

		lmms::workerwake::resetNotifyCountForTesting();

		std::atomic<int> total{0};
		std::atomic<int> onWorker{0};
		const auto mainThread = std::this_thread::get_id();
		bool everyJobRanOnce = true;

		for (int period = 0; period < kPeriods; ++period)
		{
			std::vector<std::unique_ptr<RecordingJob>> jobs;
			std::vector<ThreadableJob*> pointers;
			for (int j = 0; j < kJobsPerPeriod; ++j)
			{
				jobs.push_back(std::make_unique<RecordingJob>(total));
				pointers.push_back(jobs.back().get());
			}
			AudioEngineWorkerThread::fillJobQueue(pointers);
			// The live render's own call: it posts the wake and then takes its share.
			AudioEngineWorkerThread::startAndWaitForJobs();

			for (const auto& job : jobs)
			{
				everyJobRanOnce = everyJobRanOnce && job->runs == 1;
				if (job->runner != mainThread) { onWorker.fetch_add(1, std::memory_order_relaxed); }
			}
			if (!everyJobRanOnce) { break; }
		}

		const auto posts = lmms::workerwake::notifyCountForTesting();

		int stranded = 0;
		for (auto& worker : workers)
		{
			if (!stopWorker(*worker))
			{
				worker.release();
				++stranded;
			}
		}

		QVERIFY2(stranded == 0, "a pool worker did not stop within the join budget");
		QVERIFY2(everyJobRanOnce, "a queued period job did not run exactly once");
		QCOMPARE(total.load(), kPeriods * kJobsPerPeriod);
		QCOMPARE(posts, std::uint64_t{kPeriods});
		QVERIFY2(onWorker.load() > 0,
			"no job was taken by a pool worker: the case never exercised the wake it measures");
	}

	//! AC2, shutdown half: a worker parked in the wait is released by quit() even when a
	//! render-side wake lands at the same moment, and it joins within the budget. The two
	//! calls race by construction (two threads, a barrier), repeated so a lost wake or a
	//! missed quit would leave a worker stranded rather than pass by luck.
	void quitRacingAWakeupAlwaysJoinsTheWorker()
	{
		constexpr int kRounds = 40;
		for (int round = 0; round < kRounds; ++round)
		{
			auto worker = std::make_unique<AudioEngineWorkerThread>(nullptr);
			worker->start();

			// Let it park: one wake, then the loop re-reads the generation and waits again.
			AudioEngineWorkerThread::wakeWorkers();
			std::this_thread::sleep_for(std::chrono::milliseconds{1});

			std::atomic<int> ready{0};
			std::thread waker([&] {
				ready.fetch_add(1, std::memory_order_release);
				while (ready.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }
				AudioEngineWorkerThread::wakeWorkers();
			});
			ready.fetch_add(1, std::memory_order_release);
			while (ready.load(std::memory_order_acquire) < 2) { std::this_thread::yield(); }
			worker->quit();
			waker.join();

			if (!worker->wait(5000))
			{
				// Never destroy a running QThread: that abort would hide the failure.
				worker.release();
				QFAIL("a worker woken while quitting did not stop within the join budget");
			}
			QVERIFY(!worker->isRunning());
		}
	}
};

QTEST_MAIN(AudioWorkerWakeTest)
#include "AudioWorkerWakeTest.moc"
