/*
 * OutOfProcessHostClientLoopTest.cpp - the whole out-of-process loop on a REAL
 *                                      client process (feature row 80, board
 *                                      card #670)
 *
 * The sibling of OutOfProcessHostTest, in its own binary because the file-length
 * ratchet is not moved for a new feature (the ClipLink / ClipLinkPersistence,
 * ClipLink / ClipPersistence and SmfInterchange pairs in this directory are the
 * same shape). This file holds the case that needs two executables: the
 * ZynAddSubFx module and the RemoteZynAddSubFx client.
 *
 * THE LOOP, and every step of it goes through the SURFACE:
 *
 *   1. oop.set_mode(..., "separate-process") hosts the instrument out of
 *      process; the pid that appears comes back out of oop.get_state.
 *   2. the client is SIGKILLed. The HOST IS THE PROCESS RUNNING THIS TEST, so
 *      "the host survived" is not an assertion that can fail silently - every
 *      round after the kill runs in the very process the crash had to take down.
 *   3. the death is counted (under the CLIENT EXECUTABLE's name) and the slot
 *      reports client-exited through the same surface.
 *   4. oop.restart re-hosts it: a new client pid, and the plugin's own patch is
 *      carried over by the plugin's reload.
 *   5. at the build's bound (3 deaths in one session) oop.restart and
 *      oop.set_mode REFUSE, typed, naming the client, the count and the last
 *      exit code - and the device reports `refused-crash-loop` rather than
 *      looking healthy.
 *   6. oop.reset_crashes is the only thing that lifts it, and the slot is
 *      drivable again.
 *
 * WHAT IS PROVEN, and what a green run does NOT say: no plugin is crashed by its
 * own bug - the client is killed from outside, so the isolation shown is "a dead
 * client cannot take the host down, and the build notices, counts and refuses to
 * keep re-hosting it", the mechanism a real crash trips with an external
 * trigger. The audio of a killed slot is NOT compared here (that is
 * docs/OOP-HOSTING.md's render comparison and the registered
 * ZynSeparateProcessTest); this test never renders. Where this build has no
 * ZynAddSubFx module or no client executable, the case reports ctest *Skipped* -
 * never a pass - and the code it could not exercise is stated in the skip.
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

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QtTest>

#include <memory>

#ifndef Q_OS_WIN
#include <signal.h>
#include <unistd.h>
#endif

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "OutOfProcessHostSupport.h"
#include "OutOfProcessHosting.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::oop;
using namespace ooptest;

class OutOfProcessHostClientLoopTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		m_engineInitialised = true;
		QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		if (m_engineInitialised) { Engine::destroy(); }
	}

	//! The loop (see the file header). Skipped whole where this build has no
	//! ZynAddSubFx module or no RemoteZynAddSubFx client - never passed.
	void aRealClientIsHostedKilledNoticedAndRefused()
	{
#ifdef Q_OS_WIN
		QSKIP("cannot load a plugin module from a Windows test host (plugin modules link the zene "
			"executable, so their import descriptor names zene.exe - see AudioPluginTest.cpp), and "
			"::kill is not available: the client loop is UNEXERCISED by this run");
#else
		const QString module = zynModulePath();
		const QString client = zynClientPath();
		if (module.isEmpty() || !QFile::exists(module) || client.isEmpty() || !QFile::exists(client))
		{
			QSKIP("this build has no ZynAddSubFx module or no RemoteZynAddSubFx client: the "
				"out-of-process loop through the surface is UNEXERCISED by this run");
		}

		// RemotePlugin::init() resolves the client through the "plugins:" search
		// path, so the build's plugin directory is where the client is found.
		qputenv("LMMS_PLUGIN_DIR", QFileInfo{client}.absolutePath().toUtf8());

		// The REAL load path (PluginFactory -> the module's descriptor), the same
		// one a project load takes, because this case has to end up with an
		// instrument the track's own `inst` address resolves to.
		auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
		track->loadInstrument(QStringLiteral("zynaddsubfx"));
		Instrument* instrument = track->instrument();
		if (instrument == nullptr
			|| QLatin1String(instrument->descriptor()->name) != QLatin1String("zynaddsubfx"))
		{
			QSKIP("the ZynAddSubFx module could not be loaded into this test host: the "
				"out-of-process loop through the surface is UNEXERCISED by this run");
		}

		ControlRegistry* registry = ControlRegistry::instance();
		const QJsonObject address{
			{QStringLiteral("target"), control::trackIdOf(track.get())},
			{QStringLiteral("plugin"), QStringLiteral("inst")}};

		// --- host it out of process, through the surface -------------------
		const ControlResult mode = registry->invoke(QStringLiteral("oop.set_mode"), QJsonObject{
			{QStringLiteral("target"), address.value(QStringLiteral("target")).toString()},
			{QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("mode"), QStringLiteral("separate-process")}});
		QVERIFY2(mode.ok, qPrintable(mode.errorMessage));
		QCOMPARE(mode.result.value(QStringLiteral("mode_after")).toString(),
			QStringLiteral("separate-process"));
		QCOMPARE(mode.result.value(QStringLiteral("changed")).toBool(), true);

		QTRY_VERIFY_WITH_TIMEOUT(liveClientPid(registry, address) > 0, 15000);
		const qint64 firstPid = liveClientPid(registry, address);
		QVERIFY2(firstPid != static_cast<qint64>(QCoreApplication::applicationPid()),
			"the client pid is this process's own: it is not a separate process");
		qInfo("hosted out of process: client pid %lld, host pid %lld",
			static_cast<long long>(firstPid),
			static_cast<long long>(QCoreApplication::applicationPid()));

		// --- kill it once per crash the bound allows -----------------------
		const int bound = HostTracker::maxCrashesPerClient();
		qint64 pid = firstPid;
		for (int round = 1; round <= bound; ++round)
		{
			killAndCount(registry, address, pid, round);
			if (round == bound) { break; }

			// oop.restart through the surface, and the new pid it produced. The
			// assertions stay in this slot: QVERIFY/QCOMPARE expand to a bare
			// `return;`, which a helper returning qint64 cannot compile.
			const ControlResult restarted = registry->invoke(QStringLiteral("oop.restart"), address);
			QVERIFY2(restarted.ok, qPrintable(restarted.errorMessage));
			QCOMPARE(restarted.result.value(QStringLiteral("restarted")).toBool(), true);
			QTRY_VERIFY_WITH_TIMEOUT(liveClientPid(registry, address) > 0, 15000);
			const qint64 next = liveClientPid(registry, address);
			QVERIFY2(next != 0 && next != pid, "the restart did not produce a new client pid");
			pid = next;
		}

		// --- the bound: the path is refused, typed -------------------------
		QString refusal;
		QVERIFY2(HostTracker::instance().refusedByCrashLoop(QString::fromLatin1(ZynClient), &refusal),
			"the bound was reached and the client executable is still allowed");

		const ControlResult refusedRestart = registry->invoke(QStringLiteral("oop.restart"), address);
		QVERIFY2(!refusedRestart.ok, "a restart fed the crash loop");
		QCOMPARE(refusedRestart.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refusedRestart.errorMessage.contains(QString::fromLatin1(ZynClient)),
			qPrintable(QStringLiteral("the refusal does not name the client: ")
				+ refusedRestart.errorMessage));

		const ControlResult refusedMode = registry->invoke(QStringLiteral("oop.set_mode"), QJsonObject{
			{QStringLiteral("target"), address.value(QStringLiteral("target")).toString()},
			{QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("mode"), QStringLiteral("separate-process")}});
		QVERIFY2(!refusedMode.ok, "the crash loop was re-hosted through set_mode");
		QCOMPARE(refusedMode.errorKind, ControlErrorKind::Refused);
		qInfo("bound reached: %s", refusal.toUtf8().constData());

		// The device still REPORTS the crash rather than looking healthy.
		QCOMPARE(hostingField(registry, address, QStringLiteral("state")),
			QStringLiteral("refused-crash-loop"));

		// --- and only reset_crashes lifts it -------------------------------
		const ControlResult reset = registry->invoke(QStringLiteral("oop.reset_crashes"), QJsonObject{
			{QStringLiteral("client"), QString::fromLatin1(ZynClient)}});
		QVERIFY2(reset.ok, qPrintable(reset.errorMessage));
		QVERIFY(reset.result.value(QStringLiteral("cleared_count")).toInt() >= 1);
		QVERIFY2(!HostTracker::instance().refusedByCrashLoop(QString::fromLatin1(ZynClient), nullptr),
			"reset_crashes did not lift the refusal");

		// oop.restart is the verb the refusal blocked, and it is allowed again.
		// (oop.set_mode would answer ok with changed=false here: the mode is
		// already separate-process, which is exactly the semantic it declares.)
		const ControlResult heldAgain = registry->invoke(QStringLiteral("oop.restart"), address);
		QVERIFY2(heldAgain.ok, qPrintable(heldAgain.errorMessage));
		QCOMPARE(heldAgain.result.value(QStringLiteral("restarted")).toBool(), true);
		QTRY_VERIFY_WITH_TIMEOUT(liveClientPid(registry, address) > 0, 15000);
		qInfo("after oop.reset_crashes the slot is drivable again: client pid %lld",
			static_cast<long long>(liveClientPid(registry, address)));
#endif
	}

private:
#ifndef Q_OS_WIN
	//! SIGKILL the client, then read the death off the SURFACE: the pid is gone
	//! and the crash count has reached \a expectedAtLeast. Split out of the case
	//! so the loop body carries the loop and not the three waits it makes.
	void killAndCount(ControlRegistry* registry, const QJsonObject& address, qint64 pid,
		int expectedAtLeast)
	{
		QCOMPARE(::kill(static_cast<pid_t>(pid), SIGKILL), 0);
		// The host is still running here - it is running this call - and the
		// surface notices: the slot stops reporting a live client.
		QTRY_COMPARE_WITH_TIMEOUT(liveClientPid(registry, address), qint64(0), 15000);
		QTRY_VERIFY_WITH_TIMEOUT(clientRecord(registry, QString::fromLatin1(ZynClient))
				.value(QStringLiteral("crashes")).toInt() >= expectedAtLeast, 15000);
		qInfo("round %d: client pid %lld killed; the host kept running and counted the death",
			expectedAtLeast, static_cast<long long>(pid));
	}

#endif

	bool m_engineInitialised = false;
};

QTEST_GUILESS_MAIN(OutOfProcessHostClientLoopTest)

#include "OutOfProcessHostClientLoopTest.moc"
