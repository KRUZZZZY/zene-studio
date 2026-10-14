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

class ThreadPoolTest : public QObject
{
	Q_OBJECT
private slots:
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
