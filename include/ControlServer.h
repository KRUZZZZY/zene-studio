/*
 * ControlServer.h - the opt-in, local-only JSON-RPC control socket (SPEC A12).
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

#ifndef LMMS_CONTROL_SERVER_H
#define LMMS_CONTROL_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

// The Windows named-pipe transport (CODE-9) runs one thread per accepted
// connection, so the server needs the threading and shared-state primitives the
// POSIX half gets from QSocketNotifier and the kernel.  Guarded: on POSIX this
// header is what it always was.
#if defined(Q_OS_WIN)
#include <QMutex>
#include <atomic>
#include <thread>
#include <vector>
#endif

#include "ControlRegistry.h"

class QSocketNotifier;

namespace lmms
{

//! A local (AF_UNIX on POSIX, named pipe on Windows) socket speaking
//! line-delimited JSON-RPC, one request and one response per line:
//!
//!   -> {"id":1,"cmd":"mixer.get_state","args":{},"proto":1}
//!   <- {"id":1,"ok":true,"result":{...}}
//!   <- {"id":1,"ok":false,"error":{"kind":"invalid_args","message":"..."}}
//!
//! Off unless the instance is started with --control-socket <path>. The POSIX
//! socket file is mode 0600, is unlinked on exit, and the listener is AF_UNIX
//! only: nothing ever listens on the network (SPEC A12 / AGENT-TOOLING.md #9.1).
//!
//! The bind is destructive to the path it uses, so it is refused rather than
//! performed when the path already holds something that is not a socket, or
//! holds a socket something is still listening on (a liveness probe separates a
//! crashed run's leftover from a live listener; see listen()); close() unlinks
//! only the socket THIS instance bound. See docs/CONTROL-SOCKET-PATH-SAFETY.md.
//!
//! Implemented with POSIX sockets plus QSocketNotifier rather than Qt Network,
//! so the audio application gains no new Qt module dependency.  The Windows
//! half uses a named pipe behind the same JSON-RPC contract (CODE-9).
class ControlServer : public QObject
{
	Q_OBJECT
public:
	//! Largest request LINE accepted, in bytes. The per-client buffer used to be
	//! unbounded: a client that never sent a newline could make the instance
	//! allocate without limit. Over the cap the request is refused with
	//! `invalid_args` and the connection is dropped.
	static constexpr int MaxRequestLineBytes = 1024 * 1024;

	//! Largest reply tail held for one client while its socket buffer is full
	//! (Client::pending). A peer that stops reading must not be able to make the
	//! instance buffer replies without limit; past this the client is retired the
	//! way an over-cap request line is (partial line, then EOF - never a spliced
	//! line). The bound is far above any legitimate answer: the largest reply on
	//! this surface (control.commands_list) is a few hundred kilobytes.
	static constexpr int MaxQueuedReplyBytes = 8 * 1024 * 1024;

	explicit ControlServer(ControlRegistry* registry, QObject* parent = nullptr);
	~ControlServer() override;

	//! Listen on \p path (must be absolute on POSIX, or a \\.\pipe\ name on
	//! Windows). Returns false and sets \p error on failure.
	bool listen(const QString& path, QString* error);

	//! The typed error kind of the last listen() that FAILED; ControlErrorKind::None
	//! after a success. It is the same closed set the protocol answers with, and
	//! listen() reports the refusal on stderr in the wire shape, so a launcher that
	//! never got a socket (and so can never send a request) can still read why.
	ControlErrorKind lastErrorKind() const { return m_lastErrorKind; }

	//! Stop listening, drop every client and unlink the socket file. Idempotent.
	void close();

#if defined(Q_OS_WIN)
	bool isListening() const { return m_win32Listening.load(); }
#else
	bool isListening() const { return m_listenFd >= 0; }
#endif
	QString socketPath() const { return m_path; }
	ControlRegistry* registry() const { return m_registry; }

	//! Process one request line and return the response line (empty for a blank
	//! request). Public so the framing can be unit-tested without a socket.
	QByteArray dispatchLine(const QByteArray& line);

signals:
	//! Emitted for every request/response pair, for logging and tests.
	void exchanged(const QByteArray& request, const QByteArray& response);

private:
	//! One POSIX connection.  Windows keeps no per-connection struct here: its
	//! transport (below) is a thread per accepted pipe and needs no fd map.
	struct Client
	{
		int fd = -1;
		QSocketNotifier* notifier = nullptr;
		//! Armed only while `pending` holds the tail of a reply the socket would
		//! not take in one go (see sendBytes): a NON-BLOCKING write of a reply
		//! larger than the socket buffer is not an error, and treating it as one
		//! loses the answer to a legitimate request.
		QSocketNotifier* writeNotifier = nullptr;
		//! The bytes of a reply not accepted by the socket yet, in wire order.
		//! A line already partially written stays FIRST here, so the peer reads
		//! each line whole and in order.
		QByteArray pending;
		QByteArray buffer;

		//! Retire this connection's notifiers (either may never have been armed).
		//! Defined in ControlServer.cpp, where QSocketNotifier is complete.
		void retire() const;
		//! True after an over-cap request line was refused: the rest of what the
		//! peer sends is read and DISCARDED (bounded, per chunk) until EOF, so the
		//! connection closes with nothing queued. Closing while unread bytes sit on
		//! the socket sends RST, and an RST makes the peer's kernel throw away the
		//! typed refusal already in its receive buffer.
		bool draining = false;
	};

	void onNewConnection();
	void onClientReadable(int fd);
	//! The socket is writable again: flush the tail of a reply that did not fit
	//! earlier (Client::pending). Defined in ControlServer.cpp beside sendBytes.
	void onClientWritable(int fd);
	//! Dispatch every complete line already in \p buffer. Returns false when a
	//! reply hit a REAL write error and the client was dropped (see sendBytes's
	//! contract).
	bool dispatchPendingLines(int fd, QByteArray& buffer);
	//! Refuse the ONE request line left in the buffer that passed
	//! MaxRequestLineBytes without ending, then retire the connection: \p closed
	//! says EOF is already in hand, so the client can be dropped at once.
	void refuseOverCapLine(int fd, bool closed);
	void dropClient(int fd);
	//! Hand one whole reply line to \p fd: write what the socket takes now and
	//! queue the tail for onClientWritable. Returns false ONLY when the peer is
	//! gone (a real write error) - a full socket buffer is not an error, and a
	//! reply must never be truncated because the peer's buffer was small (macOS
	//! AF_UNIX buffers are 8 KiB; control.commands_list answers with far more).
	bool sendBytes(int fd, const QByteArray& bytes);

	//! Take ownership of a fd that is bound, pinned to mode 0600 and listening:
	//! record the inode this instance bound (so close() unlinks only that one),
	//! register the shutdown hook and start the accept notifier. Defined in
	//! ControlServerSocket.cpp beside listen().
	bool adoptListener(int fd, const QString& path, const QByteArray& nativePath);

	ControlRegistry* m_registry;
#if defined(Q_OS_WIN)
	//! The Windows transport: a named pipe behind the SAME JSON-RPC contract -
	//! the same framing, the same command ids, the same refusal shapes
	//! (CODE-9).  Defined in ControlServerWin32.cpp.
	//!
	//! listenWin32() creates the pipe namespace and the event the transport is
	//! cancellable through, registers the shutdown hook and starts the accept
	//! loop.  It returns the typed error KIND rather than reporting it, so the
	//! caller (listen(), in ControlServerSocket.cpp) reports it through the same
	//! `fail` lambda the POSIX path uses and a launcher reads the same refusal
	//! shape on both platforms.  closeWin32() stops the acceptor and joins every
	//! client thread: those joins are what make `this` safe to destroy.
	ControlErrorKind listenWin32(const QString& path, QString* detail);
	void closeWin32();
	//! Create the next pipe instance and wait for a client.  The wait is on a
	//! ConnectNamedPipe wait-handle and m_win32QuitEvent, never on a blocking
	//! ConnectNamedPipe: that is what lets closeWin32() stop this loop without
	//! closing a handle a thread is inside.
	void win32AcceptLoop();
	//! Serve ONE accepted connection to its end: read lines, dispatch each
	//! complete one on the server's thread (win32Dispatch) and write its reply
	//! back.  Owns its pipe handle; does NOT remove its own entry from
	//! m_win32ClientThreads (a thread cannot join itself) - that entry is joined
	//! by closeWin32().
	void win32ClientLoop(void* hPipe);
	//! Run dispatchLine() for one request line on the SERVER's thread and wait
	//! for the answer, bounded.  Empty when the instance started closing before
	//! the request could be answered.
	QByteArray win32Dispatch(const QByteArray& line);

	//! The full \\.\pipe\<name> this instance listens on; empty when it is not.
	QString m_pipeName;
	//! Set by closeWin32() and watched by every thread in the transport: it is
	//! how the acceptor and each client thread learn to stop, with no handle
	//! closed underneath them.
	std::atomic<bool> m_win32Quit{false};
	//! Non-null between listenWin32() and closeWin32(): what isListening()
	//! answers on this platform.
	std::atomic<bool> m_win32Listening{false};
	void* m_win32QuitEvent = nullptr;    // HANDLE, manual-reset
	void* m_win32ConnectEvent = nullptr; // HANDLE, manual-reset, the accept wait
	std::thread m_acceptThread;
	//! One entry per accepted connection, in connection order.  Joined by
	//! closeWin32() (which runs after the acceptor has been joined, so nothing
	//! appends while this is read) - the only reason the transport keeps them.
	std::vector<std::thread> m_win32ClientThreads;
#else
	int m_listenFd = -1;
	QSocketNotifier* m_notifier = nullptr;
#endif
	QString m_path;
	ControlErrorKind m_lastErrorKind = ControlErrorKind::None;
	//! The (device, inode) this instance bound with listen(), so close() can tell
	//! its own socket file from a replacement at the same path. quint64 rather than
	//! dev_t/ino_t: this header is cross-platform and those types are POSIX.
	quint64 m_boundDevice = 0;
	quint64 m_boundInode = 0;
	//! The id of the shutdown hook this instance registered (0 = none), so the
	//! destructor can un-register it: the hook closes THIS object's socket and
	//! must never outlive it (CODE-8).
	ControlRegistry::ShutdownHookId m_shutdownHookId = 0;
	QHash<int, Client> m_clients;
};

} // namespace lmms

#endif // LMMS_CONTROL_SERVER_H
