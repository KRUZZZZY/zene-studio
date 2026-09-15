/*
 * ControlServer.cpp - the opt-in, local-only JSON-RPC control socket (SPEC A12).
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

// The wire half of ControlServer: accept, the per-client read/framing loop and
// the request dispatch. The path-safety half (what --control-socket's path
// already holds, listen()'s bind lifecycle and close()'s inode bookkeeping) is
// in ControlServerSocket.cpp, split out so neither file carries every branch of
// this surface; the two halves share this class's private members.

#include "ControlServer.h"

#include <cerrno>
#include <cstring>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSocketNotifier>

#include "ControlRegistry.h"

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace lmms
{

namespace
{

//! The control protocol version this build speaks (AGENT-TOOLING.md #2: the
//! protocol number moves independently of the product version).
constexpr int ControlProtocolVersion = 1;

QByteArray responseLine(int id, const ControlResult& result)
{
	QJsonObject reply;
	reply.insert(QStringLiteral("id"), id);
	reply.insert(QStringLiteral("ok"), result.ok);
	if (result.ok)
	{
		reply.insert(QStringLiteral("result"), result.result);
	}
	else
	{
		QJsonObject error;
		error.insert(QStringLiteral("kind"), controlErrorKindName(result.errorKind));
		error.insert(QStringLiteral("message"), result.errorMessage);
		reply.insert(QStringLiteral("error"), error);
	}
	return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

QByteArray errorLine(int id, ControlErrorKind kind, const QString& message)
{
	return responseLine(id, ControlResult::failure(kind, message));
}

//! True when the request speaks this protocol; otherwise \p reply is filled.
bool protoMatches(const QJsonObject& request, int id, QByteArray* reply)
{
	if (!request.contains(QStringLiteral("proto"))) { return true; }
	if (request.value(QStringLiteral("proto")).toInt(-1) == ControlProtocolVersion) { return true; }
	*reply = errorLine(id, ControlErrorKind::Refused,
		QStringLiteral("unsupported protocol version; this instance speaks proto %1")
			.arg(ControlProtocolVersion));
	return false;
}

//! True when the request carries an object (or no) 'args'; otherwise \p reply is filled.
bool readArgs(const QJsonObject& request, QJsonObject* args, int id, QByteArray* reply)
{
	if (!request.contains(QStringLiteral("args"))) { return true; }
	if (!request.value(QStringLiteral("args")).isObject())
	{
		*reply = errorLine(id, ControlErrorKind::InvalidArgs,
			QStringLiteral("request 'args' must be an object"));
		return false;
	}
	*args = request.value(QStringLiteral("args")).toObject();
	return true;
}

} // namespace

ControlServer::ControlServer(ControlRegistry* registry, QObject* parent) :
	QObject(parent),
	m_registry(registry)
{
}

ControlServer::~ControlServer()
{
	// CODE-8: the shutdown hook holds a raw `this` and calls close() on it, so
	// it must not survive this object. Un-register BEFORE the members it
	// touches are gone, then close() here as well: whichever of the two runs
	// first removes the socket file, and close() is idempotent. A hook left
	// behind is a call on freed memory on the forced-exit path - the one route
	// where it is the ONLY thing that removes the file.
	if (m_registry != nullptr)
	{
		m_registry->removeShutdownHook(m_shutdownHookId);
		m_shutdownHookId = 0;
	}
	close();
}

void ControlServer::Client::retire() const
{
	// Both notifiers belong to the ControlServer (their parent), so they outlive this
	// struct's copies; deleteLater keeps the deletion off the current activation.
	if (writeNotifier) { writeNotifier->setEnabled(false); writeNotifier->deleteLater(); }
	if (notifier) { notifier->setEnabled(false); notifier->deleteLater(); }
}

void ControlServer::onNewConnection()
{
#if !defined(Q_OS_UNIX)
	return;
#else
	while (isListening())
	{
		const int fd = ::accept(m_listenFd, nullptr, nullptr);
		if (fd < 0)
		{
			return; // EAGAIN: no more pending connections
		}
		::fcntl(fd, F_SETFD, FD_CLOEXEC);
		::fcntl(fd, F_SETFL, O_NONBLOCK);
		Client client;
		client.fd = fd;
		client.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
		connect(client.notifier, &QSocketNotifier::activated, this, [this, fd]() { onClientReadable(fd); });
		// A reply larger than the peer's socket buffer cannot be written in one
		// go on a non-blocking fd. The write notifier exists for exactly that
		// tail; it stays disabled until a reply leaves one.
		client.writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
		client.writeNotifier->setEnabled(false);
		connect(client.writeNotifier, &QSocketNotifier::activated,
			this, [this, fd]() { onClientWritable(fd); });
		m_clients.insert(fd, client);
	}
#endif
}

#if defined(Q_OS_UNIX)
namespace
{

//! Read and DISCARD everything a draining peer has sent, one bounded chunk at a
//! time. Returns true at EOF: the tail of the over-cap request line has been read
//! and nobody kept it, so closing now is a clean FIN rather than an RST that
//! would make the peer's kernel throw away the typed refusal already in its
//! receive buffer (see Client::draining).
bool drainPeer(int fd)
{
	char sink[8192];
	while (true)
	{
		const ssize_t got = ::read(fd, sink, sizeof(sink));
		if (got > 0) { continue; }
		if (got < 0 && errno == EINTR) { continue; }
		return got == 0;
	}
}

//! Read one chunk into \p buffer (appending it) and say whether to read again.
//! Sets \p closed when the peer sent EOF. Returns false on EOF or EAGAIN, and
//! also once the pending bytes pass MaxRequestLineBytes: a client that never
//! sends a newline must not be able to grow this buffer (and the instance's
//! heap) without bound. Complete lines are dispatched by the caller, so what is
//! left here is the unterminated tail.
bool readChunk(int fd, QByteArray& buffer, bool* closed)
{
	char chunk[4096];
	const ssize_t got = ::read(fd, chunk, sizeof(chunk));
	if (got > 0)
	{
		buffer.append(chunk, static_cast<int>(got));
		return buffer.size() <= ControlServer::MaxRequestLineBytes;
	}
	if (got == 0) { *closed = true; }
	return false;
}

//! What one non-blocking write attempt achieved.
enum class WriteOutcome
{
	Complete, //!< the peer took every byte
	Full,     //!< the socket buffer filled up (EAGAIN): the caller keeps the tail
	Gone,     //!< a REAL error (EPIPE/ECONNRESET): the peer is no longer there
};

//! Write as much of data[0, size) as the socket accepts right now, reporting how
//! many bytes went out. EAGAIN is the socket buffer being full - the peer is
//! reading, just not as fast as this loop writes - and must never be reported as
//! a failure: on macOS an AF_UNIX socket buffer is 8 KiB, which is smaller than
//! several replies this surface produces (control.commands_list answers with a
//! few hundred kilobytes; measured: the macOS job saw the server close the
//! connection on that one command, because the old code truncated and dropped).
WriteOutcome writeWhatFits(int fd, const char* data, int size, int* written)
{
	*written = 0;
	while (*written < size)
	{
		const ssize_t n = ::write(fd, data + *written, static_cast<size_t>(size - *written));
		if (n > 0)
		{
			*written += static_cast<int>(n);
			continue;
		}
		if (n < 0 && errno == EINTR) { continue; }
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { return WriteOutcome::Full; }
		return WriteOutcome::Gone;
	}
	return WriteOutcome::Complete;
}

} // namespace
#endif

void ControlServer::onClientReadable(int fd)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
#else
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return; }

	if (it->draining)
	{
		// The rest of an over-cap request line: read it and keep none of it, so the
		// close below is a clean FIN (see Client::draining).
		if (drainPeer(fd)) { dropClient(fd); }
		return;
	}

	QByteArray& buffer = it->buffer;
	bool closed = false;
	while (readChunk(fd, buffer, &closed)) { }

	// A reply that could not be written in full leaves a TRUNCATED line on the
	// wire; dispatchPendingLines drops the client and the loop stops there.
	if (!dispatchPendingLines(fd, buffer)) { return; }

	// Every complete line is gone, so a buffer still over the cap is ONE request
	// line that never ended. Refuse it in the surface's own vocabulary, then
	// retire the connection: resynchronising would mean buffering the rest of a
	// line of unknown length, which is exactly what the cap refuses to do.
	if (buffer.size() > MaxRequestLineBytes)
	{
		refuseOverCapLine(fd, closed);
		return;
	}
	if (closed) { dropClient(fd); }
#endif
}

bool ControlServer::dispatchPendingLines(int fd, QByteArray& buffer)
{
	int newline = buffer.indexOf('\n');
	while (newline >= 0)
	{
		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		const QByteArray reply = dispatchLine(line);
		if (!reply.isEmpty() && !sendBytes(fd, reply + '\n'))
		{
			// A REAL write error (the peer is gone, ECONNRESET): nothing this
			// connection could still carry. A full socket buffer is NOT that -
			// sendBytes queues the tail instead and returns true.
			dropClient(fd);
			return false;
		}
		newline = buffer.indexOf('\n');
	}
	return true;
}

void ControlServer::refuseOverCapLine(int fd, bool closed)
{
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return; }
	const QByteArray refusal = errorLine(-1, ControlErrorKind::InvalidArgs,
		QStringLiteral("the request line exceeds the %1-byte limit and was refused")
			.arg(MaxRequestLineBytes));
	// The refusal goes out BEFORE the drain, and the drain is why it arrives:
	// see Client::draining.
	sendBytes(fd, refusal + '\n');
	it->draining = true;
	it->buffer.clear();
	it->buffer.squeeze();
	if (closed)
	{
		// EOF is already in hand and everything read has been discarded: the
		// close is clean.
		dropClient(fd);
	}
}

void ControlServer::dropClient(int fd)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
#else
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return; }
	it->retire();
	::close(it->fd);
	m_clients.erase(it);
#endif
}

bool ControlServer::sendBytes(int fd, const QByteArray& bytes)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
	Q_UNUSED(bytes);
	return false;
#else
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return false; }
	if (!it->pending.isEmpty())
	{
		// A line written earlier is still on its way to the peer: it must stay
		// first on the wire, so this reply waits behind it. The lines are whole,
		// so the peer still reads one reply per line.
		if (it->pending.size() + bytes.size() > MaxQueuedReplyBytes) { return false; }
		it->pending.append(bytes);
		return true;
	}

	int written = 0;
	const WriteOutcome outcome = writeWhatFits(fd, bytes.constData(), bytes.size(), &written);
	if (outcome == WriteOutcome::Gone) { return false; } // the peer is gone
	if (outcome == WriteOutcome::Full)
	{
		// The peer's buffer is full: normal flow control, not a failure. Keep
		// the tail - whole bytes, so the line stays intact - and finish it when
		// the socket says it can take more.
		it->pending = bytes.mid(written);
		it->writeNotifier->setEnabled(true);
	}
	return true;
#endif
}

void ControlServer::onClientWritable(int fd)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
#else
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return; }
	int written = 0;
	const WriteOutcome outcome = writeWhatFits(fd, it->pending.constData(), it->pending.size(),
		&written);
	if (written > 0) { it->pending.remove(0, written); }
	if (outcome == WriteOutcome::Gone)
	{
		// The peer went away while a tail was still queued (EPIPE/ECONNRESET). A
		// socket whose peer is gone reports WRITABLE forever, so a notifier left
		// armed here fires on every loop pass and spins a core at 100% without
		// ever draining the tail. Retire the connection instead - the same thing
		// a real write error does on the dispatch path.
		dropClient(fd);
		return;
	}
	if (it->pending.isEmpty()) { it->writeNotifier->setEnabled(false); }
#endif
}

QByteArray ControlServer::dispatchLine(const QByteArray& line)
{
	const QByteArray trimmed = line.trimmed();
	if (trimmed.isEmpty()) { return QByteArray(); }

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
	{
		return errorLine(-1, ControlErrorKind::InvalidArgs,
			QStringLiteral("malformed JSON request: %1").arg(parseError.errorString()));
	}

	const QJsonObject request = document.object();
	const QJsonValue idValue = request.value(QStringLiteral("id"));
	if (!idValue.isDouble())
	{
		return errorLine(-1, ControlErrorKind::InvalidArgs,
			QStringLiteral("request 'id' must be an integer"));
	}
	const int id = static_cast<int>(idValue.toDouble());

	QByteArray reply;
	if (!protoMatches(request, id, &reply)) { return reply; }

	const QJsonValue cmdValue = request.value(QStringLiteral("cmd"));
	if (!cmdValue.isString() || cmdValue.toString().isEmpty())
	{
		return errorLine(id, ControlErrorKind::InvalidArgs,
			QStringLiteral("request 'cmd' must be a non-empty command id"));
	}

	QJsonObject args;
	if (!readArgs(request, &args, id, &reply)) { return reply; }

	const ControlResult result = m_registry->invoke(cmdValue.toString(), args);
	reply = responseLine(id, result);
	emit exchanged(trimmed, reply);
	return reply;
}

} // namespace lmms
