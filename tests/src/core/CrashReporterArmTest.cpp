/*
 * CrashReporterArmTest.cpp - the ARM/DISARM proof for the crash reporter
 *                            (board task #643: crash.enable / crash.disable).
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/*
 * WHAT THIS PROVES, and why it is not CrashReporterTest.
 *
 * CrashReporterTest proves the reporter itself: the report's fields, its bounds,
 * the re-entrancy guard, the clean-run negative control and the offer on the next
 * launch. This file proves the ARM STATE: the pair the control surface drives as
 * crash.enable / crash.disable is crashreporter::install() / uninstall(), so the
 * engine-level claim the socket transcript rests on has to be measured here, in
 * process, with REAL signals:
 *
 *   1. install() arms the handler AND handlersArmed() - the predicate that asks
 *      the kernel rather than a flag - agrees; uninstall() disarms both, and the
 *      report directory the reporter remembers survives it.
 *   2. armed: a real SIGSEGV in a forked child writes a report and the child
 *      still dies by SIGSEGV.
 *   3. THE NEGATIVE CONTROL: with the reporter disarmed, the SAME crash writes NO
 *      report - and still kills the child by the signal, so the absence of a
 *      report is "the handler is gone", not "the signal was swallowed". Measured
 *      for SIGSEGV and for SIGABRT, so it is the SET and not one signal.
 *   4. re-arming is a real arming: the next crash is reported again.
 *   5. a second uninstall() is not a state change (it returns false), which is
 *      what makes crash.disable's refusal honest.
 *
 * The signal half is POSIX-only; on Windows the reporter is a documented no-op
 * and the tests assert that instead of pretending to exercise signals.
 */

#include "CrashReporter.h"

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

using namespace lmms::crashreporter;

#ifndef Q_OS_WIN

QString reportPathIn(const QTemporaryDir& dir)
{
	return dir.filePath(QStringLiteral("%1/%2")
		.arg(QString::fromUtf8(kReportDirName), QString::fromUtf8(kReportFileName)));
}

//! Wait for a child, bounded. A timeout is a HANG, and the reporter must never
//! cause one; every caller asserts on it (the same helper CrashReporterTest uses).
bool waitBounded(pid_t pid, int timeoutMs, int& status)
{
	for (int waited = 0; waited < timeoutMs; waited += 20)
	{
		const pid_t r = ::waitpid(pid, &status, WNOHANG);
		if (r == pid) { return true; }
		if (r < 0) { return false; }
		usleep(20000);
	}
	return false;
}

/*! Fork a child that really raises \p sig, and report how it died.
 *
 * The child inherits the parent's dispositions and the report-directory fd, so
 * whatever the parent armed is what decides whether a report appears. `diedBy`
 * is the signal that killed it, or -1 when it was not killed by one.
 */
bool childRaises(int sig, int* diedBy)
{
	::fflush(nullptr);
	const pid_t pid = ::fork();
	if (pid < 0) { *diedBy = -1; return false; }
	if (pid == 0)
	{
		::raise(sig);
		::_exit(90);          // reached only if the signal did nothing at all
	}
	int status = 0;
	const bool done = waitBounded(pid, 15000, status);
	*diedBy = done && WIFSIGNALED(status) ? WTERMSIG(status) : -1;
	return done;
}

//! The report's bytes, or empty when it is not there.
QByteArray slurp(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return f.readAll();
}

#endif // !Q_OS_WIN

} // namespace


class CrashReporterArmTest : public QObject
{
	Q_OBJECT

private slots:

#ifdef Q_OS_WIN
	// Windows: the arm pair must degrade honestly, not pretend.
	void windows_build_arms_nothing()
	{
		QVERIFY(!install("C:/does/not/matter"));
		QVERIFY(!isInstalled());
		QVERIFY(!handlersArmed());
		QVERIFY(reportDirectory().empty());
		QVERIFY(handledSignalList().empty());
		QVERIFY2(!uninstall(), "uninstall() must report that there was nothing to disarm");
	}
#else

	// -----------------------------------------------------------------------
	// 1. The arm state itself: install arms, uninstall disarms, and the
	//    predicate that asks the kernel agrees with both.
	// -----------------------------------------------------------------------
	void install_arms_and_uninstall_disarms()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();

		QVERIFY2(!handlersArmed(), "nothing may be armed before install()");

		QVERIFY(install(workDir));
		QVERIFY2(isInstalled(), "install() must set the module's installed flag");
		QVERIFY2(handlersArmed(),
			"install() must leave every claimed signal naming the crash handler (asked of the kernel)");
		QCOMPARE(QString::fromStdString(reportDirectory()), dir.path());
		// The set the surface reports is the set that is armed - one definition.
		QVERIFY2(!QString::fromStdString(handledSignalList()).isEmpty(),
			"an armed reporter must name the signals it claims");
		QVERIFY(QString::fromStdString(handledSignalList()).contains(QStringLiteral("SIGSEGV")));

