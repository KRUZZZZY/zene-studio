/*
 * ThreadPool.h
 *
 * Copyright (c) 2024 saker
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

#ifndef LMMS_THREAD_POOL_H
#define LMMS_THREAD_POOL_H

#include <atomic>
#include <functional>
#include <queue>
#include <tuple>
#include <type_traits>
#include <vector>

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

namespace lmms {
//! A thread pool that can be used for asynchronous processing.
class ThreadPool
{
public:
	//! Destroys the `ThreadPool` object.
	//! This blocks until all workers have finished executing.
	~ThreadPool();

	//! Enqueue function `fn` with arguments `args` to be ran asynchronously.
	template <typename Fn, typename... Args>
	auto enqueue(Fn&& fn, Args&&... args)
	{
		using StoredCallable = std::decay_t<Fn>;
		using CallableReference = std::conditional_t<std::is_lvalue_reference_v<Fn>, StoredCallable&, StoredCallable&&>;
		using ArgumentTuple = decltype(std::make_tuple(std::declval<Args>()...));
		using ReturnType = decltype(std::apply(std::declval<CallableReference>(), std::declval<ArgumentTuple&&>()));

		auto promise = std::make_shared<std::promise<ReturnType>>();
		auto task = std::packaged_task<void()>{[promise, fn = std::forward<Fn>(fn),
			args = std::make_tuple(std::forward<Args>(args)...)]() mutable
		{
			auto invoke = [&]() -> ReturnType
			{
				if constexpr (std::is_lvalue_reference_v<Fn>)
				{
					return std::apply(fn, std::move(args));
				}
				else
				{
					return std::apply(std::move(fn), std::move(args));
				}
			};

			try
			{
				if constexpr (std::is_same_v<ReturnType, void>)
				{
					invoke();
					promise->set_value();
				}
				else
				{
					promise->set_value(invoke());
				}
			}
			catch (...)
			{
				promise->set_exception(std::current_exception());
			}
		}};

		{
			const auto lock = std::unique_lock{m_runMutex};
			m_queue.push(std::move(task));
		}

		m_runCond.notify_one();
		return promise->get_future();
	}

	//! Return the number of worker threads used.
	auto numWorkers() const -> size_t;

	//! Return the global `ThreadPool` instance.
	static auto instance() -> ThreadPool&;

private:
	//! The worker count a pool may run with. Hardware concurrency is allowed to report zero,
	//! and a pool without workers would never run its queued tasks.
	static auto workerCount(size_t hardwareConcurrency) -> size_t
	{
		return hardwareConcurrency == 0 ? size_t{1} : hardwareConcurrency;
	}
#ifdef LMMS_TESTING // white-box tests reach this through `#define private public`; MSVC mangles the access
public:            // level into the symbol, so it must match what lmmsobjs (built with LMMS_TESTING) defines
#endif
	ThreadPool(size_t numWorkers);
#ifdef LMMS_TESTING
private:
#endif
	void run();
	std::vector<std::thread> m_workers;
	std::queue<std::packaged_task<void()>> m_queue;
	std::atomic<bool> m_done = false;
	std::condition_variable m_runCond;
	std::mutex m_runMutex;
	inline static size_t s_numWorkers = workerCount(std::thread::hardware_concurrency());
};
} // namespace lmms

#endif // LMMS_THREAD_POOL_H
