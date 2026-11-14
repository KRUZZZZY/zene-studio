/*
 * ControlShutdownHookTest.cpp - CODE-8: a shutdown hook must survive its owner,
 *                               and must not outlive it.
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
 *
 * THE CONTRACT UNDER TEST, in two halves that pull in opposite directions and
 * are both load-bearing:
 *
 *   it must SURVIVE   a hook registered while one registry instance existed
 *                     must still run if the shutdown path reaches a registry
 *                     that was re-created in the meantime. The registry is a
 *                     singleton that main() can destroy, and ControlSession's
 *                     last-resort guard calls
 *                     `ControlRegistry::instance()->runShutdownHooks()` on the
 *                     way out - an instance() call that BUILDS a new, empty
 *                     registry if the old one is gone. Hooks held in a member
 *                     died with the first instance, so the guard ran nothing and
 *                     the control socket was left behind on exactly the route
 *                     the hook exists for.
 *
 *   it must NOT OUTLIVE its owner
 *                     the socket hook is `[this]() { close(); }` - a raw
 *                     pointer into the ControlServer. A server destroyed before
 *                     the shutdown path ran left a call on freed memory behind.
 *                     Un-registering on destruction is what makes the hook's
 *                     lifetime the owner's lifetime; the socket file is still
 *                     removed, by close() in the same destructor.
 */

#include "ControlRegistry.h"

// The monotonic-deadline case reads the accepted client's stored deadline, which
// is private state. The same `#define private public` seam the collab invite
// tests use, undone right after this header.
#define private public
#include "ControlServer.h"
#undef private

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>

#include <string>
#endif

#include <chrono>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

using namespace lmms;

namespace
{

struct ReadyStateReset
{
	~ReadyStateReset() { ControlRegistry::setReady(false); }
};

#if defined(Q_OS_UNIX)
struct TestSocket
{
	int fd = -1;
	~TestSocket() { if (fd >= 0) { ::close(fd); } }
};

struct ReplyDrain
{
	qint64 bytes = 0;
	bool closed = false;
	bool failed = false;
};

ReplyDrain drainReplyUntilClosedOrComplete(int fd, qint64 expectedBytes)
{
	ReplyDrain result;
	QElapsedTimer timeout;
	timeout.start();
	while (!result.closed && result.bytes < expectedBytes && timeout.elapsed() < 10000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
		char buffer[64 * 1024];
		const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
		if (count > 0) { result.bytes += count; }
		else if (count == 0) { result.closed = true; }
		else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
		{
			result.failed = true;
		}
	}
	return result;
}
#endif

#if defined(Q_OS_WIN)
struct TestPipe
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	TestPipe() = default;
	explicit TestPipe(HANDLE value) : handle(value) {}
	TestPipe(const TestPipe&) = delete;
	TestPipe& operator=(const TestPipe&) = delete;
	TestPipe(TestPipe&& other) noexcept : handle(other.handle) { other.handle = INVALID_HANDLE_VALUE; }
	TestPipe& operator=(TestPipe&& other) noexcept
	{
		close();
		handle = other.handle;
		other.handle = INVALID_HANDLE_VALUE;
		return *this;
	}
	~TestPipe() { close(); }
	bool isValid() const { return handle != INVALID_HANDLE_VALUE; }
	void close()
	{
		if (isValid()) { CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
	}
};

TestPipe connectTestPipe(const QString& name, int timeoutMs)
{
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < timeoutMs)
	{
		HANDLE pipe = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()),
			GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) { return TestPipe(pipe); }
		const DWORD error = GetLastError();
		if (error == ERROR_PIPE_BUSY) { WaitNamedPipeW(reinterpret_cast<LPCWSTR>(name.utf16()), 50); }
		else if (error == ERROR_FILE_NOT_FOUND) { Sleep(10); }
		else { break; }
	}
	return TestPipe();
}

enum class PipeExchange { Replied, Closed, TimedOut, Failed };

bool isPipeClosedError(DWORD error)
{
	return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
}

PipeExchange pipeError(DWORD error)
{
	return isPipeClosedError(error) ? PipeExchange::Closed : PipeExchange::Failed;
}

PipeExchange readPipeResponse(TestPipe& pipe, QByteArray* response)
{
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 5000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
		DWORD available = 0;
		if (!PeekNamedPipe(pipe.handle, nullptr, 0, nullptr, &available, nullptr))
		{
			return pipeError(GetLastError());
		}
		if (available == 0)
		{
			Sleep(1);
			continue;
		}
		char bytes[4096];
		DWORD count = 0;
		if (!ReadFile(pipe.handle, bytes, sizeof(bytes), &count, nullptr)) { return pipeError(GetLastError()); }
		response->append(bytes, static_cast<int>(count));
		if (response->contains('\n')) { return PipeExchange::Replied; }
	}
	return PipeExchange::TimedOut;
}

PipeExchange sendMalformedRequest(TestPipe& pipe, QByteArray* response)
{
	const QByteArray request = QByteArrayLiteral("{\n");
	DWORD written = 0;
	if (!WriteFile(pipe.handle, request.constData(), static_cast<DWORD>(request.size()), &written, nullptr))
	{
		return pipeError(GetLastError());
	}
	if (written != static_cast<DWORD>(request.size())) { return PipeExchange::Failed; }
	return readPipeResponse(pipe, response);
}
#endif

