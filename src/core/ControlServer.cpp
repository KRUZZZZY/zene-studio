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

#if defined(Q_OS_UNIX)
namespace
{

//! socket() + bind() for an absolute path, or -1 with \p error set.
int openBoundSocket(const QByteArray& nativePath, QString* error)
{
	sockaddr_un address;
	std::memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	if (nativePath.size() >= static_cast<int>(sizeof(address.sun_path)))
	{
		if (error) { *error = QStringLiteral("the control socket path is too long"); }
		return -1;
	}
	std::memcpy(address.sun_path, nativePath.constData(), nativePath.size());

	const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
	{
		if (error) { *error = QString::fromLocal8Bit(std::strerror(errno)); }
		return -1;
	}
	::fcntl(fd, F_SETFD, FD_CLOEXEC);

	// A stale socket file left by a crashed instance would make bind() fail.
	::unlink(address.sun_path);
	if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		if (error) { *error = QString::fromLocal8Bit(std::strerror(errno)); }
		::close(fd);
		return -1;
	}
	return fd;
}

//! Pin the socket file to mode 0600 (verified) and listen, non-blocking.
bool pinAndListen(int fd, const char* nativePath, QString* error)
{
	if (::chmod(nativePath, S_IRUSR | S_IWUSR) != 0)
	{
		if (error) { *error = QString::fromLocal8Bit(std::strerror(errno)); }
		return false;
	}
	struct stat info;
	if (::stat(nativePath, &info) != 0 || (info.st_mode & 0777) != (S_IRUSR | S_IWUSR))
	{
		if (error) { *error = QStringLiteral("could not pin the socket file to mode 0600"); }
		return false;
	}
	if (::listen(fd, 16) != 0)
	{
		if (error) { *error = QString::fromLocal8Bit(std::strerror(errno)); }
		return false;
	}
	// The listener must not block: one QSocketNotifier activation drains every
	// pending connection, and a blocking accept() there would freeze the UI thread.
	::fcntl(fd, F_SETFL, O_NONBLOCK);
	return true;
}

} // namespace
#endif

bool ControlServer::listen(const QString& path, QString* error)
{
	if (path.isEmpty() || !path.startsWith(QLatin1Char('/')))
	{
		if (error) { *error = QStringLiteral("the control socket path must be absolute"); }
		return false;
	}
	if (isListening())
	{
		if (error) { *error = QStringLiteral("already listening on %1").arg(m_path); }
		return false;
	}

#if !defined(Q_OS_UNIX)
	Q_UNUSED(path);
	if (error)
	{
		*error = QStringLiteral("the control socket is supported on POSIX platforms only");
	}
	return false;
#else
	const QByteArray nativePath = path.toLocal8Bit();
	const int fd = openBoundSocket(nativePath, error);
	if (fd < 0) { return false; }
	if (!pinAndListen(fd, nativePath.constData(), error))
	{
		::close(fd);
		::unlink(nativePath.constData());
		return false;
	}

	m_listenFd = fd;
	m_path = path;
	m_registry->addShutdownHook([this]() { close(); });
	m_notifier = new QSocketNotifier(m_listenFd, QSocketNotifier::Read, this);
	connect(m_notifier, &QSocketNotifier::activated, this, &ControlServer::onNewConnection);
	return true;
#endif
}

void ControlServer::close()
{
#if !defined(Q_OS_UNIX)
	m_path.clear();
#else
	for (const Client& client : m_clients)
	{
		if (client.notifier) { client.notifier->deleteLater(); }
		if (client.fd >= 0) { ::close(client.fd); }
	}
	m_clients.clear();

	if (m_notifier)
	{
		m_notifier->deleteLater();
		m_notifier = nullptr;
	}
	if (m_listenFd >= 0)
	{
		::close(m_listenFd);
		m_listenFd = -1;
	}
	if (!m_path.isEmpty())
	{
		::unlink(m_path.toLocal8Bit().constData());
		m_path.clear();
	}
#endif
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

void ControlServer::onClientReadable(int fd)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
#else
	const auto it = m_clients.find(fd);
	if (it == m_clients.end()) { return; }

	QByteArray& buffer = it->buffer;
	char chunk[4096];
	bool closed = false;
	while (true)
	{
		const ssize_t got = ::read(fd, chunk, sizeof(chunk));
		if (got > 0)
		{
			buffer.append(chunk, static_cast<int>(got));
			continue;
		}
		if (got == 0) { closed = true; }
		break; // EAGAIN or EOF
	}

	int newline = buffer.indexOf('\n');
	while (newline >= 0)
	{
		const QByteArray line = buffer.left(newline);
		buffer.remove(0, newline + 1);
		const QByteArray reply = dispatchLine(line);
		if (!reply.isEmpty())
		{
			writeAll(fd, reply + '\n');
		}
		newline = buffer.indexOf('\n');
	}

	if (closed)
	{
		dropClient(fd);
	}
#endif
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
