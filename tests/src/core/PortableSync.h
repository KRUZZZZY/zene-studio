/*
 * PortableSync.h - test synchronisation that compiles below macOS 11
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef ZENE_TESTS_PORTABLE_SYNC_H
#define ZENE_TESTS_PORTABLE_SYNC_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

// Apple's libc++ marks <barrier>, <latch> and <semaphore> unavailable below macOS 11
// (_LIBCPP_AVAILABILITY_SYNC), and this project supports macOS 10.13. Tests used those
// headers and the hosted macos-x86_64 job stopped at
//
//     tests/src/core/RemotePluginShmFifoTest.cpp:109:16: error: 'barrier' is unavailable:
//     introduced in macOS 11.0 unknown
//
// cluster ci-61ad26e4f8b135f9. Raising each test target's deployment floor hides the
// product's own floor rather than fixing anything, and the primitives have the same
// fixed-count rendezvous, latch and permit semantics expressed here with a mutex and a
// condition variable: available on every target this project builds for (and macOS since
// before 10.13). These are test-only rendezvous points - no render thread ever touches
// them, so the condition variable's cost is irrelevant. A product wake that must not
// lock or allocate has its own primitive: lmms::workerwake (include/AudioWorkerWake.h).
//
// The names and call shapes deliberately mirror the standard types they replace
// (arrive_and_wait, count_down/wait, release/acquire/try_acquire_for) so a test reads the
// same way and the replacement stays mechanical. tests/macos-sync-availability-check.py
// keeps the standard, gated spellings out of the tree.

namespace lmms::testsupport
{

//! A fixed-count rendezvous: a thread's arrive_and_wait() returns only after count threads
//! have arrived. Same contract as std::barrier<> with no completion function.
class Barrier
{
public:
	explicit Barrier(std::ptrdiff_t count) : m_count{count} {}

	void arrive_and_wait()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		const std::uint64_t generation = m_generation;
		++m_waiting;
		if (m_waiting == m_count)
		{
			m_waiting = 0;
			++m_generation;
			m_cv.notify_all();
			return;
		}
		// A predicate, not a bare wait: a spurious wake must not release a thread before
		// the whole count has arrived.
		m_cv.wait(lock, [this, generation] { return m_generation != generation; });
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::ptrdiff_t m_count;
	std::ptrdiff_t m_waiting{0};
	std::uint64_t m_generation{0};
};

//! A one-shot gate: wait() returns once count_down() has been called \a count times.
//! Same contract as std::latch.
class Latch
{
public:
	explicit Latch(std::ptrdiff_t count) : m_count{count} {}

	void count_down()
	{
		const std::lock_guard<std::mutex> lock{m_mutex};
		if (--m_count <= 0)
		{
			m_cv.notify_all();
		}
	}

	void wait()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		m_cv.wait(lock, [this] { return m_count <= 0; });
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::ptrdiff_t m_count;
};

//! A binary permit: release() grants one, acquire() consumes one. Same contract as
//! std::binary_semaphore, including the timed acquisition the audio-view regressions use
//! to decide "the writer did reach the model" instead of sleeping.
class BinarySemaphore
{
public:
	//! Same constructor as std::binary_semaphore: a nonzero initial count is one permit.
	explicit BinarySemaphore(std::ptrdiff_t initial = 0) : m_available{initial > 0} {}

	void release()
	{
		{
			const std::lock_guard<std::mutex> lock{m_mutex};
			m_available = true;
		}
		m_cv.notify_one();
	}

	void acquire()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		m_cv.wait(lock, [this] { return m_available; });
		m_available = false;
	}

	template <typename Rep, typename Period>
	bool try_acquire_for(const std::chrono::duration<Rep, Period>& timeout)
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		if (!m_cv.wait_for(lock, timeout, [this] { return m_available; })) { return false; }
		m_available = false;
		return true;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_available{false};
};

} // namespace lmms::testsupport

#endif // ZENE_TESTS_PORTABLE_SYNC_H