#if defined(Q_OS_WIN)
struct TestWinHandle
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	~TestWinHandle()
	{
		if (handle != nullptr && handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); }
	}
};

HANDLE connectToNamedPipe(const QString& path)
{
	const std::wstring nativePath = path.toStdWString();
	QElapsedTimer timeout;
	timeout.start();
	while (timeout.elapsed() < 5000)
	{
		const HANDLE pipe = CreateFileW(nativePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
			nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) { return pipe; }
		const DWORD error = GetLastError();
		if (error == ERROR_PIPE_BUSY) { WaitNamedPipeW(nativePath.c_str(), 100); }
		else if (error == ERROR_FILE_NOT_FOUND) { Sleep(10); }
		else { return INVALID_HANDLE_VALUE; }
	}
	return INVALID_HANDLE_VALUE;
}

bool writeIncompletePipeByte(HANDLE pipe, HANDLE event, DWORD* error)
{
	const char byte = '{';
	OVERLAPPED operation{};
	ResetEvent(event);
	operation.hEvent = event;
	DWORD written = 0;
	if (!WriteFile(pipe, &byte, 1, nullptr, &operation))
	{
		*error = GetLastError();
		if (*error != ERROR_IO_PENDING) { return false; }
		if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
		{
			*error = ERROR_TIMEOUT;
			return false;
		}
	}
	if (GetOverlappedResult(pipe, &operation, &written, FALSE)) { return written == 1; }
	*error = GetLastError();
	return false;
}

bool isNamedPipeDisconnect(DWORD error)
{
	return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
}

#endif

} // namespace

class ControlShutdownHookTest : public QObject
{
	Q_OBJECT
private slots:

	void init();

	void windowsCapsActivePipeClientsAndReusesFreedSlot();

	void everyNonblankRequestEmitsItsExchange();

#if defined(Q_OS_WIN)
	void anIdleNamedPipePeerIsDisconnected();

	void aTrickledIncompleteNamedPipeRequestDoesNotExtendTheDeadline();
#endif

	//! The first half: the hook survives a registry instance it was not
	//! registered on. destroy() then instance() is exactly the sequence that
	//! used to lose it.
	void aHookSurvivesTheRegistryInstanceItWasRegisteredOn();

	//! A live server's hook survives registry destruction and closes the socket
	//! when the recreated registry runs it. Removal must not use the server's
	//! stale pointer to the registry that was destroyed.
	void aLiveServerHookRunsThroughARecreatedRegistry();

	//! Removal acts on process-lifetime state and must be callable without a
	//! ControlRegistry instance that may already have been destroyed.
	void shutdownHookRemovalDoesNotRequireARegistryInstance();

	//! The second half: the owner's death takes the hook with it, and the socket
	//! file is gone either way (close() removes it in the same destructor).
	void theOwnerTakesItsHookWithItWhenItDies();

	//! A single oversized reply must not leave a tail larger than the queue cap.
	void aSingleOversizedReplyDoesNotExceedTheQueueCap();

	//! A reply may exceed the cap when its first write leaves a bounded tail.
	void aLargeReplyWithBoundedTailIsSentCompletely();

	//! Re-listening must not orphan a hook: close() stops THIS listener, so the
	//! hook that closes THIS listener's socket goes with it. A later listen()
	//! registers a fresh hook and overwrites m_shutdownHookId; if close() had
	//! left the old hook registered, the destructor (which removes only the
	//! newest id) would leave the orphan behind - still capturing `this` - and
	//! the forced-exit shutdown path would call close() on freed memory.
	void aClosedListenerTakesItsHookWithItBeforeTheNextListen();

	//! Removal is targeted, ids are never reused, and running twice runs once:
	//! the store is the process's, so a second pass must not re-run what the
	//! first already did.
	void removalIsTargetedAndTheStoreIsDrainedOnce();

	//! The socket path the hook exists to remove: with the server alive, the
	//! shutdown path (not the destructor) is what unlinks it. This is the
	//! behaviour main.cpp's normal exit and the last-resort guard both rely on.
	void theShutdownPathUnlinksALiveServersSocket();

	//! BUG 8: a POSIX client's idle deadline is anchored to the MONOTONIC clock,
	//! not the adjustable wall clock. `QDateTime::currentMSecsSinceEpoch()` moves
	//! when the system clock is stepped (NTP, a user change), so a forward step
	//! dropped an active client early and a backward step let an idle one outlive
	//! its timeout. The deadline a fresh connection stores must be measured from
	//! steady_clock: the values the two clocks report are days apart, so the
	//! elapsed-from-steady assertion below is deterministic and fails on the
	//! wall-clock behaviour.
	void aClientDeadlineIsAnchoredToTheMonotonicClock();
};

#include "ControlShutdownHookTestParts1.h"
#include "ControlShutdownHookTestParts2.h"

QTEST_GUILESS_MAIN(ControlShutdownHookTest)
#include "ControlShutdownHookTest.moc"
