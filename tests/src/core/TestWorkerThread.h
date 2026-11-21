/*
 * TestWorkerThread.h - a QThread that runs a callable, without QThread::create()
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef LMMS_TESTS_TEST_WORKER_THREAD_H
#define LMMS_TESTS_TEST_WORKER_THREAD_H

#include <QThread>

#include <functional>
#include <utility>

namespace lmms::test
{

/*! Runs @a body on a QThread, the way `QThread::create(body)` would.
 *
 * `QThread::create()` is the portable way to start a function on a thread - except where Qt
 * was configured without `cxx11_future`: `qthread.h` wraps the whole factory in
 * `#if QT_CONFIG(cxx11_future)`, and the hosted mingw64 job's Qt 5.15.6 package carries
 * `QT_FEATURE_cxx11_future -1`, so the name does not exist there (cluster
 * ci-539b88713935565b: `error: 'create' is not a member of 'QThread'`). This subclass is the
 * same thing without std::async: `run()` calls the body once, and the caller keeps using
 * `start()` and `wait()` exactly as before. Test code only - a product thread has its own
 * class with its own shutdown rules.
 */
class TestWorkerThread : public QThread
{
public:
	explicit TestWorkerThread(std::function<void()> body)
		: m_body(std::move(body))
	{
	}

protected:
	void run() override
	{
		if (m_body) { m_body(); }
	}

private:
	std::function<void()> m_body;
};

} // namespace lmms::test

#endif // LMMS_TESTS_TEST_WORKER_THREAD_H
