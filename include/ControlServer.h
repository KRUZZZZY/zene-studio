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

#include "ControlRegistry.h"

class QSocketNotifier;

namespace lmms
{

//! A local (AF_UNIX) socket speaking line-delimited JSON-RPC, one request and
//! one response per line:
//!
//!   -> {"id":1,"cmd":"mixer.get_state","args":{},"proto":1}
//!   <- {"id":1,"ok":true,"result":{...}}
//!   <- {"id":1,"ok":false,"error":{"kind":"invalid_args","message":"..."}}
//!
//! Off unless the instance is started with --control-socket <path>. The socket
//! file is mode 0600, is unlinked on exit, and the listener is AF_UNIX only:
//! nothing ever listens on the network (SPEC A12 / AGENT-TOOLING.md #9.1).
//!
//! The bind is destructive to the path it uses, so it is refused rather than
//! performed when the path already holds something that is not a socket, or
//! holds a socket something is still listening on (a liveness probe separates a
//! crashed run's leftover from a live listener; see listen()); close() unlinks
//! only the socket THIS instance bound. See docs/CONTROL-SOCKET-PATH-SAFETY.md.
//!
//! Implemented with POSIX sockets plus QSocketNotifier rather than Qt Network,
//! so the audio application gains no new Qt module dependency.
class ControlServer : public QObject
{
	Q_OBJECT
public:
	//! Largest request LINE accepted, in bytes. The per-client buffer used to be
	//! unbounded: a client that never sent a newline could make the instance
	//! allocate without limit. Over the cap the request is refused with
	//! `invalid_args` and the connection is dropped.
	static constexpr int MaxRequestLineBytes = 1024 * 1024;

	explicit ControlServer(ControlRegistry* registry, QObject* parent = nullptr);
	~ControlServer() override;

	//! Listen on \p path (must be absolute). Returns false and sets \p error on failure.
	bool listen(const QString& path, QString* error);

	//! The typed error kind of the last listen() that FAILED; ControlErrorKind::None
	//! after a success. It is the same closed set the protocol answers with, and
	//! listen() reports the refusal on stderr in the wire shape, so a launcher that
	//! never got a socket (and so can never send a request) can still read why.
	ControlErrorKind lastErrorKind() const { return m_lastErrorKind; }

	//! Stop listening, drop every client and unlink the socket file. Idempotent.
	void close();

	bool isListening() const { return m_listenFd >= 0; }
	QString socketPath() const { return m_path; }
	ControlRegistry* registry() const { return m_registry; }

	//! Process one request line and return the response line (empty for a blank
	//! request). Public so the framing can be unit-tested without a socket.
	QByteArray dispatchLine(const QByteArray& line);

signals:
	//! Emitted for every request/response pair, for logging and tests.
	void exchanged(const QByteArray& request, const QByteArray& response);

private:
	struct Client
	{
		int fd = -1;
		QSocketNotifier* notifier = nullptr;
		QByteArray buffer;
		//! True after an over-cap request line was refused: the rest of what the
		//! peer sends is read and DISCARDED (bounded, per chunk) until EOF, so the
		//! connection closes with nothing queued. Closing while unread bytes sit on
		//! the socket sends RST, and an RST makes the peer's kernel throw away the
		//! typed refusal already in its receive buffer.
		bool draining = false;
	};

	void onNewConnection();
	void onClientReadable(int fd);
	//! Dispatch every complete line already in \p buffer. Returns false when a
	//! reply could not be written in full and the client was dropped (see
	//! writeAll's contract).
	bool dispatchPendingLines(int fd, QByteArray& buffer);
	//! Refuse the ONE request line left in the buffer that passed
	//! MaxRequestLineBytes without ending, then retire the connection: \p closed
	//! says EOF is already in hand, so the client can be dropped at once.
	void refuseOverCapLine(int fd, bool closed);
	void dropClient(int fd);
	//! Write every byte of \p bytes or fail. A caller MUST drop the client when
	//! this returns false: the bytes already written are a TRUNCATED line, and
	//! writing the next reply after them would make the two read as one line.
	bool writeAll(int fd, const QByteArray& bytes);

	//! Take ownership of a fd that is bound, pinned to mode 0600 and listening:
	//! record the inode this instance bound (so close() unlinks only that one),
	//! register the shutdown hook and start the accept notifier. Defined in
	//! ControlServerSocket.cpp beside listen().
	bool adoptListener(int fd, const QString& path, const QByteArray& nativePath);

	ControlRegistry* m_registry;
	int m_listenFd = -1;
	QSocketNotifier* m_notifier = nullptr;
	QString m_path;
	ControlErrorKind m_lastErrorKind = ControlErrorKind::None;
	//! The (device, inode) this instance bound with listen(), so close() can tell
	//! its own socket file from a replacement at the same path. quint64 rather than
	//! dev_t/ino_t: this header is cross-platform and those types are POSIX.
	quint64 m_boundDevice = 0;
	quint64 m_boundInode = 0;
	QHash<int, Client> m_clients;
};

} // namespace lmms

#endif // LMMS_CONTROL_SERVER_H
