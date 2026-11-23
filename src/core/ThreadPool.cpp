/*
 * ThreadPool.cpp
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
#include "ThreadPool.h"

#include <cassert>
#include <optional>

namespace lmms {
namespace {
// Set on a worker thread whose pool is destroyed from inside one of that pool's tasks.
// That worker cannot be joined; it is detached and leaves run() after the current task,
// before touching members the destructor has already released.
thread_local bool s_poolDestroyedOnThisWorker = false;
} // namespace

ThreadPool::ThreadPool(size_t numWorkers)
{
	numWorkers = workerCount(numWorkers);
	assert(numWorkers > 0);

	m_workers.reserve(numWorkers);
	try
	{
		for (size_t i = 0; i < numWorkers; ++i)
		{
			m_workers.emplace_back([this] { run(); });
		}
	}
	catch (...)
	{
		// Workers already started capture `this`; stop and join them before the members die.
		{
			const auto lock = std::unique_lock{m_runMutex};
			m_done = true;
		}

		m_runCond.notify_all();

		for (auto& worker : m_workers)
		{
			if (worker.joinable()) { worker.join(); }
		}

		throw;
	}
}

ThreadPool::~ThreadPool()
{
	const auto self = std::this_thread::get_id();
	{
		const auto lock = std::unique_lock{m_runMutex};
		m_done = true;
	}

	m_runCond.notify_all();

	for (auto& worker : m_workers)
	{
		if (!worker.joinable()) { continue; }

		if (worker.get_id() == self)
		{
			// A task released the last owner of this pool, so the destructor runs on that
			// worker. Joining the calling thread throws from a noexcept destructor; let
			// this worker exit after its task instead of joining it.
			s_poolDestroyedOnThisWorker = true;
			worker.detach();
			continue;
		}

		worker.join();
	}
}

auto ThreadPool::numWorkers() const -> size_t
{
	return m_workers.size();
}

void ThreadPool::run()
{
	// Leave without touching any member when the destructor ran on this worker: the pool
	// is gone except for the task still running on this stack.
	while (!s_poolDestroyedOnThisWorker)
	{
		std::optional<std::packaged_task<void()>> task;
		{
			auto lock = std::unique_lock{m_runMutex};
			m_runCond.wait(lock, [this] { return !m_queue.empty() || m_done; });

			if (m_queue.empty()) { break; }
			task.emplace(std::move(m_queue.front()));
			m_queue.pop();
		}
		(*task)();
	}
}

auto ThreadPool::instance() -> ThreadPool&
{
	static auto s_pool = ThreadPool{s_numWorkers};
	return s_pool;
}

} // namespace lmms
