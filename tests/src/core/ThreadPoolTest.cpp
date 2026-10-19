/*
 * ThreadPoolTest.cpp - queued task completion during destruction
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

#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#if defined(__GLIBC__)
#include <dlfcn.h>
#include <pthread.h>
#endif

// Construction and shutdown are intentionally private. Expose them only in
// this test translation unit so it can synchronize on shutdown deterministically.
#define private public
#include "ThreadPool.h"
#undef private

namespace
{

// The child process allows s_createsBeforeFailure more successful pthread_create
// calls, then makes the next one fail with EAGAIN (a negative value disables it).
std::atomic<int> s_createsBeforeFailure{-1};
std::atomic<int> s_injectedFailures{0};
std::atomic<int> s_successfulCreates{0};

#if defined(__GLIBC__)
using PthreadCreate = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
PthreadCreate s_realPthreadCreate = nullptr;
#endif

} // namespace

#if defined(__GLIBC__)
// Interposes glibc's pthread_create in this executable (linked with ENABLE_EXPORTS),
// so the worker launches of std::thread can be made to fail on demand.
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
	void* (*startRoutine)(void*), void* argument)
{
	if (s_realPthreadCreate == nullptr) { return ENOSYS; }

	// Only the child arms the countdown, and only its constructing thread creates threads.
	const auto remaining = s_createsBeforeFailure.load(std::memory_order_relaxed);
	if (remaining >= 0)
	{
		s_createsBeforeFailure.store(remaining - 1, std::memory_order_relaxed);
		if (remaining == 0)
		{
			s_injectedFailures.fetch_add(1, std::memory_order_relaxed);
			return EAGAIN;
		}
	}

	const auto result = s_realPthreadCreate(thread, attributes, startRoutine, argument);
	if (result == 0) { s_successfulCreates.fetch_add(1, std::memory_order_relaxed); }
	return result;
}
#endif

// Child-process body: a two-worker pool whose second launch fails must stop and join
// the first worker before the constructor rethrows. Before that fix, the joinable first
// worker reaches std::thread's destructor during unwinding and the process aborts.
int runPartialStartupChild()
{
#if !defined(__GLIBC__)
	return 77;
#else
	s_createsBeforeFailure.store(1, std::memory_order_relaxed);
	try
	{
		lmms::ThreadPool pool{2};
		return 3;
	}
	catch (const std::system_error&)
	{
		return s_successfulCreates.load(std::memory_order_relaxed) == 1
			&& s_injectedFailures.load(std::memory_order_relaxed) == 1 ? 0 : 2;
	}
#endif
}

struct RvalueQualifiedCallable
{
	auto operator()() && -> int { return 413; }
	auto operator()() & -> int = delete;
};

struct LvalueQualifiedCallable
{
	auto operator()() & -> int { return 41; }
	auto operator()() && -> int = delete;
};

class ThreadPoolTest : public QObject
{
	Q_OBJECT
private slots:
	void enqueuesMutableCallable()
	{
		auto future = lmms::ThreadPool::instance().enqueue([count = 0]() mutable { return ++count; });
		QCOMPARE(future.get(), 1);
	}

	void enqueuesMoveOnlyCallable()
	{
		auto result = std::make_shared<int>(0);
		auto future = lmms::ThreadPool::instance().enqueue([owned = std::make_unique<int>(41), result]() mutable {
			*result = ++*owned;
		});
		future.get();
		QCOMPARE(*result, 42);
	}

	void enqueuesMoveOnlyArgument()
	{
		auto result = std::make_shared<int>(0);
		auto future = lmms::ThreadPool::instance().enqueue(
			[result](std::unique_ptr<int> value) { *result = *value; }, std::make_unique<int>(413));
		future.get();
		QCOMPARE(*result, 413);
	}

	void enqueuesRvalueQualifiedCallable()
	{
		auto future = lmms::ThreadPool::instance().enqueue(RvalueQualifiedCallable{});
		QCOMPARE(future.get(), 413);
	}

	void enqueuesLvalueQualifiedCallable()
	{
		LvalueQualifiedCallable callable;
		auto future = lmms::ThreadPool::instance().enqueue(callable);
		QCOMPARE(future.get(), 41);
	}

	void destructionDrainsQueuedTasks()
	{
		auto pool = std::make_unique<lmms::ThreadPool>(1);
		auto* const poolState = pool.get();
		auto startedPromise = std::make_shared<std::promise<void>>();
		auto started = startedPromise->get_future();
		auto releasePromise = std::make_shared<std::promise<void>>();
		auto release = releasePromise->get_future().share();
		auto runningTask = pool->enqueue([startedPromise, release] {
			startedPromise->set_value();
			release.wait();
		});
		started.wait();

		std::atomic<bool> queuedTaskRan{false};
		auto queuedTask = pool->enqueue([&queuedTaskRan] {
			queuedTaskRan.store(true, std::memory_order_release);
		});
		std::thread destroyer([&pool] { pool.reset(); });
		while (!poolState->m_done.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		releasePromise->set_value();
		destroyer.join();

		runningTask.get();
		try
		{
			queuedTask.get();
		}
		catch (const std::future_error&)
		{
			QFAIL("pool destruction abandoned the queued task's promise");
		}
		QVERIFY(queuedTaskRan.load(std::memory_order_acquire));
	}

	void partialStartupFailureStopsStartedWorkers()
	{
#if !defined(__GLIBC__)
		QSKIP("Deterministic pthread failure injection requires glibc symbol interposition.");
#else
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--thread-pool-failure-child")});
		QVERIFY2(child.waitForStarted(5000), qPrintable(child.errorString()));
		if (!child.waitForFinished(10000))
		{
			child.kill();
			child.waitForFinished(5000);
			QFAIL("The child did not unwind after a worker-start failure.");
		}
		QVERIFY2(child.exitStatus() == QProcess::NormalExit, "The child crashed instead of unwinding.");
		QCOMPARE(child.exitCode(), 0);
#endif
	}
};

int main(int argc, char** argv)
{
#if defined(__GLIBC__)
	const auto libc = dlopen("libc.so.6", RTLD_NOW);
	if (libc == nullptr) { return 127; }
	s_realPthreadCreate = reinterpret_cast<PthreadCreate>(dlsym(libc, "pthread_create"));
	if (s_realPthreadCreate == nullptr) { return 127; }
#endif
	QCoreApplication app{argc, argv};
	if (argc > 1 && std::strcmp(argv[1], "--thread-pool-failure-child") == 0)
	{
		return runPartialStartupChild();
	}
	ThreadPoolTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ThreadPoolTest.moc"