		QVERIFY2(uninstall(), "uninstall() must report the change it made");
		QVERIFY2(!isInstalled(), "uninstall() must clear the installed flag");
		QVERIFY2(!handlersArmed(),
			"after uninstall() no claimed signal may still name the crash handler");
		QVERIFY2(reportDirectory() == workDir,
			"the reporter must REMEMBER its directory across a disarm: that is what crash.enable re-arms to");
	}

	// -----------------------------------------------------------------------
	// 2. Armed: a real crash is reported.
	// -----------------------------------------------------------------------
	void armed_crash_is_reported()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		QVERIFY(handlersArmed());

		int diedBy = -1;
		QVERIFY2(childRaises(SIGSEGV, &diedBy), "the crashing child hung: a crash must not deadlock the process");
		QVERIFY2(diedBy == SIGSEGV, "the child must still die by SIGSEGV, as it would without the reporter");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for a real SIGSEGV while armed");
		QVERIFY(report.contains("signal=SIGSEGV(11)\n"));
	}

	// -----------------------------------------------------------------------
	// 3. THE NEGATIVE CONTROL: disarmed, the same crash reports NOTHING and
	//    still kills the process by the signal. This is the leg that makes
	//    crash.disable's claim a measurement rather than a promise.
	// -----------------------------------------------------------------------
	void disarmed_crash_writes_no_report()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));
		QVERIFY(handlersArmed());
		QVERIFY(uninstall());
		QVERIFY2(!handlersArmed(), "the negative control is vacuous unless the disarm really happened");
		QVERIFY2(!isInstalled(), "the negative control is vacuous unless the flag was cleared");

		// The directory is REMOVED first, so the assertion below cannot pass
		// because a report from an earlier leg was already lying there.
		QVERIFY(!QDir(dir.path()).exists(QString::fromUtf8(kReportDirName)));

		int diedBy = -1;
		QVERIFY2(childRaises(SIGSEGV, &diedBy), "the disarmed child hung");
		QVERIFY2(diedBy == SIGSEGV,
			"the signal must NOT be swallowed by a disarmed reporter: the child still dies by SIGSEGV");
		QVERIFY2(!QFileInfo::exists(reportPathIn(dir)),
			"DISARMED: a real SIGSEGV wrote a report - the disarm does not hold");
		QVERIFY2(!QDir(dir.path()).exists(QString::fromUtf8(kReportDirName)),
			"DISARMED: the crash-reports directory was created, so something is still handling the signal");

		// The same for SIGABRT: the claim is about the SET, not one signal.
		int abortedBy = -1;
		QVERIFY2(childRaises(SIGABRT, &abortedBy), "the disarmed child hung on SIGABRT");
		QVERIFY2(abortedBy == SIGABRT, "the child must still die by SIGABRT while disarmed");
		QVERIFY2(!QFileInfo::exists(reportPathIn(dir)),
			"DISARMED: a real SIGABRT wrote a report");
	}

	// -----------------------------------------------------------------------
	// 4. Re-arming is a real arming: the next crash is reported again, which is
	//    what makes control.undo of a crash.disable meaningful.
	// -----------------------------------------------------------------------
	void rearm_reports_again()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		QVERIFY(uninstall());
		QVERIFY2(!handlersArmed(), "the disarm before the re-arm must have taken");

		// Re-arm the way crash.enable does with no arguments: to the directory
		// the reporter remembers, not to a new one.
		QVERIFY(install(reportDirectory()));
		QVERIFY(handlersArmed());

		int diedBy = -1;
		QVERIFY2(childRaises(SIGABRT, &diedBy), "the re-armed child hung");
		QVERIFY(diedBy == SIGABRT);
		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "after a re-arm the same crash must be reported again");
		QVERIFY(report.contains("signal=SIGABRT(6)\n"));
	}

	// -----------------------------------------------------------------------
	// 5. The no-op calls report themselves as no-ops: this is what lets
	//    crash.disable refuse a disable of a disarmed reporter and crash.enable
	//    refuse an enable of an armed one, instead of recording an inverse of a
	//    call that changed nothing.
	// -----------------------------------------------------------------------
	void a_second_uninstall_is_not_a_state_change()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		QVERIFY2(uninstall(), "the first uninstall() IS a state change and must say so");
		QVERIFY2(!uninstall(), "the second uninstall() is not, and must say so");
		QVERIFY(!handlersArmed());
	}

#endif // Q_OS_WIN
};

QTEST_GUILESS_MAIN(CrashReporterArmTest)
#include "CrashReporterArmTest.moc"
