/*
 * WasmWorkerPool.h - the process-wide lane pool every WASM worker shares
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

#ifndef LMMS_WASM_WORKER_POOL_H
#define LMMS_WASM_WORKER_POOL_H

#include "lmms_export.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace lmms::wasm
{

class WasmWorker;

/*! One pool of lane threads, shared by every WasmWorker in the process.
 *
 * WHAT THIS REPLACES. WasmWorker used to own a std::thread and a
 * `sleep_for(200us)` polling loop: N wasm effects cost N threads, and a queued
 * block waited for the next poll tick. This pool starts a BOUNDED number of
 * lanes (WasmWorkerPool::laneCount()) for the whole process, hands them the
 * workers' queues, and parks a lane on a generation counter when there is
 * nothing to do - so a queued block is picked up by a wake-up, not a timer.
 *
 * THREADING CONTRACT.
 *  - The audio thread calls notifyWork() (through WasmWorker::submit()) and
 *    nothing else here: one relaxed increment of the work generation and one
 *    acquire load. It takes no lock, allocates nothing and does NOT notify when
 *    every lane is awake - `wakes_suppressed` counts that case. When the pool
 *    is idle it is one `std::atomic::notify_one()`, i.e. a single futex wake.
 *  - A lane claims ONE worker at a time (WasmWorker::claimForLane()), drains
 *    that worker's queue in FIFO order and releases it. In-order draining per
 *    worker is what keeps a stateful module's blocks in the order they were
 *    submitted; two workers may be in flight at once, two blocks of the same
 *    worker never are.
 *  - Registration is a control-thread operation (start()/stop()).
 */
class LMMS_EXPORT WasmWorkerPool
{
public:
	//! Bounds, so the pool cannot grow with the project.
	static constexpr std::size_t maxWorkers = 64;
	static constexpr std::size_t maxLanes = 8;

	//! The process's one pool. Started on first use.
	static WasmWorkerPool& instance();

	WasmWorkerPool(const WasmWorkerPool&) = delete;
	WasmWorkerPool& operator=(const WasmWorkerPool&) = delete;

	/*! How many lanes this process runs: bounded by maxLanes and by the
	 *  hardware (cores - 1, at least one), so N wasm effects cost this many
	 *  threads rather than N.
	 */
	static std::size_t laneCount();

	//! Control thread. False when the pool already holds maxWorkers workers.
	bool addWorker(WasmWorker* worker);
	//! Control thread. Idempotent.
	void removeWorker(WasmWorker* worker);

	/*! Audio thread: a block was queued. Bumps the work generation and wakes a
	 *  lane IF one is parked. Returns true when a wake-up was signalled.
	 *  Lock free, allocation free; the steady-state case (a lane already awake)
	 *  is one relaxed add and one acquire load.
	 */
	bool notifyWork();

	/*! The pool's own counters: what it did, and what the audio path cost.
	 *  `wakes_suppressed` is the number of notifyWork() calls that needed no
	 *  wake-up; `park`s is how often a lane went to sleep; `blocks` is the
	 *  number of queued blocks the lanes processed.
	 */
	struct Stats
	{
		std::uint64_t lanes = 0;
		std::uint64_t workers = 0;
		std::uint64_t lanePasses = 0;
		std::uint64_t blocks = 0;
		std::uint64_t wakeups = 0;
		std::uint64_t wakesSuppressed = 0;
		std::uint64_t parks = 0;
	};
	Stats stats() const;

	//! Stop and join every lane. Idempotent; also runs from the destructor.
	void shutdown();

	/*! One lane's pass over the registered workers: claim, drain, release.
	 *  Returns true when any worker did work. Public for the pool's own use and
	 *  for a test that drives a lane by hand; never call it from two threads
	 *  with the same \p lane index.
	 */
	bool drainOnce(std::size_t lane);

private:
	WasmWorkerPool();
	~WasmWorkerPool();

	void laneMain(std::size_t lane);
	bool hasPendingWork() const;

	std::thread m_threads[maxLanes];
	std::atomic<bool> m_stop{false};

	//! Dense array of registered workers; a slot is published with a release
	//! store of m_workerCount after the pointer is written.
	std::atomic<WasmWorker*> m_workers[maxWorkers]{};
	std::atomic<std::size_t> m_workerCount{0};

	//! The work generation. A lane wakes when this changes, so a wake-up that
	//! lands between its scan and its park cannot be lost.
	std::atomic<std::uint64_t> m_generation{0};
	std::atomic<std::uint32_t> m_idleLanes{0};
	std::atomic<std::uint64_t> m_lanes{0};
	std::atomic<std::uint64_t> m_lanePasses{0};
	std::atomic<std::uint64_t> m_blocks{0};
	std::atomic<std::uint64_t> m_wakeups{0};
	std::atomic<std::uint64_t> m_wakesSuppressed{0};
	std::atomic<std::uint64_t> m_parks{0};
};

} // namespace lmms::wasm

#endif // LMMS_WASM_WORKER_POOL_H
