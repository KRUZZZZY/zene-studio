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

	void init()
	{
		// Each case starts from an empty hook store. Draining is safe: the hooks
		// this test registers are counters it owns, and a case that failed
		// before its own runShutdownHooks() must not leak into the next one.
		ControlRegistry* registry = ControlRegistry::instance();
		registry->runShutdownHooks();
		QCOMPARE(registry->shutdownHookCount(), 0);
	}

	void windowsCapsActivePipeClientsAndReusesFreedSlot()
	{
#if !defined(Q_OS_WIN)
		QSKIP("the named-pipe client limit is enforced by the Windows transport");
#else
		ControlServer server(ControlRegistry::instance());
		const QString pipeName = QStringLiteral("\\\\.\\pipe\\zene-client-cap-%1-%2")
			.arg(static_cast<qulonglong>(GetCurrentProcessId()))
			.arg(static_cast<qulonglong>(GetTickCount64()));
		QString error;
		QVERIFY2(server.listen(pipeName, &error), qPrintable(error));

		std::vector<TestPipe> clients;
		clients.reserve(ControlServer::MaxClients);
		for (int index = 0; index < ControlServer::MaxClients; ++index)
		{
			TestPipe client = connectTestPipe(pipeName, 5000);
			QVERIFY2(client.isValid(), "could not connect one of the 64 allowed pipe clients");
			clients.push_back(std::move(client));
		}

		TestPipe excess = connectTestPipe(pipeName, 5000);
		QVERIFY2(excess.isValid(), "the listener stopped while refusing an excess pipe client");
		QByteArray response;
		QVERIFY2(sendMalformedRequest(excess, &response) == PipeExchange::Closed,
			"an excess pipe client received a response instead of being disconnected");

		clients.front().close();
		QElapsedTimer reuseTimeout;
		reuseTimeout.start();
		bool reused = false;
		while (!reused && reuseTimeout.elapsed() < 5000)
		{
			TestPipe replacement = connectTestPipe(pipeName, 1000);
			if (!replacement.isValid()) { QTest::qWait(10); continue; }
			response.clear();
			const PipeExchange exchange = sendMalformedRequest(replacement, &response);
			if (exchange == PipeExchange::Replied)
			{
				clients.push_back(std::move(replacement));
				reused = true;
			}
			else if (exchange == PipeExchange::Failed || exchange == PipeExchange::TimedOut)
			{
				QFAIL("the freed client slot produced neither a reply nor a closed pipe");
			}
			QTest::qWait(10);
		}
		QVERIFY2(reused, "a freed pipe-client slot was not reusable");
#endif
	}

	void everyNonblankRequestEmitsItsExchange()
	{
		ControlServer server(ControlRegistry::instance());
		QSignalSpy exchanged(&server, &ControlServer::exchanged);
		QVERIFY(exchanged.isValid());

		const QByteArray malformedRequest = QByteArrayLiteral("{");
		const QByteArray malformedResponse = server.dispatchLine(malformedRequest);
		QCOMPARE(exchanged.size(), 1);
		QCOMPARE(exchanged.at(0).at(0).toByteArray(), malformedRequest);
		QCOMPARE(exchanged.at(0).at(1).toByteArray(), malformedResponse);

		const QByteArray missingIdRequest = QByteArrayLiteral("{\"cmd\":\"mixer.get_state\"}");
		const QByteArray missingIdResponse = server.dispatchLine(missingIdRequest);
		QCOMPARE(exchanged.size(), 2);
		QCOMPARE(exchanged.at(1).at(0).toByteArray(), missingIdRequest);
		QCOMPARE(exchanged.at(1).at(1).toByteArray(), missingIdResponse);
	}

