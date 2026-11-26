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

// ---------------------------------------------------------------------------------------
// BUG 15 (cluster 3118): the accept thread is retired before the next one starts
// ---------------------------------------------------------------------------------------
//
// std::thread's move assignment calls std::terminate() when the target is still
// joinable, and a body that returned on its own leaves its std::thread joinable
// until someone joins it. The Windows acceptor is exactly that shape: it breaks
// out of its loop when CreateNamedPipeW fails, clears m_win32Listening on the way
// out, and leaves the joinable thread owned by the server. Assigning the next
// acceptor over it - the pre-fix `m_acceptThread = std::thread(...)` in
// listenWin32() - therefore ended the process on the retry path.
//
// RestartableThread owns that join. This case drives it through the sequence the
// transport uses, including 64 restarts with no join in between - the accept
// loop's failure-and-retry path. No timing decides a result: every body signals a
// semaphore as its last statement and the test waits for that signal. The 30 s
// cap only turns a body that never ran into a failure with a message instead of a
// hang.
void ControlShutdownHookTest::aFinishedAcceptThreadIsJoinedBeforeTheNextStart() {
#if defined(LMMS_HAVE_RESTARTABLE_THREAD)
	RestartableThread acceptThread;
	// close(), closeWin32() and the destructor all call it, the first two on a
	// server that may never have listened.
	acceptThread.join();
	QVERIFY2(!acceptThread.joinable(), "joining nothing made the thread joinable");

	QSemaphore firstBodyRan;
	acceptThread.start([&firstBodyRan]() { firstBodyRan.release(); });
	QVERIFY2(firstBodyRan.tryAcquire(1, 30000), "the first accept body never ran");
	// The body RETURNED on its own - the state a failed acceptor leaves behind.
	// Its std::thread is still joinable, so the next start has to retire it
	// instead of assigning over it.
	QVERIFY2(acceptThread.joinable(),
		"a body that returned left no thread to join: the hazard state was not reached");

	QSemaphore secondBodyRan;
	acceptThread.start([&secondBodyRan]() { secondBodyRan.release(); });  // pre-fix: std::terminate here
	QVERIFY2(secondBodyRan.tryAcquire(1, 30000), "the retried listen body never ran");

	std::atomic<int> bodies{0};
	for (int attempt = 0; attempt < 64; ++attempt)
	{
		QSemaphore ran;
		// No join between attempts: each start() is the one that has to retire the
		// previous, already-finished body - the accept-loop failure and retry path.
		acceptThread.start([&ran, &bodies]() {
			bodies.fetch_add(1);
			ran.release();
		});
		QVERIFY2(ran.tryAcquire(1, 30000), "an accept body did not run");
	}
	acceptThread.join();
	acceptThread.join();  // idempotent, like close() on a listener that already stopped
	QVERIFY2(!acceptThread.joinable(), "the second join made the accept thread joinable");
	QCOMPARE(bodies.load(), 64);

	std::atomic<int> shortLivedBodies{0};
	{
		// The owner's death retires a body that already returned: a destructor that
		// destroyed a still-joinable thread would abort this case.
		RestartableThread shortLived;
		QSemaphore ran;
		shortLived.start([&ran, &shortLivedBodies]() {
			shortLivedBodies.fetch_add(1);
			ran.release();
		});
		QVERIFY2(ran.tryAcquire(1, 30000), "the short-lived body never ran");
	}
	QCOMPARE(shortLivedBodies.load(), 1);
#else
	// The seam is not in this tree: this is the pre-fix shape of the very defect
	// the finding names. A body returns on its own (its std::thread stays
	// joinable) and the next body is moved into the same std::thread object.
	std::thread acceptThread;
	QSemaphore firstBodyRan;
	acceptThread = std::thread([&firstBodyRan]() { firstBodyRan.release(); });
	QVERIFY2(firstBodyRan.tryAcquire(1, 30000), "the first accept body never ran");
	QVERIFY2(acceptThread.joinable(),
		"a body that returned left no thread to join: the hazard state was not reached");

	acceptThread = std::thread([]() {});  // BUG 15: std::terminate, not an exception
	QFAIL("moving a new std::thread over the finished acceptor did not end the process");
#endif
}
