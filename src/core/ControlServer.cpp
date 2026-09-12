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
	close();
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
		if (!reply.isEmpty() && !writeAll(fd, reply + '\n'))
		{
			// A failed write leaves a TRUNCATED line on the wire: this socket is
			// non-blocking, so it can take part of a line and then EAGAIN. Write
			// nothing else on this connection and drop it, so the peer reads EOF
			// after a partial line instead of the next reply's bytes glued to it.
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
	writeAll(fd, refusal + '\n');
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
	if (it->notifier)
	{
		it->notifier->setEnabled(false);
		it->notifier->deleteLater();
	}
	::close(it->fd);
	m_clients.erase(it);
#endif
}

bool ControlServer::writeAll(int fd, const QByteArray& bytes)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
	Q_UNUSED(bytes);
	return false;
#else
	ssize_t written = 0;
	while (written < bytes.size())
	{
		const ssize_t n = ::write(fd, bytes.constData() + written,
			static_cast<size_t>(bytes.size() - written));
		if (n > 0)
		{
			written += n;
			continue;
		}
		if (n < 0 && errno == EINTR) { continue; }
		return false;
	}
	return true;
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
