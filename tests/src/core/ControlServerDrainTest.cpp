/*
 * ControlServerDrainTest.cpp - the per-activation drain bound of ControlServer.
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
 * THE CONTRACT UNDER TEST: one readable activation of a client's socket reads
 * at most ControlServer::DrainBytesPerActivation bytes. A peer that keeps its
 * socket full must not hold the event loop inside one drain; whatever is left
 * waits for the next activation.
 *
 * The path is the real one: a ControlServer's QSocketNotifier -> onClientReadable
 * -> drainPeer, driven by a real AF_UNIX peer. One QCoreApplication::processEvents
 * call is one activation, and the bytes the peer can write right after it are the
 * bytes that activation consumed. There are no sleeps and no wall-clock deadlines:
 * every loop is capped in iterations.
 */

#include "ControlRegistry.h"
#include "ControlServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace lmms;

namespace
{

struct ReadyStateReset
{
	~ReadyStateReset() { ControlRegistry::setReady(false); }
};

#if defined(Q_OS_UNIX)

//! Upper bound on every loop in the test. A loop that never reaches its condition
//! fails the test; it does not wait on a clock.
constexpr int MaxActivations = 4096;
//! Upper bound on write attempts while filling one peer's send queue.
constexpr int MaxWritesPerFill = 1 << 16;
//! Bytes handed to each send() while filling; the kernel takes what it can.
constexpr int WriteChunkBytes = 64 * 1024;
//! Slack above the bound that one activation may consume (kernel accounting).
constexpr qint64 ConsumedSlackBytes = 8192;
//! Text the server writes back for an over-cap request line.
constexpr char RefusalText[] = "the request line exceeds";

#if defined(MSG_NOSIGNAL)
constexpr int SendFlags = MSG_NOSIGNAL;
#else
constexpr int SendFlags = 0;
#endif

struct TestSocket
{
	int fd = -1;
	~TestSocket() { if (fd >= 0) { ::close(fd); } }
};

struct WriteResult
{
	qint64 bytes = 0;
	int error = 0; // non-zero: a write failed for a reason other than a full queue
};

//! Writes unterminated data until the kernel refuses more. Never blocks.
WriteResult writeUntilWouldBlock(int fd)
{
	const QByteArray chunk(WriteChunkBytes, 'x');
	WriteResult result;
	for (int attempt = 0; attempt < MaxWritesPerFill; ++attempt)
	{
		const ssize_t n = ::send(fd, chunk.constData(), static_cast<size_t>(chunk.size()), SendFlags);
		if (n > 0)
		{
			result.bytes += n;
			continue;
		}
		if (n < 0 && errno == EINTR)
		{
			continue;
		}
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			result.error = errno;
		}
		return result;
	}
	return result;
}

#endif

} // namespace

class ControlServerDrainTest : public QObject
{
	Q_OBJECT
private Q_SLOTS:
	void drainIsBoundedPerActivation();
};