#if defined(Q_OS_WIN)
	void anIdleNamedPipePeerIsDisconnected()
	{
		ControlServer server(ControlRegistry::instance());
		const QString path = QStringLiteral("\\\\.\\pipe\\zene-control-idle-%1-%2")
			.arg(GetCurrentProcessId()).arg(GetTickCount64());
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));

		TestWinHandle pipe;
		pipe.handle = connectToNamedPipe(path);
		QVERIFY2(pipe.handle != INVALID_HANDLE_VALUE, "could not connect to the test named pipe");
		QElapsedTimer idleWait;
		idleWait.start();
		TestWinHandle event;
		event.handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		QVERIFY(event.handle != nullptr);

		char byte = 0;
		DWORD bytesRead = 0;
		OVERLAPPED operation{};
		operation.hEvent = event.handle;
		if (ReadFile(pipe.handle, &byte, sizeof(byte), &bytesRead, &operation))
		{
			QCOMPARE(bytesRead, DWORD{0});
			QVERIFY(idleWait.elapsed() >= ControlServer::ClientIdleTimeoutMs - 5000);
			return;
		}
		const DWORD startError = GetLastError();
		if (startError == ERROR_BROKEN_PIPE || startError == ERROR_PIPE_NOT_CONNECTED)
		{
			QVERIFY(idleWait.elapsed() >= ControlServer::ClientIdleTimeoutMs - 5000);
			return;
		}
		QVERIFY2(startError == ERROR_IO_PENDING, "the named-pipe read did not pend");

		const DWORD wait = WaitForSingleObject(event.handle,
			static_cast<DWORD>(ControlServer::ClientIdleTimeoutMs + 5000));
		if (wait != WAIT_OBJECT_0)
		{
			CancelIoEx(pipe.handle, &operation);
			GetOverlappedResult(pipe.handle, &operation, &bytesRead, TRUE);
			QVERIFY2(false, "the idle named-pipe client was not disconnected before the deadline");
		}

		QVERIFY(idleWait.elapsed() >= ControlServer::ClientIdleTimeoutMs - 5000);
		const BOOL completed = GetOverlappedResult(pipe.handle, &operation, &bytesRead, FALSE);
		const DWORD completionError = completed ? ERROR_SUCCESS : GetLastError();
		const bool disconnected = (completed && bytesRead == 0)
			|| (!completed && (completionError == ERROR_BROKEN_PIPE
				|| completionError == ERROR_PIPE_NOT_CONNECTED));
		QVERIFY2(disconnected, "the idle named-pipe read completed without a disconnect");
	}

	void aTrickledIncompleteNamedPipeRequestDoesNotExtendTheDeadline()
	{
		ControlServer server(ControlRegistry::instance());
		const QString path = QStringLiteral("\\\\.\\pipe\\zene-control-trickle-%1-%2")
			.arg(GetCurrentProcessId()).arg(GetTickCount64());
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));

		TestWinHandle pipe;
		pipe.handle = connectToNamedPipe(path);
		QVERIFY2(pipe.handle != INVALID_HANDLE_VALUE, "could not connect to the test named pipe");
		TestWinHandle writeEvent;
		writeEvent.handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		QVERIFY(writeEvent.handle != nullptr);

		QElapsedTimer trickleWait;
		trickleWait.start();
		bool disconnected = false;
		while (trickleWait.elapsed() < ControlServer::ClientIdleTimeoutMs + 5000)
		{
			DWORD writeError = ERROR_SUCCESS;
			if (!writeIncompletePipeByte(pipe.handle, writeEvent.handle, &writeError))
			{
				QVERIFY2(isNamedPipeDisconnect(writeError),
					"the incomplete-request trickle write failed unexpectedly");
				disconnected = true;
				break;
			}
			Sleep(250);
		}

		QVERIFY(trickleWait.elapsed() >= ControlServer::ClientIdleTimeoutMs - 5000);
		QVERIFY2(disconnected,
			"trickling an incomplete request kept the named-pipe client alive past its deadline");
	}
