/*
 * EngineServeTest.cpp - the rules of serve mode (`zene --engine`) that need no engine
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

// Q050-517. The process-level behaviour (the socket comes up with no display at all, interface
// commands answer typed, SIGTERM and control.quit end it cleanly) is tests/control-engine-serve.py,
// which drives the real binary. This file holds the three decisions that are pure functions.

#include "EngineServe.h"
#include "UnattendedRun.h"

#include <QtTest>

using namespace lmms;

class EngineServeTest : public QObject
{
	Q_OBJECT

private slots:
	void cleanup()
	{
		setEngineServe(false);
		setAgentInstance(false);
	}

	//! An engine nobody can reach or stop is refused before anything is opened.
	void startNeedsAControlSocket()
	{
		engineserve::StartOptions options;
		QVERIFY(engineserve::startRefusal(options).contains(QStringLiteral("--control-socket")));

		options.controlSocket = QStringLiteral("/tmp/zene.sock");
		QCOMPARE(engineserve::startRefusal(options), QString());

		options.fileToLoad = QStringLiteral("song.mmpz");
		QCOMPARE(engineserve::startRefusal(options), QString());
	}

	//! A project and an import name two different initial states; picking one silently would drop the other.
	void startRefusesAProjectAndAnImportTogether()
	{
		engineserve::StartOptions options;
		options.controlSocket = QStringLiteral("/tmp/zene.sock");
		options.fileToLoad = QStringLiteral("song.mmpz");
		options.fileToImport = QStringLiteral("song.mid");
		QVERIFY(engineserve::startRefusal(options).contains(QStringLiteral("not both")));

		options.fileToLoad.clear();
		QCOMPARE(engineserve::startRefusal(options), QString());
	}

	//! The save a window's close event does for control.quit {save:true}: only for a quit that asked
	//! for it, and only when there is a file to write (control.quit refuses save:true without one).
	void quitSavesOnlyWhatWasAskedFor()
	{
		QVERIFY(engineserve::quitShouldSave(true, true, true));
		QVERIFY(!engineserve::quitShouldSave(false, true, true));   // the loop ended some other way
		QVERIFY(!engineserve::quitShouldSave(true, false, true));   // save:false, or SIGTERM
		QVERIFY(!engineserve::quitShouldSave(true, true, false));   // nothing to save to
	}

	//! Serve mode has no display whatever the desktop offers: `requires: display` commands must be
	//! refused typed, and nothing may wait on a dialog. The control: without the flag, a process that
	//! names a real platform is attended and has a display.
	void serveModeHasNoDisplayAndIsUnattended()
	{
		const QByteArray saved = qgetenv("QT_QPA_PLATFORM");
		qputenv("QT_QPA_PLATFORM", "xcb");

		QVERIFY(!isEngineServe());
		QVERIFY(hasDisplay());
		QVERIFY(!hasNoDisplay());
		QVERIFY(!isUnattendedRun());

		setEngineServe(true);
		QVERIFY(isEngineServe());
		QVERIFY(!hasDisplay());
		QVERIFY(hasNoDisplay());
		QVERIFY(isUnattendedRun());

		setEngineServe(false);
		QVERIFY(hasDisplay());

		if (saved.isNull()) { qunsetenv("QT_QPA_PLATFORM"); }
		else { qputenv("QT_QPA_PLATFORM", saved); }
	}
};

QTEST_GUILESS_MAIN(EngineServeTest)
#include "EngineServeTest.moc"
