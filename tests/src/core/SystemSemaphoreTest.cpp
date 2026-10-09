/*
 * SystemSemaphoreTest.cpp - the timed acquire of the system semaphore waits no longer than its timeout
 *
 * The shared-memory transport's audio-thread waits use tryAcquire(); this checks it on the platform
 * backend compiled here (POSIX on Linux and macOS, Win32 on Windows, which is not run from this tree).
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

#include <QtTest>

#include <chrono>
#include <string>

#include "SystemSemaphore.h"

class SystemSemaphoreTest : public QObject
{
	Q_OBJECT

private slots:
	void tryAcquireTimesOutWithoutCount()
	{
		const auto key = std::string{"zene-sem-test-empty-"} + std::to_string(QCoreApplication::applicationPid());
		lmms::SystemSemaphore semaphore{key, 0u};

		const auto started = std::chrono::steady_clock::now();
		QVERIFY(!semaphore.tryAcquire(30));
		const auto elapsed = std::chrono::steady_clock::now() - started;
		QVERIFY2(elapsed >= std::chrono::milliseconds{20}, "tryAcquire returned before its timeout");
		QVERIFY2(elapsed < std::chrono::seconds{2}, "tryAcquire waited far beyond its timeout");
	}

	void tryAcquireTakesACountAtOnce()
	{
		const auto key = std::string{"zene-sem-test-count-"} + std::to_string(QCoreApplication::applicationPid());
		lmms::SystemSemaphore semaphore{key, 0u};

		QVERIFY(semaphore.release());
		const auto started = std::chrono::steady_clock::now();
		QVERIFY(semaphore.tryAcquire(0));
		QVERIFY(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
		QVERIFY(!semaphore.tryAcquire(0));
	}
};

QTEST_APPLESS_MAIN(SystemSemaphoreTest)

#include "SystemSemaphoreTest.moc"