#endif

	//! The first half: the hook survives a registry instance it was not
	//! registered on. destroy() then instance() is exactly the sequence that
	//! used to lose it.
	void aHookSurvivesTheRegistryInstanceItWasRegisteredOn()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int ran = 0;
		const ControlRegistry::ShutdownHookId id = registry->addShutdownHook([&ran]() { ++ran; });
		QVERIFY2(id != 0, "the first registered hook was given the 'no hook' id");
		QCOMPARE(registry->shutdownHookCount(), 1);

		// The instance that comes back must be a NEW object, and the observable
		// is the destroyed instance's own state - NOT its address. `new` is
		// allowed to hand back the bytes `delete` has just freed, and on
		// linux-x86_64 run 35126160372 it did: the pointer comparison this slot
		// used to make reported "destroy() did not build a new instance for
		// instance()" on CI while every other fact in that run shows the
		// recreation happening (the hook count is 1 over a store that outlives
		// the instance, and the hook is run below by the instance that came
		// back). A rebuilt registry cannot carry the destroyed one's marker; an
		// instance() that did NOT destroy-and-rebuild - the failure this case is
		// here for - cannot lose it.
		const QString marker = QStringLiteral("registered-on-the-destroyed-instance");
		registry->setObjectName(marker);
		QCOMPARE(registry->objectName(), marker);   // the marker is observable at all

		ControlRegistry::destroy();
		ControlRegistry* recreated = ControlRegistry::instance();
		QVERIFY2(recreated->objectName() != marker,
			"destroy() did not build a new instance for instance(): the registry it "
			"handed back still carries the state of the one that was destroyed");

		// The guard's own call, on the re-created instance, with no argument
		// pointing at the old one: the hook still has to run.
		recreated->runShutdownHooks();
		QCOMPARE(ran, 1);
		QCOMPARE(recreated->shutdownHookCount(), 0);
	}

	//! A live server's hook survives registry destruction and closes the socket
	//! when the recreated registry runs it. Removal must not use the server's
	//! stale pointer to the registry that was destroyed.
	void aLiveServerHookRunsThroughARecreatedRegistry()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-hook-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("recreated-registry.sock"));
		const QString marker = QStringLiteral("old-registry-instance");

		ControlServer server(registry);
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));
		registry->setObjectName(marker);
		QCOMPARE(registry->shutdownHookCount(), 1);

		ControlRegistry::destroy();
		ControlRegistry* recreated = ControlRegistry::instance();
		QVERIFY2(recreated->objectName() != marker,
			"destroy() did not replace the registry used by the live server");
		QCOMPARE(recreated->shutdownHookCount(), 1);

		recreated->runShutdownHooks();
		QVERIFY(!server.isListening());
		QVERIFY2(!QFile::exists(path), "the live server hook left its socket behind");
		QCOMPARE(recreated->shutdownHookCount(), 0);
#endif
	}

	//! Removal acts on process-lifetime state and must be callable without a
	//! ControlRegistry instance that may already have been destroyed.
	void shutdownHookRemovalDoesNotRequireARegistryInstance()
	{
		using RemovalFunction = decltype(&ControlRegistry::removeShutdownHook);
		QVERIFY2((std::is_invocable_r_v<bool, RemovalFunction, ControlRegistry::ShutdownHookId>),
			"shutdown hook removal still requires a registry instance");
	}

	//! The second half: the owner's death takes the hook with it, and the socket
	//! file is gone either way (close() removes it in the same destructor).
	void theOwnerTakesItsHookWithItWhenItDies()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-hook-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("shutdown-hook.sock"));

		{
			ControlServer server(registry);
			QString error;
			QVERIFY2(server.listen(path, &error), qPrintable(error));
			QVERIFY2(server.isListening(), "listen() succeeded without a listener");
			QVERIFY2(QFile::exists(path), "listen() bound no socket file");
			QCOMPARE(registry->shutdownHookCount(), 1);
		}

		// The server is gone. Its hook closed over `this`, so it must be gone
		// too: a hook left behind is a call on freed memory, and the shutdown
		// path is the one route where it might be that hook alone.
		QCOMPARE(registry->shutdownHookCount(), 0);
		QVERIFY2(!QFile::exists(path),
			"the server died and left its socket file behind - the shutdown contract is "
			"\"gone on exit\", and a live server's close() removes it");
		// And the shutdown path still runs cleanly with nothing registered.
		registry->runShutdownHooks();