void ControlServerDrainTest::drainIsBoundedPerActivation()
{
#if !defined(Q_OS_UNIX)
	QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
	ControlRegistry::setReady(true);
	ReadyStateReset readyStateReset;
	ControlRegistry* registry = ControlRegistry::instance();

	// The socket path must fit sun_path (108 bytes on Linux). Use the harness temp
	// directory when it does; otherwise fall back to a short directory under /tmp.
	QTemporaryDir primaryDir(QDir::tempPath() + QStringLiteral("/zd-XXXXXX"));
	QVERIFY2(primaryDir.isValid(), "no temporary directory for the socket path");
	QString path = primaryDir.filePath(QStringLiteral("drain.sock"));
	std::optional<QTemporaryDir> fallbackDir;
	if (QFile::encodeName(path).size() >= 100)
	{
		fallbackDir.emplace(QStringLiteral("/tmp/zd-XXXXXX"));
		QVERIFY2(fallbackDir->isValid(), "no /tmp directory for the socket path");
		path = fallbackDir->filePath(QStringLiteral("drain.sock"));
	}

	ControlServer server(registry);
	QString error;
	QVERIFY2(server.listen(path, &error), qPrintable(error));

	TestSocket client;
	client.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	QVERIFY(client.fd >= 0);
	int sendBufferBytes = 4 * 1024 * 1024;
	QVERIFY(::setsockopt(client.fd, SOL_SOCKET, SO_SNDBUF, &sendBufferBytes, sizeof(sendBufferBytes)) == 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	const QByteArray nativePath = QFile::encodeName(path);
	QVERIFY(nativePath.size() < static_cast<qsizetype>(sizeof(address.sun_path)));
	std::memcpy(address.sun_path, nativePath.constData(), static_cast<size_t>(nativePath.size() + 1));
	QVERIFY(::connect(client.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
	const int flags = ::fcntl(client.fd, F_GETFL, 0);
	QVERIFY(flags >= 0);
	QVERIFY(::fcntl(client.fd, F_SETFL, flags | O_NONBLOCK) == 0);

	// (b) An unterminated request line past the cap. The server refuses it and
	// keeps draining the peer. Peek, do not read, so the refusal stays queued.
	const QByteArray refusal(RefusalText);
	bool refusalSeen = false;
	for (int i = 0; i < MaxActivations && !refusalSeen; ++i)
	{
		const WriteResult pushed = writeUntilWouldBlock(client.fd);
		QVERIFY2(pushed.error == 0, "a write to the control socket failed before the refusal");
		QCoreApplication::processEvents(QEventLoop::AllEvents);
		char peek[4096];
		const ssize_t peeked = ::recv(client.fd, peek, sizeof(peek), MSG_PEEK);
		refusalSeen = peeked > 0 && QByteArray(peek, static_cast<qsizetype>(peeked)).contains(refusal);
	}
	QVERIFY2(refusalSeen, "the server never refused the over-cap request line");

	// (c) Fill the peer's send queue with no activation in between.
	const WriteResult fill = writeUntilWouldBlock(client.fd);
	QVERIFY2(fill.error == 0, "a write to the control socket failed while filling");
	qInfo() << "fill bytes:" << fill.bytes
		<< "DrainBytesPerActivation:" << static_cast<int>(ControlServer::DrainBytesPerActivation);
	QVERIFY2(fill.bytes >= 2 * ControlServer::DrainBytesPerActivation,
		qPrintable(QStringLiteral("fill = %1 bytes; the test needs >= 2 * DrainBytesPerActivation = %2")
			.arg(fill.bytes).arg(2 * ControlServer::DrainBytesPerActivation)));

	// (d) One activation, then the peer refills the queue. The bytes it can write
	// after that are what the single activation consumed. (e) repeats it with the
	// peer still supplying data. After round 0 the queue is already full again, so
	// a separate refill step would add nothing; each later round is the activation
	// plus the refill that follows it.
	constexpr int Rounds = 4;
	for (int round = 0; round < Rounds; ++round)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents);
		const WriteResult consumed = writeUntilWouldBlock(client.fd);
		QVERIFY2(consumed.error == 0, "a write to the control socket failed after the activation");
		qInfo() << "round" << round << "consumed bytes:" << consumed.bytes;
		QVERIFY2(consumed.bytes > 0, "one activation consumed nothing from a full queue");
		QVERIFY2(consumed.bytes <= ControlServer::DrainBytesPerActivation + ConsumedSlackBytes,
			qPrintable(QStringLiteral(
				"round %1: consumed = %2 bytes in ONE activation; limit = DrainBytesPerActivation (%3) + %4")
				.arg(round).arg(consumed.bytes)
				.arg(ControlServer::DrainBytesPerActivation).arg(ConsumedSlackBytes)));
	}

	// (f) Normal path: the peer finishes its write side. The server keeps draining
	// until the FIN, then drops the client. The refusal must still be readable,
	// and the end must be a clean EOF, never ECONNRESET.
	QVERIFY(::shutdown(client.fd, SHUT_WR) == 0);
	QByteArray received;
	bool sawEof = false;
	int receiveError = 0;
	for (int i = 0; i < MaxActivations && !sawEof && receiveError == 0; ++i)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents);
		char buffer[4096];
		const ssize_t n = ::recv(client.fd, buffer, sizeof(buffer), 0);
		if (n > 0)
		{
			received.append(buffer, static_cast<qsizetype>(n));
		}
		else if (n == 0)
		{
			sawEof = true;
		}
		else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
		{
			receiveError = errno;
		}
	}
	QVERIFY2(receiveError != ECONNRESET, "the server reset the connection instead of closing it cleanly");
	QVERIFY2(receiveError == 0, qPrintable(QStringLiteral("recv failed with errno %1").arg(receiveError)));
	QVERIFY2(sawEof, "the server never closed the connection after the peer's FIN");
	QVERIFY2(received.contains(refusal), "the refusal line was not readable before EOF");
#endif
}

QTEST_GUILESS_MAIN(ControlServerDrainTest)
#include "ControlServerDrainTest.moc"
