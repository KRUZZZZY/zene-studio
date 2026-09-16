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

#include "ControlRegistry.h"
#include "ControlServer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace lmms;

class ControlShutdownHookTest : public QObject
{
	Q_OBJECT
private slots:

	void init()
	{
		// Each case starts from an empty hook store. Draining is safe: the hooks
		// this test registers are counters it owns, and a case that failed
		// before its own runShutdownHooks() must not leak into the next one.
		ControlRegistry* registry = ControlRegistry::instance();
		registry->runShutdownHooks();
		QCOMPARE(registry->shutdownHookCount(), 0);
	}

	//! The first half: the hook survives a registry instance it was not
	//! registered on. destroy() then instance() is exactly the sequence that
	//! used to lose it.
	void aHookSurvivesTheRegistryInstanceItWasRegisteredOn()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int ran = 0;
		const ControlRegistry::ShutdownHookId id = registry->addShutdownHook([&ran]() { ++ran; });
		QVERIFY2(id != 0, "the first registered hook was given the 'no hook' id");
		QCOMPARE(registry->shutdownHookCount(), 1);

		// The instance that comes back must be a NEW object, and the observable
		// is the destroyed instance's own state - NOT its address. `new` is
		// allowed to hand back the bytes `delete` has just freed, and on
		// linux-x86_64 run 35126160372 it did: the pointer comparison this slot
		// used to make reported "destroy() did not build a new instance for
		// instance()" on CI while every other fact in that run shows the
		// recreation happening (the hook count is 1 over a store that outlives
		// the instance, and the hook is run below by the instance that came
		// back). A rebuilt registry cannot carry the destroyed one's marker; an
		// instance() that did NOT destroy-and-rebuild - the failure this case is
		// here for - cannot lose it.
		const QString marker = QStringLiteral("registered-on-the-destroyed-instance");
		registry->setObjectName(marker);
		QCOMPARE(registry->objectName(), marker);   // the marker is observable at all

		ControlRegistry::destroy();
		ControlRegistry* recreated = ControlRegistry::instance();
		QVERIFY2(recreated->objectName() != marker,
			"destroy() did not build a new instance for instance(): the registry it "
			"handed back still carries the state of the one that was destroyed");

		// The guard's own call, on the re-created instance, with no argument
		// pointing at the old one: the hook still has to run.
		recreated->runShutdownHooks();
		QCOMPARE(ran, 1);
		QCOMPARE(recreated->shutdownHookCount(), 0);
	}

	//! The second half: the owner's death takes the hook with it, and the socket
	//! file is gone either way (close() removes it in the same destructor).
	void theOwnerTakesItsHookWithItWhenItDies()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("the control socket is AF_UNIX only; listen() refuses on this platform");
#else
		ControlRegistry* registry = ControlRegistry::instance();
		QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/zene-hook-XXXXXX"));
		QVERIFY2(dir.isValid(), "no temporary directory for the socket path");
		const QString path = dir.filePath(QStringLiteral("shutdown-hook.sock"));

		{
			ControlServer server(registry);
			QString error;
			QVERIFY2(server.listen(path, &error), qPrintable(error));
			QVERIFY2(server.isListening(), "listen() succeeded without a listener");
			QVERIFY2(QFile::exists(path), "listen() bound no socket file");
			QCOMPARE(registry->shutdownHookCount(), 1);
		}

		// The server is gone. Its hook closed over `this`, so it must be gone
		// too: a hook left behind is a call on freed memory, and the shutdown
		// path is the one route where it might be that hook alone.
		QCOMPARE(registry->shutdownHookCount(), 0);
		QVERIFY2(!QFile::exists(path),
			"the server died and left its socket file behind - the shutdown contract is "
			"\"gone on exit\", and a live server's close() removes it");
		// And the shutdown path still runs cleanly with nothing registered.
		registry->runShutdownHooks();
#endif
	}

	//! Removal is targeted, ids are never reused, and running twice runs once:
	//! the store is the process's, so a second pass must not re-run what the
	//! first already did.
	void removalIsTargetedAndTheStoreIsDrainedOnce()
	{
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

	//! The socket path the hook exists to remove: with the server alive, the
	//! shutdown path (not the destructor) is what unlinks it. This is the
	//! behaviour main.cpp's normal exit and the last-resort guard both rely on.
	void theShutdownPathUnlinksALiveServersSocket()
	{
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
};

QTEST_GUILESS_MAIN(ControlShutdownHookTest)
#include "ControlShutdownHookTest.moc"
