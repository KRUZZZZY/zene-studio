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
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */
#include "ThreadPool.h"

#include <cassert>
#include <optional>
#include <utility>

namespace lmms {
ThreadPool::ThreadPool(size_t numWorkers)
	: m_state{std::make_shared<State>()}
	, m_done{m_state->done}
{
	numWorkers = workerCount(numWorkers);
	assert(numWorkers > 0);

	m_workers.reserve(numWorkers);
	try
	{
		for (size_t i = 0; i < numWorkers; ++i)
		{
			m_workers.emplace_back([state = m_state] { run(state); });
		}
	}
	catch (...)
	{
		// Workers already started capture the shared state; stop and join them before it dies.
		{
			const auto lock = std::unique_lock{m_state->runMutex};
			m_state->done = true;
		}

		m_state->runCond.notify_all();

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
	auto selfIsWorker = false;
	for (const auto& worker : m_workers)
	{
		if (worker.joinable() && worker.get_id() == self)
		{
			selfIsWorker = true;
			break;
		}
	}

	{
		const auto lock = std::unique_lock{m_state->runMutex};
		m_state->done = true;
	}

	m_state->runCond.notify_all();

	if (!selfIsWorker)
	{
		for (auto& worker : m_workers)
		{
			if (worker.joinable()) { worker.join(); }
		}
		return;
	}

	// A task released the pool's last owner, so this destructor runs on one of the pool's
	// own workers. Joining that thread throws from a noexcept destructor, and joining a
	// sibling can deadlock: the sibling may be waiting for the future of the very task this
	// destructor is running inside, and that task cannot return before we do. Detach every
	// worker instead; each one keeps the shared state alive, drains what is still queued,
	// and exits on its own.
	for (auto& worker : m_workers)
	{
		if (worker.joinable()) { worker.detach(); }
	}
}

auto ThreadPool::numWorkers() const -> size_t
{
	return m_workers.size();
}

void ThreadPool::run(std::shared_ptr<State> state)
{
	while (true)
	{
		std::optional<std::packaged_task<void()>> task;
		{
			auto lock = std::unique_lock{state->runMutex};
			state->runCond.wait(lock, [&state] { return !state->queue.empty() || state->done; });

			if (state->queue.empty()) { break; }
			task.emplace(std::move(state->queue.front()));
			state->queue.pop();
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
