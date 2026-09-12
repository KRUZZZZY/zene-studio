/*
 * ControlServerSocket.cpp - the bind/close lifecycle of the control socket.
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

// The path-safety half of ControlServer: what the path handed to
// --control-socket already holds, whether it may be bound at all, and the inode
// bookkeeping that lets close() unlink only the socket THIS instance bound.
// Split out of ControlServer.cpp, which keeps the wire half (accept, framing,
// dispatch); both halves work on the same private members, so the member
// functions defined here are declared in ControlServer.h. See
// docs/CONTROL-SOCKET-PATH-SAFETY.md.

#include "ControlServer.h"

#include <cerrno>
#include <cstring>

#include <QJsonDocument>
#include <QJsonObject>
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

//! True when something is LISTENING at \p nativePath.
//!
//! A socket file at the path is only evidence that a socket was once created
//! there. `connect()` is what separates a crashed run's leftover from a live
//! listener: a stale socket refuses the connection (`ECONNREFUSED`), a live one
//! accepts it. The probe connection is closed immediately; the listener sees a
//! client that connected and went away, which is what a liveness probe looks like
//! — this repo's own `tests/control_socket_harness.py` waits for a socket the same
//! way.
//!
//! Only `ECONNREFUSED` (and `ENOENT`: the socket vanished under us) mean "nothing
//! is listening". EVERY other outcome is treated as LIVE, because the two ways to
//! be wrong are not symmetric: refusing to start costs an exit code, while
//! unlinking a live socket costs a running program its control channel:
//!   * `EACCES` — a socket owned by another user (`/var/run/docker.sock`, an X11
//!     socket, an ssh-agent's): that is a live foreign socket, not our leftover;
//!   * `EAGAIN`/`EINPROGRESS` — a listener whose accept backlog is full;
//!   * `socket()` failing at all — we cannot even probe.
//! The probe is non-blocking for the same reason: a blocking `connect()` to a
//! listener with a full backlog would HANG the instance at start-up.
bool socketHasLiveListener(const char* nativePath)
{
	const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { return true; }
	::fcntl(fd, F_SETFD, FD_CLOEXEC);
	::fcntl(fd, F_SETFL, O_NONBLOCK);

	sockaddr_un address;
	std::memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, nativePath, std::strlen(nativePath));

	const int connected = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
	const int probeErrno = errno;
	::close(fd);
	if (connected == 0) { return true; }
	return probeErrno != ECONNREFUSED && probeErrno != ENOENT;
}

//! What the path handed to --control-socket already holds.
enum class PathState
{
	Free,        //!< nothing there: bind normally
	StaleSocket, //!< a socket nothing is listening on: unlink, then bind
	LiveSocket,  //!< a socket something IS listening on: REFUSE, touch nothing
	Conflict,    //!< a regular file, a device, a FIFO or a symlink: REFUSE
	Directory,   //!< a directory: it can never be a socket path
};

//! What a refusal calls the non-socket thing standing at the path.
QString describeNotASocket(const struct stat& info)
{
	if (S_ISLNK(info.st_mode)) { return QStringLiteral("a symbolic link"); }
	if (S_ISREG(info.st_mode)) { return QStringLiteral("a regular file"); }
	if (S_ISFIFO(info.st_mode)) { return QStringLiteral("a FIFO"); }
	if (S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode)) { return QStringLiteral("a device node"); }
	return QStringLiteral("not a socket");
}

//! lstat() \p nativePath — never following a symlink — into \p info. Returns
//! false when it failed, reporting in \p missing whether the path simply is not
//! there and, for every other failure, the reason in \p problem.
bool statSocketPath(const char* nativePath, struct stat* info, bool* missing, QString* problem)
{
	if (::lstat(nativePath, info) == 0) { return true; }
	*missing = errno == ENOENT;
	if (!*missing && problem) { *problem = QString::fromLocal8Bit(std::strerror(errno)); }
	return false;
}

//! Classify \p nativePath for the bind WITHOUT following a symlink (lstat, not
//! stat), describing what was found in \p found and any lstat() failure in
//! \p problem. lstat is deliberate: unlink() on a symlink deletes the LINK, not
//! its target, so a symlink is never treated as a stale socket even when it
//! points at one.
PathState classifySocketPath(const char* nativePath, QString* found, QString* problem)
{
	struct stat info;
	bool missing = false;
	if (!statSocketPath(nativePath, &info, &missing, problem))
	{
		return missing ? PathState::Free : PathState::Conflict;
	}
	if (S_ISSOCK(info.st_mode))
	{
		if (found) { *found = QStringLiteral("a socket"); }
		return socketHasLiveListener(nativePath) ? PathState::LiveSocket : PathState::StaleSocket;
	}
	if (S_ISDIR(info.st_mode))
	{
		if (found) { *found = QStringLiteral("a directory"); }
		return PathState::Directory;
	}
	if (found) { *found = describeNotASocket(info); }
	return PathState::Conflict;
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

//! Decide whether the path may be bound. Returns false with the typed refusal in
//! \p kind and \p message when the path must be left alone; on true,
//! \p unlinkStale says a crashed instance's socket is there and has to be
//! removed first.
//!
//! The refusals are the surface's own vocabulary, and each is decided BEFORE the
//! destructive bind: a stale socket is unlinked and used, a live socket is
//! refused (unlinking it would leave that instance reachable by nobody while it
//! still believes it is listening — docs/CONTROL-SURFACE-FUZZ.md F1/F2), a
//! directory is an argument error (invalid_args: nothing was ever at risk of
//! being deleted), and anything else is refused (refused: unlinking would
//! destroy it).
bool mayBindPath(const QString& path, const char* nativePath, bool* unlinkStale,
	ControlErrorKind* kind, QString* message)
{
	QString found;
	QString examineError;
	const PathState state = classifySocketPath(nativePath, &found, &examineError);
	if (state == PathState::LiveSocket)
	{
		*kind = ControlErrorKind::Refused;
		*message = QStringLiteral("refusing to use %1 as the control socket: a live socket is already "
			"listening there (another instance, or another program); pass a different path, or "
			"stop that process first").arg(path);
		return false;
	}
	if (state == PathState::Conflict)
	{
		*kind = ControlErrorKind::Refused;
		*message = examineError.isEmpty()
			? QStringLiteral("refusing to use %1 as the control socket: %2 already exists there, it is "
				"not a socket, and starting the server would destroy it; remove it yourself or pass "
				"a different path").arg(path, found)
			: QStringLiteral("refusing to use %1 as the control socket: it could not be "
				"examined (%2)").arg(path, examineError);
		return false;
	}
	if (state == PathState::Directory)
	{
		*kind = ControlErrorKind::InvalidArgs;
		*message = QStringLiteral("%1 is a directory, and a directory can never be a control socket path "
			"(nothing was deleted); pass the path of a socket file inside it").arg(path);
		return false;
	}
	if (state == PathState::StaleSocket)
	{
		qWarning("control socket: unlinking the stale socket file %s left by an earlier instance",
			qPrintable(path));
		*unlinkStale = true;
	}
	return true;
}

//! What close() should do with the path it bound.
enum class BoundInode
{
	Absent,   //!< nothing is there (already unlinked): nothing to do
	Ours,     //!< the very inode this instance bound: unlink it
	Replaced, //!< a DIFFERENT file is there now: leave it alone and say so
};

//! Which of the three the path holds NOW, read with lstat() so a symlink is
//! never mistaken for the socket this instance bound.
BoundInode boundInodeState(const char* nativePath, quint64 device, quint64 inode)
{
	struct stat info;
	if (::lstat(nativePath, &info) != 0) { return BoundInode::Absent; }
	if (static_cast<quint64>(info.st_dev) == device
		&& static_cast<quint64>(info.st_ino) == inode)
	{
		return BoundInode::Ours;
	}
	return BoundInode::Replaced;
}

} // namespace
#endif

// ---- the two helpers listen() needs on EVERY platform --------------------------------
// The socket itself is POSIX-only, but the refusal path is not: listen() validates its
// path and reports a typed error BEFORE it reaches the #if !defined(Q_OS_UNIX) branch
// below. Both helpers it uses for that lived inside the Q_OS_UNIX block above, so on
// Windows MSVC compiled those call sites with no definitions in scope and failed the
// release build:
//   ControlServerSocket.cpp(338): error C3861: 'reportTypedError': identifier not found
//   ControlServerSocket.cpp(342): error C3861: 'pathValidationError': identifier not found
// gcc and clang never saw it: Q_OS_UNIX is defined on both, so the definitions were in
// scope for every local build and every other CI job. Neither helper uses a POSIX
// interface -- one builds a JSON line for stderr, the other is pure QString -- so they
// belong outside the guard, where every platform that compiles listen() can see them.
namespace
{

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

//! The invalid_args refusal for a path listen() cannot accept, or an empty
//! string when the path is usable. \p listening is whether this instance already
//! holds a listener, and \p bound is the path it holds.
QString pathValidationError(const QString& path, bool listening, const QString& bound)
{
	if (path.isEmpty() || !path.startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("the control socket path must be absolute");
	}
	if (listening) { return QStringLiteral("already listening on %1").arg(bound); }
	return QString();
}

} // namespace

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

	const QString pathError = pathValidationError(path, isListening(), m_path);
	if (!pathError.isEmpty())
	{
		return fail(ControlErrorKind::InvalidArgs, pathError);
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

	// The bind UNLINKS the path. Establish what is there first (mayBindPath):
	//   - nothing            -> bind normally;
	//   - a stale socket     -> a socket NOTHING is listening on (a crashed
	//                           instance's leftover): unlinking it is the only way
	//                           to bind, so do it, and say so;
	//   - a live socket      -> someone IS listening there: REFUSE, touch nothing;
	//   - a directory        -> invalid_args: a socket can never live at a
	//                           directory's path, and nothing was at risk;
	//   - anything else      -> REFUSED: unlinking would destroy it.
	ControlErrorKind refusalKind = ControlErrorKind::None;
	QString refusal;
	bool unlinkStale = false;
	if (!mayBindPath(path, nativePath.constData(), &unlinkStale, &refusalKind, &refusal))
	{
		return fail(refusalKind, refusal);
	}

	QString detail;
	const int fd = openBoundSocket(nativePath, unlinkStale, &detail);
	if (fd < 0) { return fail(ControlErrorKind::Refused, detail); }
	if (!pinAndListen(fd, nativePath.constData(), &detail))
	{
		::close(fd);
		::unlink(nativePath.constData());
		return fail(ControlErrorKind::Refused, detail);
	}
	return adoptListener(fd, path, nativePath);
#endif
}

bool ControlServer::adoptListener(int fd, const QString& path, const QByteArray& nativePath)
{
#if !defined(Q_OS_UNIX)
	Q_UNUSED(fd);
	Q_UNUSED(path);
	Q_UNUSED(nativePath);
	return false;
#else
	m_listenFd = fd;
	m_path = path;
	// Remember WHICH inode this instance bound, so close() can refuse to unlink a
	// path that no longer holds it: if the socket at the path has been replaced
	// while we were listening, the replacement belongs to someone else, and
	// removing it would leave that instance listening where nothing can reach it
	// (docs/CONTROL-SURFACE-FUZZ.md F1).
	struct stat bound;
	if (::lstat(nativePath.constData(), &bound) == 0)
	{
		m_boundDevice = static_cast<quint64>(bound.st_dev);
		m_boundInode = static_cast<quint64>(bound.st_ino);
	}
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
		const QByteArray nativePath = m_path.toLocal8Bit();
		const BoundInode state = boundInodeState(nativePath.constData(), m_boundDevice, m_boundInode);
		if (state == BoundInode::Ours)
		{
			::unlink(nativePath.constData());
		}
		else if (state == BoundInode::Replaced)
		{
			// The path was replaced while we were listening: that socket is somebody
			// else's, and unlinking it would leave them listening where nothing can
			// reach them (docs/CONTROL-SURFACE-FUZZ.md F1). Say so instead.
			qWarning("control socket: not unlinking %s: the path now holds a different file "
				"(it was replaced while this instance was listening)",
				qPrintable(m_path));
		}
		m_path.clear();
	}
#endif
}

} // namespace lmms
