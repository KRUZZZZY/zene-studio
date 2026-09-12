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

//! socket() + bind() for an absolute path, or -1 with \p error set. It unlinks
//! the path ONLY when \p unlinkStale — the caller has established with lstat()
//! that what is there is a socket left by a crashed instance. Nothing else is
//! ever removed: the previous version unlinked whatever it found, which is how
//! `--control-socket ~/my-song.mmp` destroyed that project (see
//! docs/CONTROL-SOCKET-PATH-SAFETY.md).
int openBoundSocket(const QByteArray& nativePath, bool unlinkStale, QString* error)
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

	if (unlinkStale)
	{
		::unlink(address.sun_path);
	}
	if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
	{
		if (error) { *error = QString::fromLocal8Bit(std::strerror(errno)); }
		::close(fd);
		return -1;
	}
	return fd;
}

//! What the path handed to --control-socket already holds.
enum class PathState
{
	Free,        //!< nothing there: bind normally
	StaleSocket, //!< a socket a crashed instance left behind: unlink, then bind
	Conflict,    //!< a regular file, a device, a FIFO or a symlink: REFUSE
	Directory,   //!< a directory: it can never be a socket path
};

//! Classify \p nativePath for the bind WITHOUT following a symlink (lstat, not
//! stat), describing what was found in \p found and any lstat() failure in
//! \p problem. lstat is deliberate: unlink() on a symlink deletes the LINK, not
//! its target, so a symlink is never treated as a stale socket even when it
//! points at one.
PathState classifySocketPath(const char* nativePath, QString* found, QString* problem)
{
	struct stat info;
	if (::lstat(nativePath, &info) != 0)
	{
		if (errno == ENOENT) { return PathState::Free; }
		if (problem) { *problem = QString::fromLocal8Bit(std::strerror(errno)); }
		return PathState::Conflict;
	}
	if (S_ISSOCK(info.st_mode)) { return PathState::StaleSocket; }
	if (S_ISDIR(info.st_mode))
	{
		if (found) { *found = QStringLiteral("a directory"); }
		return PathState::Directory;
	}
	if (found)
	{
		if (S_ISLNK(info.st_mode)) { *found = QStringLiteral("a symbolic link"); }
		else if (S_ISREG(info.st_mode)) { *found = QStringLiteral("a regular file"); }
		else if (S_ISFIFO(info.st_mode)) { *found = QStringLiteral("a FIFO"); }
		else if (S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode)) { *found = QStringLiteral("a device node"); }
		else { *found = QStringLiteral("not a socket"); }
	}
	return PathState::Conflict;
}

