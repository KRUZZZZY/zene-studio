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

// Complete fixture method definitions; included once after the fixture declaration.

void ControlShutdownHookTest::removalIsTargetedAndTheStoreIsDrainedOnce() {
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
void ControlShutdownHookTest::theShutdownPathUnlinksALiveServersSocket() {
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
void ControlShutdownHookTest::aClientDeadlineIsAnchoredToTheMonotonicClock() {
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
