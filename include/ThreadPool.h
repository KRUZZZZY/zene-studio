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
#include <memory>
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
	//! From any other thread this blocks until the queued tasks have run and all workers
	//! have finished. When the destructor runs on one of the pool's own workers (a task
	//! released the pool's last owner), it cannot join the other workers: a sibling may be
	//! waiting for the future of the task that is releasing the pool. It signals shutdown
	//! and detaches every worker instead. Each worker keeps the pool's shared state alive,
	//! runs the tasks already queued, and then exits on its own, so queued tasks are still
	//! executed and their futures do not become broken promises.
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
			const auto lock = std::unique_lock{m_state->runMutex};
			m_state->queue.push(std::move(task));
		}

		m_state->runCond.notify_one();
		return promise->get_future();
	}

	//! Return the number of worker threads used.
	auto numWorkers() const -> size_t;

	//! Return the global `ThreadPool` instance.
	static auto instance() -> ThreadPool&;

private:
	//! State shared with the workers, so a shutdown started from one of them outlives the
	//! `ThreadPool` object until the last worker has left `run()`.
	struct State
	{
		std::queue<std::packaged_task<void()>> queue;
		std::condition_variable runCond;
		std::mutex runMutex;
		std::atomic<bool> done = false;
	};

	//! The worker count a pool may run with. Hardware concurrency is allowed to report zero,
	//! and a pool without workers would never run its queued tasks.
	static auto workerCount(size_t hardwareConcurrency) -> size_t
	{
		return hardwareConcurrency == 0 ? size_t{1} : hardwareConcurrency;
	}
	ThreadPool(size_t numWorkers);
	//! Workers and the shutdown flag reference the shared state, so an accidental move would
	//! leave them pointing at the moved-from pool's state. The pool was never copyable or
	//! movable; keep it that way explicitly.
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	static void run(std::shared_ptr<State> state);
	std::shared_ptr<State> m_state;
	//! Shutdown flag, owned by the shared state so that workers keep using it after a
	//! worker-initiated destruction has released the rest of the object.
	std::atomic<bool>& m_done;
	std::vector<std::thread> m_workers;
	inline static size_t s_numWorkers = workerCount(std::thread::hardware_concurrency());
};
} // namespace lmms

#endif // LMMS_THREAD_POOL_H