//! The refusal a launcher can read when there is no socket to answer on: the
//! surface's OWN typed error, in the exact shape the protocol uses on the wire
//! ({"id":-1,"ok":false,"error":{"kind":"...","message":"..."}}; -1 is the id
//! the server itself gives a reply that belongs to no request), on stderr.
void reportTypedError(ControlErrorKind kind, const QString& message)
{
	QJsonObject error;
	error.insert(QStringLiteral("kind"), controlErrorKindName(kind));
	error.insert(QStringLiteral("message"), message);
	QJsonObject reply;
	reply.insert(QStringLiteral("id"), -1);
	reply.insert(QStringLiteral("ok"), false);
	reply.insert(QStringLiteral("error"), error);
	const QByteArray line = QJsonDocument(reply).toJson(QJsonDocument::Compact);
	// "%s": the message is a path, which may contain '%'.
	qWarning("control socket: %s", line.constData());
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
	m_lastErrorKind = ControlErrorKind::None;
	// Every failure below is REPORTED twice: as the string the caller prints and
	// as the surface's typed error on stderr, because a launcher whose instance
	// refused to start has no socket to send a request to
	// (docs/CONTROL-SOCKET-PATH-SAFETY.md).
	const auto fail = [this, error](ControlErrorKind kind, const QString& message) {
		m_lastErrorKind = kind;
		if (error) { *error = message; }
		reportTypedError(kind, message);
		return false;
	};

	if (path.isEmpty() || !path.startsWith(QLatin1Char('/')))
	{
		return fail(ControlErrorKind::InvalidArgs,
			QStringLiteral("the control socket path must be absolute"));
	}
	if (isListening())
	{
		return fail(ControlErrorKind::InvalidArgs,
			QStringLiteral("already listening on %1").arg(m_path));
	}

#if !defined(Q_OS_UNIX)
	Q_UNUSED(path);
	return fail(ControlErrorKind::InvalidArgs,
		QStringLiteral("the control socket is supported on POSIX platforms only"));
#else
	const QByteArray nativePath = path.toLocal8Bit();
	if (nativePath.size() >= static_cast<int>(sizeof(sockaddr_un::sun_path)))
	{
		return fail(ControlErrorKind::InvalidArgs,
			QStringLiteral("the control socket path is too long (%1 bytes; the limit is %2)")
				.arg(nativePath.size()).arg(sizeof(sockaddr_un::sun_path) - 1));
	}

	// The bind UNLINKS the path. Before it does, establish what is there:
	//   - nothing            -> bind normally;
	//   - a socket file      -> a stale socket from a crashed instance: unlinking
	//                           it is the only way to bind, so do it, and say so;
	//   - a directory        -> a socket can never live at a directory's path, so
	//                           the argument itself is wrong (invalid_args) and
	//                           nothing is or was at risk of being deleted;
	//   - anything else      -> REFUSE (refused): unlinking would destroy it.
	QString found;
	QString examineError;
	const PathState state = classifySocketPath(nativePath.constData(), &found, &examineError);
	if (state == PathState::Conflict)
	{
		if (!examineError.isEmpty())
		{
			return fail(ControlErrorKind::Refused,
				QStringLiteral("refusing to use %1 as the control socket: it could not be "
					"examined (%2)").arg(path, examineError));
		}
		return fail(ControlErrorKind::Refused,
			QStringLiteral("refusing to use %1 as the control socket: %2 already exists there, it is "
				"not a socket, and starting the server would destroy it; remove it yourself or pass "
				"a different path").arg(path, found));
	}
	if (state == PathState::Directory)
	{
		return fail(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is a directory, and a directory can never be a control socket path "
				"(nothing was deleted); pass the path of a socket file inside it").arg(path));
	}
	if (state == PathState::StaleSocket)
	{
		qWarning("control socket: unlinking the stale socket file %s left by an earlier instance",
			qPrintable(path));
	}

	QString detail;
	const int fd = openBoundSocket(nativePath, state == PathState::StaleSocket, &detail);
	if (fd < 0) { return fail(ControlErrorKind::Refused, detail); }
	if (!pinAndListen(fd, nativePath.constData(), &detail))
	{
		::close(fd);
		::unlink(nativePath.constData());
		return fail(ControlErrorKind::Refused, detail);
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

	if (it->draining)
	{
		// The rest of an over-cap request line: read it and keep none of it, so the
		// close below is a clean FIN (see Client::draining).
		char sink[8192];
		while (true)
		{
			const ssize_t got = ::read(fd, sink, sizeof(sink));
			if (got > 0) { continue; }
			if (got < 0 && errno == EINTR) { continue; }
			if (got == 0) { dropClient(fd); }
			return; // EAGAIN: wait for the next activation
		}
	}

	QByteArray& buffer = it->buffer;
	char chunk[4096];
	bool closed = false;
	while (true)
	{
		const ssize_t got = ::read(fd, chunk, sizeof(chunk));
		if (got > 0)
		{
			buffer.append(chunk, static_cast<int>(got));
			// Stop reading once the pending bytes pass the cap: a client that never
			// sends a newline must not be able to grow this buffer (and the
			// instance's heap) without bound. Complete lines are dispatched below,
			// so what is left here is the unterminated tail.
			if (buffer.size() > MaxRequestLineBytes) { break; }
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
		if (!reply.isEmpty() && !writeAll(fd, reply + '\n'))
		{
			// A failed write leaves a TRUNCATED line on the wire: this socket is
			// non-blocking, so it can take part of a line and then EAGAIN. Write
			// nothing else on this connection and drop it, so the peer reads EOF
			// after a partial line instead of the next reply's bytes glued to it.
			dropClient(fd);
			return;
		}
		newline = buffer.indexOf('\n');
	}

	// Every complete line is gone, so a buffer still over the cap is ONE request
	// line that never ended. Refuse it in the surface's own vocabulary, then
	// retire the connection: resynchronising would mean buffering the rest of a
	// line of unknown length, which is exactly what the cap refuses to do.
	if (buffer.size() > MaxRequestLineBytes)
	{
		const QByteArray refusal = errorLine(-1, ControlErrorKind::InvalidArgs,
			QStringLiteral("the request line exceeds the %1-byte limit and was refused")
				.arg(MaxRequestLineBytes));
		// The refusal goes out BEFORE the drain, and the drain is why it arrives:
		// see Client::draining.
		writeAll(fd, refusal + '\n');
		it->draining = true;
		buffer.clear();
		buffer.squeeze();
		if (closed)
		{
			// EOF is already in hand and everything read has been discarded: the
			// close is clean.
			dropClient(fd);
		}
		return;
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