#endif
	}

	//! A single oversized reply must not leave a tail larger than the queue cap.
	void aSingleOversizedReplyDoesNotExceedTheQueueCap()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry::setReady(true);
		ReadyStateReset readyStateReset;
		ControlRegistry* registry = ControlRegistry::instance();
		const QString payload(ControlServer::MaxQueuedReplyBytes + 1024 * 1024,
			QLatin1Char('x'));
		ControlCommand command;
		command.id = QStringLiteral("test.oversized_reply");
		command.group = QStringLiteral("test");
		command.verb = QStringLiteral("oversized_reply");
		command.description = QStringLiteral("test reply larger than the socket queue cap");
		command.requiresEngine = false;
		command.resultSchema = QJsonObject{
			{QStringLiteral("type"), QStringLiteral("object")},
			{QStringLiteral("required"), QJsonArray{QStringLiteral("payload")}},
			{QStringLiteral("properties"), QJsonObject{
				{QStringLiteral("payload"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}};
		command.handler = [payload](const QJsonObject&) {
			return ControlResult::success(QJsonObject{{QStringLiteral("payload"), payload}});
		};
		registry->registerCommand(command);

		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-reply-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("oversized-reply.sock"));
		ControlServer server(registry);
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));
		QSignalSpy exchanged(&server, &ControlServer::exchanged);
		QVERIFY(exchanged.isValid());

		TestSocket client;
		client.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
		QVERIFY(client.fd >= 0);
		int receiveBufferBytes = 1024;
		QVERIFY(::setsockopt(client.fd, SOL_SOCKET, SO_RCVBUF, &receiveBufferBytes,
			sizeof(receiveBufferBytes)) == 0);
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		const QByteArray nativePath = QFile::encodeName(path);
		QVERIFY(nativePath.size() < static_cast<qsizetype>(sizeof(address.sun_path)));
		std::memcpy(address.sun_path, nativePath.constData(), static_cast<size_t>(nativePath.size() + 1));
		QVERIFY(::connect(client.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
		const int flags = ::fcntl(client.fd, F_GETFL, 0);
		QVERIFY(flags >= 0);
		QVERIFY(::fcntl(client.fd, F_SETFL, flags | O_NONBLOCK) == 0);
		const QByteArray request =
			"{\"id\":1,\"cmd\":\"test.oversized_reply\"}\n";
		QCOMPARE(::write(client.fd, request.constData(), static_cast<size_t>(request.size())),
			static_cast<ssize_t>(request.size()));
		QTRY_COMPARE_WITH_TIMEOUT(exchanged.count(), 1, 10000);
		const qint64 expectedBytes = exchanged.first().at(1).toByteArray().size() + 1;
		const ReplyDrain drain = drainReplyUntilClosedOrComplete(client.fd, expectedBytes);
		QVERIFY2(!drain.failed, "reading the oversized reply failed");
		QVERIFY2(drain.closed && drain.bytes > 0 && drain.bytes < expectedBytes,
			"the server did not retire the client after sending a prefix with an over-cap tail");
#endif
	}

	//! A reply may exceed the cap when its first write leaves a bounded tail.
	void aLargeReplyWithBoundedTailIsSentCompletely()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry::setReady(true);
		ReadyStateReset readyStateReset;
		ControlRegistry* registry = ControlRegistry::instance();
		const QString payload(ControlServer::MaxQueuedReplyBytes + 1024,
			QLatin1Char('x'));
		ControlCommand command;
		command.id = QStringLiteral("test.bounded_tail_reply");
		command.group = QStringLiteral("test");
		command.verb = QStringLiteral("bounded_tail_reply");
		command.description = QStringLiteral("test reply larger than the socket queue cap");
		command.requiresEngine = false;
		command.resultSchema = QJsonObject{
			{QStringLiteral("type"), QStringLiteral("object")},
			{QStringLiteral("required"), QJsonArray{QStringLiteral("payload")}},
			{QStringLiteral("properties"), QJsonObject{
				{QStringLiteral("payload"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}};
		command.handler = [payload](const QJsonObject&) {
			return ControlResult::success(QJsonObject{{QStringLiteral("payload"), payload}});
		};
		registry->registerCommand(command);

		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-reply-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("bounded-tail-reply.sock"));
		ControlServer server(registry);
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));
		QSignalSpy exchanged(&server, &ControlServer::exchanged);
		QVERIFY(exchanged.isValid());

		TestSocket client;
		client.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
		QVERIFY(client.fd >= 0);
		int receiveBufferBytes = 128 * 1024;
		QVERIFY(::setsockopt(client.fd, SOL_SOCKET, SO_RCVBUF, &receiveBufferBytes,
			sizeof(receiveBufferBytes)) == 0);
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		const QByteArray nativePath = QFile::encodeName(path);
		QVERIFY(nativePath.size() < static_cast<qsizetype>(sizeof(address.sun_path)));
		std::memcpy(address.sun_path, nativePath.constData(), static_cast<size_t>(nativePath.size() + 1));
		QVERIFY(::connect(client.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
		const int flags = ::fcntl(client.fd, F_GETFL, 0);
		QVERIFY(flags >= 0);
		QVERIFY(::fcntl(client.fd, F_SETFL, flags | O_NONBLOCK) == 0);
		const QByteArray request =
			"{\"id\":1,\"cmd\":\"test.bounded_tail_reply\"}\n";
		QCOMPARE(::write(client.fd, request.constData(), static_cast<size_t>(request.size())),
			static_cast<ssize_t>(request.size()));
		QTRY_COMPARE_WITH_TIMEOUT(exchanged.count(), 1, 10000);
		const qint64 expectedBytes = exchanged.first().at(1).toByteArray().size() + 1;
		QVERIFY(expectedBytes > ControlServer::MaxQueuedReplyBytes);
		int bufferedBytes = 0;
		QVERIFY(::ioctl(client.fd, FIONREAD, &bufferedBytes) == 0);
		QVERIFY(static_cast<qint64>(bufferedBytes) >
			expectedBytes - ControlServer::MaxQueuedReplyBytes);
		QVERIFY(static_cast<qint64>(bufferedBytes) < expectedBytes);
		const ReplyDrain drain = drainReplyUntilClosedOrComplete(client.fd, expectedBytes);
		QVERIFY2(!drain.failed, "reading the bounded-tail oversized reply failed");
		QVERIFY2(!drain.closed && drain.bytes == expectedBytes,
			"the server dropped an oversized reply even though its queued tail fit the cap");
#endif
	}

	//! Re-listening must not orphan a hook: close() stops THIS listener, so the
	//! hook that closes THIS listener's socket goes with it. A later listen()
	//! registers a fresh hook and overwrites m_shutdownHookId; if close() had
	//! left the old hook registered, the destructor (which removes only the
	//! newest id) would leave the orphan behind - still capturing `this` - and
	//! the forced-exit shutdown path would call close() on freed memory.
	void aClosedListenerTakesItsHookWithItBeforeTheNextListen()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-hook-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("relisten.sock"));

		{
			ControlServer server(registry);
			QString error;

			// First listen: exactly one hook.
			QVERIFY2(server.listen(path, &error), qPrintable(error));
			QVERIFY2(server.isListening(), "listen() succeeded without a listener");
			QCOMPARE(registry->shutdownHookCount(), 1);

			// Stop listening: the hook that closes THIS listener must go with it,
			// not stay behind waiting for the next listen() to overwrite its id.
			server.close();
			QVERIFY2(!server.isListening(), "close() left the listener running");
			QCOMPARE(registry->shutdownHookCount(), 0);

			// Re-listen: a fresh hook, not a second one orphaned behind the old.
			QVERIFY2(server.listen(path, &error), qPrintable(error));
			QCOMPARE(registry->shutdownHookCount(), 1);
		}

		// The owner died: the newest hook is removed by the destructor; the old
		// one must already be gone. A hook left behind captures freed `this`.
		QCOMPARE(registry->shutdownHookCount(), 0);
#endif
	}

	//! Removal is targeted, ids are never reused, and running twice runs once:
	//! the store is the process's, so a second pass must not re-run what the
	//! first already did.
	void removalIsTargetedAndTheStoreIsDrainedOnce()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int first = 0;
		int second = 0;
		const ControlRegistry::ShutdownHookId firstId =
			registry->addShutdownHook([&first]() { ++first; });
		const ControlRegistry::ShutdownHookId secondId =
			registry->addShutdownHook([&second]() { ++second; });
		QVERIFY(firstId != secondId);
		QCOMPARE(registry->shutdownHookCount(), 2);

		QVERIFY2(registry->removeShutdownHook(firstId), "the first hook could not be removed");
		QVERIFY2(!registry->removeShutdownHook(firstId), "an id was removed twice");
		QVERIFY2(!registry->removeShutdownHook(0), "the 'no hook' id removed something");
		QCOMPARE(registry->shutdownHookCount(), 1);

		registry->runShutdownHooks();
		QCOMPARE(first, 0);
		QCOMPARE(second, 1);

		registry->runShutdownHooks();
		QCOMPARE(second, 1);
		QCOMPARE(registry->shutdownHookCount(), 0);
	}

	//! The socket path the hook exists to remove: with the server alive, the
	//! shutdown path (not the destructor) is what unlinks it. This is the
	//! behaviour main.cpp's normal exit and the last-resort guard both rely on.
	void theShutdownPathUnlinksALiveServersSocket()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-hook-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("live.sock"));

		ControlServer server(registry);
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));
		QCOMPARE(registry->shutdownHookCount(), 1);

		registry->runShutdownHooks();
		QVERIFY2(!QFile::exists(path), "the shutdown path left the socket file behind");
		QCOMPARE(registry->shutdownHookCount(), 0);

		// The owner is still alive, and still owns a closed server: destroying it
		// now must be safe (the hook it would remove is already gone).
		QVERIFY(!server.isListening());
		QVERIFY2(!registry->removeShutdownHook(0), "0 removed something");
