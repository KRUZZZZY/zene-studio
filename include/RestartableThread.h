/*
 * RestartableThread.h - a thread this object owns, whose body may finish on its
 *                       own and be started again.
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

#ifndef LMMS_RESTARTABLE_THREAD_H
#define LMMS_RESTARTABLE_THREAD_H

#include <functional>
#include <thread>
#include <utility>

namespace lmms
{

//! One thread this object owns, which may finish by itself and may be started
//! again (BUG 15, cluster 3118).
//!
//! std::thread's copy assignment is deleted and its move assignment calls
//! std::terminate() when the target is still joinable - a process abort, not an
//! exception, so no caller can recover from it. A body that returns on its own
//! leaves its std::thread joinable until someone joins it, so "a worker that
//! finishes by itself" and "assign the next worker to the same std::thread" do
//! not combine.
//!
//! The Windows control transport has exactly that shape: its accept loop breaks
//! out when CreateNamedPipeW fails, clears m_win32Listening as it goes, and the
//! server still owns the joinable thread. The retried listen() used to assign the
//! new acceptor over it, so a pipe that could not be created plus one retry
//! killed the instance. Everything needed to replace the thread lives here:
//! start() joins the body this object ran last before it starts another one, and
//! join() (called by close() and by the destructor) is idempotent.
//!
//! start() waits for a body that is still running rather than abandoning it, so
//! a caller must start() only once the previous body has returned - which the
//! transport knows, because that is the same event that cleared its listening
//! flag. Nothing here is on the audio path: start() allocates the std::function
//! and join() waits.
class RestartableThread
{
public:
	RestartableThread() = default;
	~RestartableThread() { join(); }
	RestartableThread(const RestartableThread&) = delete;
	RestartableThread& operator=(const RestartableThread&) = delete;

	//! Join the body this object started last (if any), then run \p body on a
	//! new thread. A joinable thread is never assigned over.
	void start(std::function<void()> body)
	{
		join();
		m_thread = std::thread(std::move(body));
	}

	//! Wait for the body this object started last, if there is one. Idempotent,
	//! and a no-op before the first start().
	void join()
	{
		if (m_thread.joinable()) { m_thread.join(); }
	}

	//! True while this object holds a thread that has not been joined yet. A body
	//! that returned is still joinable: that is the state start() retires.
	bool joinable() const { return m_thread.joinable(); }

private:
	std::thread m_thread;
};

} // namespace lmms

#endif // LMMS_RESTARTABLE_THREAD_H
