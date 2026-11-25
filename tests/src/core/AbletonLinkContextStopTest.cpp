/*
 * AbletonLinkContextStopTest.cpp - Link's asio Context stopped from its own worker.
 *
 * THE CLAIM: Context::stop() (src/3rdparty/ableton-link/link/include/ableton/
 * platforms/asio/Context.hpp) can be called from a handler that is running on the
 * context's own worker thread - a Link callback - without terminating the process,
 * and the worker is still joined afterwards, by a later stop() from another thread
 * or by the context's destructor.
 *
 * HOW. The context's worker is created by its ThreadFactory template argument, so
 * the test passes its own (ObservedThreadFactory): a factory that makes the same
 * thread and counts the worker functions that have returned. A handler posted with
 * async() runs on that worker and calls stop() from there; the test waits for the
 * handler to return through a promise, never on a clock. The count then shows
 * whether stop() or the destructor waited for the worker thread to leave, which a
 * detached thread would not.
 *
 * WHAT IT CANNOT PROVE: that every platform's thread library reports the self-join
 * the same way - the test asserts the contract (no self-join, the worker still
 * joined), not one library's error code. Local ThreadSanitizer does not run on
 * this box, so the ordering argument is by inspection: the join is the only
 * synchronisation, and std::thread::join synchronises with the thread's exit.
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
 */

#include <QtTest>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include <ableton/platforms/asio/Context.hpp>

namespace
{

//! A bound so a deadlock fails the test instead of hanging the suite. No assertion
//! reads a duration: every wait is either ready or a failure (see the file comment).
constexpr auto deadlockGuard = std::chrono::seconds(30);

struct NullLog
{
};

//! The interface scan is never called by these tests.
struct NoInterfaces
{
	std::vector<ableton::discovery::IpAddress> operator()() const { return {}; }
};

//! The context's thread seam: it makes the worker the default factory would, and
//! then counts that the worker function returned. A joined worker has returned by
//! the time the joining thread continues; a detached one has not necessarily.
struct ObservedThreadFactory
{
	static inline std::atomic<int> workerReturned{0};

	template <typename Callable, typename... Args>
	static std::thread makeThread(std::string, Callable&& f, Args&&... args)
	{
		auto body = std::bind(std::forward<Callable>(f), std::forward<Args>(args)...);
		return std::thread(
			[body = std::move(body)]() mutable
			{
				body();
				workerReturned.fetch_add(1, std::memory_order_release);
			});
	}
};

using TestContext = ableton::platforms::asio::Context<NoInterfaces, NullLog, ObservedThreadFactory>;

//! An exception handler of the context's own shape: the worker catches only
//! `ExceptionHandler::Exception`, and runs this one where it catches it.
struct ReportingHandler
{
	struct Exception
	{
		int code;
	};

	std::atomic<int>* code;
	std::promise<void>* caught;

	void operator()(const Exception& e)
	{
		code->store(e.code, std::memory_order_relaxed);
		caught->set_value();
	}
};

} // namespace

class AbletonLinkContextStopTest : public QObject
{
	Q_OBJECT

private slots:
	void init() { ObservedThreadFactory::workerReturned.store(0, std::memory_order_relaxed); }

	void stopFromTheWorkerThreadReturnsInsteadOfJoiningItself();
	void theDestructorJoinsAWorkerThatStoppedItself();
	void stopFromAnotherThreadJoinsTheWorker();
	void theWorkerSurvivesAnExceptionItHandles();
	void aMovedFromContextStopsAsANoOp();
};

//! RED without the fix: stop() joins the calling thread, std::thread::join throws
//! std::system_error out of the handler, the worker's loop catches only its own
//! exception type, and the exception escapes the thread - std::terminate.
void AbletonLinkContextStopTest::stopFromTheWorkerThreadReturnsInsteadOfJoiningItself()
{
	std::promise<std::thread::id> workerThread;
	std::promise<void> stopped;
	{
		TestContext context;
		auto workerId = workerThread.get_future();
		auto stoppedFuture = stopped.get_future();
		context.async(
			[&context, &workerThread, &stopped]
			{
				workerThread.set_value(std::this_thread::get_id());
				context.stop();  // from the worker: no self-join, and this returns
				stopped.set_value();
			});

		QCOMPARE(stoppedFuture.wait_for(deadlockGuard), std::future_status::ready);
		QVERIFY2(workerId.get() != std::this_thread::get_id(),
			"the handler must have run on the context's worker thread, not on this one");

		// The worker has left its loop. A stop() from another thread joins it, so the
		// worker function has returned by the time this returns.
		context.stop();
		QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
	}
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
}

//! The second half of the bug: stop() left the worker joinable, and the destructor's
//! join was behind `mpWork`, which stop() had already reset - a std::thread
//! destroyed while joinable calls std::terminate.
void AbletonLinkContextStopTest::theDestructorJoinsAWorkerThatStoppedItself()
{
	std::promise<void> stopped;
	{
		TestContext context;
		auto stoppedFuture = stopped.get_future();
		context.async(
			[&context, &stopped]
			{
				context.stop();
				stopped.set_value();
			});
		QCOMPARE(stoppedFuture.wait_for(deadlockGuard), std::future_status::ready);
	}
	// The context is gone: its destructor joined the worker instead of terminating.
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
}

//! The normal path, and the counterpart of the fix's join guard: stop() from an
//! application thread waits for the worker, and a second stop() is a no-op.
void AbletonLinkContextStopTest::stopFromAnotherThreadJoinsTheWorker()
{
	TestContext context;
	context.stop();
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
	context.stop();
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
}

//! The failure path the worker's loop exists for: it catches its handler's own
//! exception type, reports it, and keeps running.
void AbletonLinkContextStopTest::theWorkerSurvivesAnExceptionItHandles()
{
	std::atomic<int> code{-1};
	std::promise<void> caught;
	auto caughtFuture = caught.get_future();
	TestContext context{ReportingHandler{&code, &caught}};

	context.async([] { throw ReportingHandler::Exception{7}; });
	QCOMPARE(caughtFuture.wait_for(deadlockGuard), std::future_status::ready);
	QCOMPARE(code.load(), 7);

	// The worker is still running the service: it leaves only when stopped.
	context.stop();
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
}

//! Reconfiguration: a moved-from context has no service and no thread, so stop()
//! must stay a no-op there rather than reach for a thread it does not own.
void AbletonLinkContextStopTest::aMovedFromContextStopsAsANoOp()
{
	TestContext source;
	TestContext moved{std::move(source)};

	source.stop();
	source.stop();
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 0);

	moved.stop();
	QCOMPARE(ObservedThreadFactory::workerReturned.load(), 1);
}

QTEST_GUILESS_MAIN(AbletonLinkContextStopTest)
#include "AbletonLinkContextStopTest.moc"
