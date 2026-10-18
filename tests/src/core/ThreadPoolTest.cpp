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

#include <QtTest>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

// Construction and shutdown are intentionally private. Expose them only in
// this test translation unit so it can synchronize on shutdown deterministically.
#define private public
#include "ThreadPool.h"
#undef private

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
};

QTEST_GUILESS_MAIN(ThreadPoolTest)
#include "ThreadPoolTest.moc"
