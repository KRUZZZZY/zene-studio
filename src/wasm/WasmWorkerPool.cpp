/*
 * WasmWorkerPool.cpp - the process-wide lane pool every WASM worker shares
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "WasmWorkerPool.h"

#include "WasmWorker.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace lmms::wasm
{

namespace
{

//! Cores this pool may use. One is always started, so a single-core box still
//! makes progress; the cap keeps a big machine from starting one lane per core
//! in a process that also runs the audio engine's own workers.
std::size_t computeLaneCount()
{
	const unsigned int cores = std::thread::hardware_concurrency();
	const std::size_t available = cores > 1 ? static_cast<std::size_t>(cores - 1) : 1;
	return std::min<std::size_t>(std::max<std::size_t>(available, 1), WasmWorkerPool::maxLanes);
}

} // namespace

WasmWorkerPool& WasmWorkerPool::instance()
{
	static WasmWorkerPool pool;
	return pool;
}

std::size_t WasmWorkerPool::laneCount()
{
	// Named once, so `wasm.pool` and the constructor cannot disagree.
	static const std::size_t lanes = computeLaneCount();
	return lanes;
}

WasmWorkerPool::WasmWorkerPool()
{
	const std::size_t lanes = laneCount();
	m_lanes.store(lanes, std::memory_order_relaxed);
	for (std::size_t i = 0; i < lanes; ++i)
	{
		m_threads[i] = std::thread([this, i] { laneMain(i); });
	}
}

WasmWorkerPool::~WasmWorkerPool()
{
	shutdown();
}

void WasmWorkerPool::shutdown()
{
	const bool wasStopped = m_stop.exchange(true, std::memory_order_acq_rel);
	if (wasStopped) { return; }
	// Wake every parked lane so it sees the stop flag rather than waiting for
	// work that will never come.
	m_generation.fetch_add(1, std::memory_order_release);
	m_generation.notify_all();
	for (std::thread& lane : m_threads)
	{
		if (lane.joinable()) { lane.join(); }
	}
}

bool WasmWorkerPool::addWorker(WasmWorker* worker)
{
	if (worker == nullptr) { return false; }
	const std::size_t count = m_workerCount.load(std::memory_order_relaxed);
	if (count >= maxWorkers) { return false; }
	for (std::size_t i = 0; i < maxWorkers; ++i)
	{
		if (m_workers[i].load(std::memory_order_relaxed) == nullptr)
		{
			m_workers[i].store(worker, std::memory_order_release);
			m_workerCount.store(i + 1, std::memory_order_release);
			// A lane must pick the worker up so it can load its module even
			// before the first block is submitted.
			notifyWork();
			return true;
		}
	}
	return false;
}

void WasmWorkerPool::removeWorker(WasmWorker* worker)
{
	if (worker == nullptr) { return; }
	for (std::size_t i = 0; i < maxWorkers; ++i)
	{
		if (m_workers[i].load(std::memory_order_relaxed) != worker) { continue; }
		m_workers[i].store(nullptr, std::memory_order_release);
		// Shrink the dense prefix so lanes stop scanning the far slots. Lanes
		// may read a stale count for one pass; that is harmless (a null slot is
		// skipped, and a worker is only reachable through its own claim flag).
		const std::size_t count = m_workerCount.load(std::memory_order_relaxed);
		if (i + 1 == count)
		{
			std::size_t shrunk = i;
			while (shrunk > 0 && m_workers[shrunk - 1].load(std::memory_order_relaxed) == nullptr)
			{
				--shrunk;
			}
			m_workerCount.store(shrunk, std::memory_order_release);
		}
		return;
	}
}

bool WasmWorkerPool::notifyWork()
{
	m_generation.fetch_add(1, std::memory_order_release);
	if (m_idleLanes.load(std::memory_order_acquire) == 0)
	{
		// Every lane is awake: nothing to wake, and the audio thread took no
		// syscall at all. This is the steady-state case.
		m_wakesSuppressed.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	m_generation.notify_one();
	m_wakeups.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool WasmWorkerPool::hasPendingWork() const
{
	const std::size_t count = m_workerCount.load(std::memory_order_acquire);
	for (std::size_t i = 0; i < count; ++i)
	{
		const WasmWorker* worker = m_workers[i].load(std::memory_order_acquire);
		if (worker != nullptr && worker->hasPendingWork()) { return true; }
	}
	return false;
}

bool WasmWorkerPool::drainOnce(std::size_t lane)
{
	bool didWork = false;
	const std::size_t count = m_workerCount.load(std::memory_order_acquire);
	for (std::size_t i = 0; i < count; ++i)
	{
		// Round robin from this lane's own offset, so two lanes tend to take
		// different workers and a busy worker cannot starve the rest.
		const std::size_t slot = (lane + i) % maxWorkers;
		WasmWorker* worker = m_workers[slot].load(std::memory_order_acquire);
		if (worker == nullptr) { continue; }
		if (!worker->claimForLane()) { continue; }
		const bool workerDidWork = worker->drainOnLane();
		if (workerDidWork)
		{
			didWork = true;
			m_blocks.fetch_add(1, std::memory_order_relaxed);
		}
		worker->releaseLane();
	}
	if (didWork) { m_lanePasses.fetch_add(1, std::memory_order_relaxed); }
	return didWork;
}

void WasmWorkerPool::laneMain(std::size_t lane)
{
	while (!m_stop.load(std::memory_order_acquire))
	{
		if (drainOnce(lane)) { continue; }

		// Nothing to do: park until the work generation moves. Reading the
		// generation BEFORE publishing this lane as idle is what makes the
		// hand-off lossless: a notifyWork() that lands after the scan either
		// finds this lane idle (and wakes it) or has already bumped the
		// generation (and the wait below returns immediately).
		const std::uint64_t generation = m_generation.load(std::memory_order_acquire);
		m_idleLanes.fetch_add(1, std::memory_order_release);
		if (!hasPendingWork() && !m_stop.load(std::memory_order_acquire))
		{
			m_parks.fetch_add(1, std::memory_order_relaxed);
			m_generation.wait(generation, std::memory_order_acquire);
		}
		m_idleLanes.fetch_sub(1, std::memory_order_release);
	}
}

WasmWorkerPool::Stats WasmWorkerPool::stats() const
{
	Stats out;
	out.lanes = m_lanes.load(std::memory_order_relaxed);
	out.workers = m_workerCount.load(std::memory_order_relaxed);
	out.lanePasses = m_lanePasses.load(std::memory_order_relaxed);
	out.blocks = m_blocks.load(std::memory_order_relaxed);
	out.wakeups = m_wakeups.load(std::memory_order_relaxed);
	out.wakesSuppressed = m_wakesSuppressed.load(std::memory_order_relaxed);
	out.parks = m_parks.load(std::memory_order_relaxed);
	return out;
}

} // namespace lmms::wasm