#endif
	}

	//! BUG 8: a POSIX client's idle deadline is anchored to the MONOTONIC clock,
	//! not the adjustable wall clock. `QDateTime::currentMSecsSinceEpoch()` moves
	//! when the system clock is stepped (NTP, a user change), so a forward step
	//! dropped an active client early and a backward step let an idle one outlive
	//! its timeout. The deadline a fresh connection stores must be measured from
	//! steady_clock: the values the two clocks report are days apart, so the
	//! elapsed-from-steady assertion below is deterministic and fails on the
	//! wall-clock behaviour.
	void aClientDeadlineIsAnchoredToTheMonotonicClock()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-deadline-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("deadline.sock"));

		ControlServer server(registry);
		QString error;
		QVERIFY2(server.listen(path, &error), qPrintable(error));

		TestSocket client;
		client.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
		QVERIFY(client.fd >= 0);
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		const QByteArray nativePath = QFile::encodeName(path);
		QVERIFY(nativePath.size() < static_cast<qsizetype>(sizeof(address.sun_path)));
		std::memcpy(address.sun_path, nativePath.constData(), static_cast<size_t>(nativePath.size() + 1));
		QVERIFY(::connect(client.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

		// The reference the deadline must be measured from, and the wall clock it
		// must NOT be measured from.
		const qint64 monotonicBase = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		const qint64 wallBase = QDateTime::currentMSecsSinceEpoch();

		server.onNewConnection();
		QCOMPARE(server.m_clients.size(), 1);
		const qint64 deadline = server.m_clients.constBegin()->deadlineMs;

		const qint64 fromMonotonic = deadline - monotonicBase;
		QVERIFY2(fromMonotonic >= ControlServer::ClientIdleTimeoutMs - 1000,
			"the stored deadline is shorter than the idle timeout");
		QVERIFY2(fromMonotonic <= ControlServer::ClientIdleTimeoutMs + 1000,
			"the stored deadline is not anchored to the monotonic clock");
		QVERIFY2(std::llabs(deadline - wallBase) > 1000000000LL,
			"the stored deadline is anchored to the adjustable wall clock");
#endif
	}
};

QTEST_GUILESS_MAIN(ControlShutdownHookTest)
#include "ControlShutdownHookTest.moc"
