/*
 * ControlCrashAcknowledgeTest.cpp
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

// The crash reporter's `offered` sentinel, THROUGH THE CONTROL COMMAND.
//
// The reporter writes that sentinel as a FILE (CrashReporter.cpp:
// writeSmallFile opens it for writing), so a path that merely EXISTS at that
// name is not an acknowledgement: a directory occupying it blocks the write
// and the reporter cannot write through it.
//
// crash.acknowledge_report must therefore refuse such a call instead of
// reporting an acknowledgement that never happened - and the refusal must be
// the honest one ("the sentinel is not a file"), not "ALREADY acknowledged"
// for a state that never was.
//
// The command is the seam, so the claim is made through it: the same path a
// client takes, with the engine up (the command is engine-gated) and the
// reporter installed over a temporary working directory.

#include "CrashReporter.h"
#include "ControlRegistry.h"
#include "Engine.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

class ControlCrashAcknowledgeTest : public QObject
{
	Q_OBJECT

private slots:
	// The engine is only up for the case that needs it, and it is torn down
	// again right after, so nothing else in this binary inherits an engine.
	void cleanup()
	{
		if (!m_engineStarted) { return; }
		lmms::crashreporter::uninstall();
		lmms::ControlRegistry::setReady(false);
		lmms::Engine::destroy();
		m_engineStarted = false;
	}

#ifdef Q_OS_WIN
	// The Windows build must degrade honestly, not pretend: the reporter is a
	// documented no-op there, so there is no sentinel and no acknowledgement.
	void windows_build_is_a_documented_noop()
	{
		QVERIFY(!lmms::crashreporter::install("C:/does/not/matter"));
		QVERIFY(!lmms::crashreporter::isInstalled());
		QVERIFY(!lmms::crashreporter::hasPendingReport());
	}
#else
	// -----------------------------------------------------------------------
	// The `offered` marker path holds a DIRECTORY: the reporter cannot write
	// the sentinel there, so the command must refuse - typed, and without
	// touching the report the user still has to attach.
	// -----------------------------------------------------------------------
	void acknowledge_refuses_directory_at_offered_marker()
	{
		lmms::Engine::init(true);
		m_engineStarted = true;
		lmms::ControlRegistry::setReady(true);

		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(lmms::crashreporter::install(dir.path().toStdString()));

		// A report is there to acknowledge, written where the reporter writes
		// its reports.
		const QString reportPath = QString::fromStdString(lmms::crashreporter::pendingReportPath());
		const QFileInfo reportInfo(reportPath);
		QVERIFY(QDir().mkpath(reportInfo.absolutePath()));
		QFile report(reportPath);
		QVERIFY(report.open(QIODevice::WriteOnly));
		QCOMPARE(report.write("pending crash report\n"), qint64(21));
		report.close();
		QVERIFY(QFileInfo(reportPath).isFile());

		// The fault state: a DIRECTORY where the sentinel belongs.
		const QString markerPath = QDir(reportInfo.absolutePath())
			.filePath(QString::fromUtf8(lmms::crashreporter::kOfferedMarkerName));
		QVERIFY(QDir().mkdir(markerPath));
		QVERIFY(QFileInfo(markerPath).isDir());

		const lmms::ControlResult result = lmms::ControlRegistry::instance()->invoke(
			QStringLiteral("crash.acknowledge_report"));
		QVERIFY2(!result.ok, qPrintable(result.errorMessage));
		// `busy`, not `refused`: the report was NEVER acknowledged - the
		// sentinel was not written - so this is not the "already offered"
		// no-change case, it is a write that did not happen.
		QCOMPARE(result.errorKind, lmms::ControlErrorKind::Busy);
		// The report is kept - the user still has to attach it - and the
		// directory that blocked the sentinel is left exactly as it was.
		QVERIFY(QFileInfo(reportPath).isFile());
		QVERIFY(QFileInfo(markerPath).isDir());

		// The read side must agree with the write side: the sentinel was never
		// written, so crash.list_reports must not call the report "offered".
		const lmms::ControlResult listed = lmms::ControlRegistry::instance()->invoke(
			QStringLiteral("crash.list_reports"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("report_count")).toInt(), 1);
		QCOMPARE(listed.result.value(QStringLiteral("offered")).toBool(), false);
	}
#endif // Q_OS_WIN

private:
	bool m_engineStarted = false;
};

QTEST_GUILESS_MAIN(ControlCrashAcknowledgeTest)
#include "ControlCrashAcknowledgeTest.moc"
