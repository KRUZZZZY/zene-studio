/*
 * ControlServerWin32.cpp - the Windows half of the control transport: the same
 * JSON-RPC contract served over a named pipe (CODE-9).
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

// The POSIX control transport is an AF_UNIX socket (ControlServerSocket.cpp owns
// its bind/close lifecycle, ControlServer.cpp the wire half).  On Windows the
// transport is a NAMED PIPE behind exactly the same contract: the same
// line-delimited JSON-RPC framing, one request and one response per line, the
// same command ids, the same typed refusal shapes, the same opt-in flag
// (--control-socket <name>) - only the kernel object under it differs.  A client
// cannot tell the difference from the wire; docs/CONTROL-NAMED-PIPE.md states
// what IS different and what is CI-only evidence.
//
// Every line of this file is inside `#if defined(Q_OS_WIN)`, and the Windows
// branch of the two lifecycle functions it uses lives in
// ControlServerSocket.cpp: on POSIX this file compiles to nothing and the
// listener is the socket it always was.
//
// Shape of the transport, and why:
//   * one thread per accepted connection (the POSIX path is one event loop with
//     a QSocketNotifier per connection; a Windows thread is the equivalent
//     per-connection driver, and it keeps the blocking pipe calls off the
//     server's thread);
//   * every wait is on an event this file owns, and every pipe operation is
//     OVERLAPPED, so closeWin32() can stop any thread without closing a handle
//     that thread is inside (a blocking ConnectNamedPipe or ReadFile cannot be
//     stopped that way, and closing the handle underneath it is undefined);
//   * dispatchLine() ALWAYS runs on the server's thread (the object's own
//     thread), through a queued call - the control surface, the engine and the
//     journal are not thread-safe and the POSIX path dispatches from the event
//     loop for the same reason;
//   * the client thread waits for that answer with a BOUND, because closeWin32()
//     joins it: an answer that cannot arrive (the instance is closing) must turn
//     into a closed connection, never into a shutdown that cannot finish.

#include "ControlServer.h"

#if defined(Q_OS_WIN)

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSemaphore>
#include <QString>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include "ControlRegistry.h"

namespace lmms
{

namespace
{

//! The named-pipe namespace.  Compared case-insensitively because Windows
//! resolves pipe names that way, and required in full: CreateNamedPipeW wants
//! \\.\pipe\<name>, not a bare <name>.
const QString kPipeNamespace = QStringLiteral("\\\\.\\pipe\\");

//! The longest pipe name Windows accepts, the prefix included (the SDK's own
//! bound).  A longer one is refused here as a typed invalid_args rather than
//! surfacing as a bare ERROR_FILENAME_EXCED_RANGE.
constexpr int kMaxPipeNameChars = 256;

//! nOutBufferSize/nInBufferSize of every instance.  The out side is the one that
//! matters: control.commands_list answers with a few hundred kilobytes, and a
//! small buffer would stall the writer for no reason.  This is a throughput
//! knob, not a limit on reply size - writes are chunked and completed by the
//! peer's reads.
constexpr DWORD kPipeBufferBytes = 256 * 1024;

//! Read chunk for one connection.  A request line is dispatched as soon as its
//! newline arrives, so this only bounds the per-read allocation.
constexpr DWORD kReadChunkBytes = 64 * 1024;

//! How long a client thread waits for the server's thread to answer ONE request
//! before it gives up and drops the connection, and the slice it re-checks the
//! quit flag between waits.  Both are bounds on purpose: closeWin32() joins this
//! thread, so no wait here may be indefinite (see the header of this file).
constexpr int kDispatchWaitMs = 30000;
constexpr int kQuitPollMs = 100;

//! A stored handle as the Windows type it is.  The members hold `void*` so the
//! header needs no <windows.h>; HANDLE is `void*` in the SDK, and the cast
//! compiles whichever way it is spelled.
HANDLE asHandle(void* stored) { return reinterpret_cast<HANDLE>(stored); }

//! A manual-reset event, or nullptr.
void* createEvent() { return CreateEventW(nullptr, TRUE, FALSE, nullptr); }

//! GetLastError() as a readable fragment.
QString win32ErrorText(DWORD error)
{
	return QStringLiteral("Windows error %1").arg(error);
}

//! One request in flight between a client thread and the server's thread.
//! Shared, because either side may finish first: the client thread stops waiting
//! when the instance is closing, and the queued call still owns its own
//! reference when it finally runs (touching freed memory is the alternative).
struct Win32Dispatch
{
	QByteArray request;
	QByteArray response;
	bool answeredOk = false;
	QSemaphore answered;
};

//! Wait for ONE overlapped pipe operation to complete, or for the listener to go
//! away.  Returns true when it completed, false when the instance is closing (the
//! operation is cancelled; the handle stays usable, the caller drops it).
bool waitForIo(HANDLE pipe, OVERLAPPED* operation, HANDLE quitEvent, DWORD* transferred)
{
	*transferred = 0;
	const HANDLE waits[2] = { operation->hEvent, quitEvent };
	if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
	{
		CancelIoEx(pipe, operation);
		return false;
	}
	return GetOverlappedResult(pipe, operation, transferred, TRUE) != FALSE;
}

//! Write every byte of \p bytes to \p pipe, or return false when the peer is
//! gone or the instance is closing.  Chunked: a byte-mode pipe completes a write
//! when its buffer takes what it can, so the tail is written by the next call.
bool writeAll(HANDLE pipe, HANDLE ioEvent, HANDLE quitEvent, const QByteArray& bytes)
{
	int sent = 0;
	while (sent < bytes.size())
	{
		OVERLAPPED operation;
		std::memset(&operation, 0, sizeof(operation));
		ResetEvent(ioEvent);
		operation.hEvent = ioEvent;
		DWORD written = 0;
		if (!WriteFile(pipe, bytes.constData() + sent,
				static_cast<DWORD>(bytes.size() - sent), &written, &operation))
		{
			const DWORD error = GetLastError();
			if (error != ERROR_IO_PENDING) { return false; }
			if (!waitForIo(pipe, &operation, quitEvent, &written)) { return false; }
		}
		sent += static_cast<int>(written);
	}
	return true;
}

//! The over-cap refusal, in the surface's own vocabulary: the SAME line, byte
//! for byte, that ControlServer::refuseOverCapLine() sends on POSIX (id -1,
//! invalid_args, the same sentence).  Kept here as a second spelling of one
//! sentence rather than a call into POSIX-only code; the Windows smoke test
//! (tests/control-named-pipe-smoke.py) asserts the shape a client reads.
QByteArray overCapRefusalLine()
{
	QJsonObject error;
	error.insert(QStringLiteral("kind"), controlErrorKindName(ControlErrorKind::InvalidArgs));
	error.insert(QStringLiteral("message"),
		QStringLiteral("the request line exceeds the %1-byte limit and was refused")
			.arg(ControlServer::MaxRequestLineBytes));
	QJsonObject reply;
	reply.insert(QStringLiteral("id"), -1);
	reply.insert(QStringLiteral("ok"), false);
	reply.insert(QStringLiteral("error"), error);
	return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

//! One overlapped read: whatever the peer has sent is appended to \p buffer.
//! Returns false when this connection is over - the peer closed its end, the pipe
//! failed, or the instance is closing (the quit event cancels the pending read).
bool readChunk(HANDLE pipe, HANDLE ioEvent, HANDLE quitEvent, QByteArray* buffer)
{
	char chunk[kReadChunkBytes];
	OVERLAPPED operation;
	std::memset(&operation, 0, sizeof(operation));
	ResetEvent(ioEvent);
	operation.hEvent = ioEvent;
	DWORD got = 0;
	if (!ReadFile(pipe, chunk, sizeof(chunk), &got, &operation))
	{
		if (GetLastError() != ERROR_IO_PENDING) { return false; } // the peer is gone
		if (!waitForIo(pipe, &operation, quitEvent, &got)) { return false; }
	}
	if (got == 0) { return false; } // a clean EOF: the peer closed its end
	buffer->append(chunk, static_cast<int>(got));
	return true;
}

} // namespace

ControlErrorKind ControlServer::listenWin32(const QString& path, QString* detail)
{
	// Every refusal below comes back as a KIND, and the caller (listen(), in
	// ControlServerSocket.cpp) reports it through its own `fail` lambda - so a
	// launcher that never got a listener reads the same typed line on Windows as
	// on POSIX.  \p detail is filled when it is asked for, or not at all.
	const auto refuse = [detail](ControlErrorKind kind, const QString& message) {
		if (detail) { *detail = message; }
		return kind;
	};

	if (isListening())
	{
		return refuse(ControlErrorKind::InvalidArgs,
			QStringLiteral("already listening on %1").arg(m_path));
	}
	if (!path.startsWith(kPipeNamespace, Qt::CaseInsensitive))
	{
		return refuse(ControlErrorKind::InvalidArgs,
			QStringLiteral("the control socket path must name a Windows named pipe "
				"(\"\\\\.\\pipe\\<name>\") on this platform; got \"%1\"").arg(path));
	}
	if (path.size() > kMaxPipeNameChars)
	{
		return refuse(ControlErrorKind::InvalidArgs,
			QStringLiteral("the control socket path is too long (%1 characters; "
				"a Windows pipe name is at most %2)").arg(path.size()).arg(kMaxPipeNameChars));
	}

	void* quitEvent = createEvent();
	void* connectEvent = createEvent();
	if (quitEvent == nullptr || connectEvent == nullptr)
	{
		const DWORD error = GetLastError();
		if (quitEvent != nullptr) { CloseHandle(asHandle(quitEvent)); }
		if (connectEvent != nullptr) { CloseHandle(asHandle(connectEvent)); }
		return refuse(ControlErrorKind::Refused,
			QStringLiteral("cannot create the named pipe's events (%1)").arg(win32ErrorText(error)));
	}

	m_win32QuitEvent = quitEvent;
	m_win32ConnectEvent = connectEvent;
	m_win32Quit.store(false);
	m_pipeName = path;
	m_path = path;
	m_win32Listening.store(true);
	// CODE-8: the same shutdown-hook bookkeeping the POSIX listener registers in
	// adoptListener(), for the same reason - the hook closes THIS object's
	// listener and must never outlive it (see ControlServer::~ControlServer()).
	m_shutdownHookId = m_registry->addShutdownHook([this]() { close(); });
	m_acceptThread = std::thread(&ControlServer::win32AcceptLoop, this);
	return ControlErrorKind::None;
}

void ControlServer::win32AcceptLoop()
{
	const std::wstring name = m_pipeName.toStdWString();
	while (!m_win32Quit.load())
	{
		// One instance per connection, created as the previous one is taken:
		// PIPE_UNLIMITED_INSTANCES is the named pipe's equivalent of listen(2)'s
		// pending-connection queue.  PIPE_REJECT_REMOTE_CLIENTS is the Windows
		// spelling of "local only" (SPEC A12 / AGENT-TOOLING.md #9.1): the pipe
		// refuses a client that reaches it over \\<host>\pipe\... , which is the
		// one way a named pipe could ever be reached from off the machine.
		//
		// It goes in the PIPE mode, not the open mode: CreateNamedPipe "fails
		// if dwOpenMode specifies anything other than 0 or the flags listed",
		// and the remote-client modes are listed under dwPipeMode (0x8 sits in
		// the pipe-mode bit space: NOWAIT 0x1, READMODE_MESSAGE 0x2,
		// TYPE_MESSAGE 0x4).  OR'ed into dwOpenMode it returned
		// ERROR_INVALID_PARAMETER (87) in the first msvc-x64 run that reached
		// this line (35126160372).  Qt's QLocalServer and Rust's std pass it
		// here too.
		const HANDLE pipe = CreateNamedPipeW(name.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			PIPE_UNLIMITED_INSTANCES,
			kPipeBufferBytes, kPipeBufferBytes, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
		{
			// Read the code FIRST: the argument conversions below are Qt's, and
			// the order arguments are evaluated in is the compiler's - MSVC and
			// GCC pick opposite ends, so GetLastError() inside the call reports
			// whatever the last conversion did on one of them.
			const DWORD error = GetLastError();
			// The listener is down (the name belongs to a pipe this process
			// cannot use, or the process is out of handles).  Say so once and
			// stop; connections already established keep their own instance.
			qWarning("control socket: cannot create a named-pipe instance for %s (%s)",
				qPrintable(m_pipeName), qPrintable(win32ErrorText(error)));
			break;
		}

		BOOL connected = FALSE;
		OVERLAPPED operation;
		std::memset(&operation, 0, sizeof(operation));
		ResetEvent(asHandle(m_win32ConnectEvent));
		operation.hEvent = asHandle(m_win32ConnectEvent);
		if (ConnectNamedPipe(pipe, &operation))
		{
			connected = TRUE;
		}
		else
		{
			const DWORD error = GetLastError();
			if (error == ERROR_PIPE_CONNECTED)
			{
				// A client was already waiting when this instance was created:
				// the connection is complete and the event is never signalled.
				connected = TRUE;
			}
			else if (error == ERROR_IO_PENDING)
			{
				const HANDLE waits[2] = { asHandle(m_win32ConnectEvent),
					asHandle(m_win32QuitEvent) };
				const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
				if (wait == WAIT_OBJECT_0)
				{
					DWORD transferred = 0;
					connected = GetOverlappedResult(pipe, &operation, &transferred, TRUE);
				}
				else
				{
					// closeWin32(): cancel the pending connect, then close.
					CancelIoEx(pipe, &operation);
					CloseHandle(pipe);
					break;
				}
			}
		}

		if (!connected || m_win32Quit.load())
		{
			DisconnectNamedPipe(pipe);
			CloseHandle(pipe);
			break;
		}

		// The connection is complete: give it its own thread and go back to
		// waiting for the next one.  The entry stays until closeWin32() joins it.
		m_win32ClientThreads.emplace_back(&ControlServer::win32ClientLoop, this, pipe);
	}
	m_win32Listening.store(false);
}

void ControlServer::win32ClientLoop(void* hPipe)
{
	const HANDLE pipe = asHandle(hPipe);
	const HANDLE quitEvent = asHandle(m_win32QuitEvent);
	// This thread's own I/O event: one at a time, because a client thread issues
	// one pipe operation at a time (read, then reply, then read again), which is
	// also what keeps a reply line whole and in order on the wire.
	const HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (ioEvent == nullptr)
	{
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
		return;
	}

	QByteArray buffer;
	bool draining = false;
	while (!m_win32Quit.load())
	{
		if (!readChunk(pipe, ioEvent, quitEvent, &buffer)) { break; }
		if (draining)
		{
			// The over-cap line was refused: the rest of what this peer sends is
			// read and DISCARDED, exactly as the POSIX path drains it, so the
			// connection was never allowed to buffer an unbounded line.
			continue;
		}
		if (!win32ServeBuffer(hPipe, ioEvent, quitEvent, buffer, &draining)) { break; }
	}

	DisconnectNamedPipe(pipe);
	CloseHandle(pipe);
	CloseHandle(ioEvent);
}

bool ControlServer::win32ServeBuffer(void* hPipe, void* hIoEvent, void* hQuitEvent,
	QByteArray& buffer, bool* overCap)
{
	const HANDLE pipe = asHandle(hPipe);
	const HANDLE ioEvent = asHandle(hIoEvent);
	const HANDLE quitEvent = asHandle(hQuitEvent);

	// Every COMPLETE line, in wire order.  dispatchLine() runs on the server's
	// thread (win32Dispatch), never here.
	int newline = buffer.indexOf('\n');
	while (newline >= 0)
	{
		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		const QByteArray reply = win32Dispatch(line);
		if (m_win32Quit.load()) { return false; }
		if (!reply.isEmpty() && !writeAll(pipe, ioEvent, quitEvent, reply + '\n')) { return false; }
		newline = buffer.indexOf('\n');
	}

	// A buffer still over the cap is ONE request line that never ended: refuse it
	// in the surface's own vocabulary, then read and discard the rest (the POSIX
	// rule, and the same sentence).
	if (buffer.size() > MaxRequestLineBytes)
	{
		if (!writeAll(pipe, ioEvent, quitEvent, overCapRefusalLine() + '\n')) { return false; }
		buffer.clear();
		buffer.squeeze();
		*overCap = true;
	}
	return true;
}

QByteArray ControlServer::win32Dispatch(const QByteArray& line)
{
	auto pending = std::make_shared<Win32Dispatch>();
	pending->request = line;
	const bool posted = QMetaObject::invokeMethod(this, [this, pending]() {
		// The server's thread, which is the whole point: dispatchLine() touches
		// the registry, the engine and the journal, and the control surface has
		// always been served from one thread (the POSIX path dispatches from the
		// event loop).  A queued call - not a blocking one - is what keeps a
		// closing instance from deadlocking on a thread it is about to join.
		if (!m_win32Quit.load())
		{
			pending->response = dispatchLine(pending->request);
			pending->answeredOk = true;
		}
		pending->answered.release();
	}, Qt::QueuedConnection);
	if (!posted) { return QByteArray(); }

	bool answered = false;
	for (int slice = 0; slice < kDispatchWaitMs / kQuitPollMs && !answered; ++slice)
	{
		answered = pending->answered.tryAcquire(1, kQuitPollMs);
		if (!answered && m_win32Quit.load()) { break; }
	}
	if (!answered) { return QByteArray(); }
	return pending->answeredOk ? pending->response : QByteArray();
}

void ControlServer::closeWin32()
{
	// Order matters.  Quit first: every thread in the transport watches this
	// flag, and the event wakes the two that are parked in a kernel wait.
	m_win32Quit.store(true);
	if (m_win32QuitEvent != nullptr) { SetEvent(asHandle(m_win32QuitEvent)); }
	m_win32Listening.store(false);
	m_pipeName.clear();

	// Then the acceptor, so nothing can append to the client list below.
	if (m_acceptThread.joinable()) { m_acceptThread.join(); }

	// Then every client thread.  Each one exits on its own: the quit event
	// cancels its pending read or write, and its wait for an answer from this
	// thread is bounded (win32Dispatch).  Joining them here is what makes it
	// safe for `this` to be destroyed - a client thread holds this object.
	for (std::thread& worker : m_win32ClientThreads)
	{
		if (worker.joinable()) { worker.join(); }
	}
	m_win32ClientThreads.clear();

	// Only now, with nobody waiting on them, do the events go.
	if (m_win32QuitEvent != nullptr)
	{
		CloseHandle(asHandle(m_win32QuitEvent));
		m_win32QuitEvent = nullptr;
	}
	if (m_win32ConnectEvent != nullptr)
	{
		CloseHandle(asHandle(m_win32ConnectEvent));
		m_win32ConnectEvent = nullptr;
	}
	m_path.clear();
}

} // namespace lmms

#endif // Q_OS_WIN
