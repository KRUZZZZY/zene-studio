/*
 * CrashReporterTest.cpp
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
 *
 */

// Acceptance proofs for the offline crash reporter:
//
//   * a child process that REALLY raises the signal gets a bounded report with
//     the expected fields (signal, fault address, build/version, timestamp,
//     project path) -- SIGSEGV via a real bad write, SIGABRT via abort();
//   * the report cannot exceed the cap, even for absurd input;
//   * a second crash while the first is being handled neither recurses nor
//     hangs;
//   * a run that exits cleanly leaves NO report and NO stale marker;
//   * the next launch detects the report and offers it exactly once.
//
// The signal half is POSIX-only; on Windows the reporter is a documented no-op
// and the tests assert that instead of pretending to exercise signals.

#include "CrashReporter.h"
#include "lmmsversion.h"

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

QByteArray slurp(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return f.readAll();
}

QString reportPathIn(const QTemporaryDir& dir)
{
	return dir.filePath(QStringLiteral("%1/%2")
		.arg(QString::fromUtf8(kReportDirName), QString::fromUtf8(kReportFileName)));
}

// Wait for a child, bounded to `timeoutMs`.  Returns true when it exited in
// time; `status` receives waitpid()'s status word.  A timeout is a HANG, which
// the reporter must never cause, and every caller asserts on it.
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

#endif // !Q_OS_WIN

} // namespace


class CrashReporterTest : public QObject
{
	Q_OBJECT

private slots:

#ifdef Q_OS_WIN
	// The Windows build must degrade honestly, not pretend.
	void windows_build_is_a_documented_noop()
	{
		QVERIFY(!install("C:/does/not/matter"));
		QVERIFY(!isInstalled());
		QVERIFY(!hasPendingReport());
		QVERIFY(pendingReportPath().empty());
		QVERIFY(!sessionMarkerExists());
		CrashInfo info;
		info.signal = 6;
		QVERIFY(!writeReportIfIdle(info));
	}
#else

	// -----------------------------------------------------------------------
	// 1. The report writer itself: direct call, all fields present.
	// -----------------------------------------------------------------------
	void writeReport_writes_expected_fields()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		setProjectPath("/home/someone/Music/my track.mmp");

		CrashInfo info;
		info.signal = SIGSEGV;
		info.faultAddress = 0xdeadbeefULL;
		info.programCounter = 0x123456ULL;
		info.threadId = 4242;
		info.processId = 77;
		info.unixTime = 1700000000ULL;

		QVERIFY(writeReportIfIdle(info));

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "the report file was not written");
		QVERIFY(report.startsWith("Zene Studio crash report v1\n"));
		QVERIFY(report.contains("signal=SIGSEGV(11)\n"));
		QVERIFY(report.contains("fault_addr=0xdeadbeef\n"));
		QVERIFY(report.contains("pc=0x123456\n"));
		QVERIFY(report.contains("thread=4242\n"));
		QVERIFY(report.contains("pid=77\n"));
		QVERIFY(report.contains("time_unix=1700000000\n"));
		// Build id / version must be a real, non-empty build identity.
		QVERIFY(!QByteArray(LMMS_VERSION).isEmpty());
		QVERIFY(report.contains("version=" + QByteArray(LMMS_VERSION) + "\n"));
		QVERIFY(report.contains("platform="));
		// The project that was open is recorded verbatim.
		QVERIFY(report.contains("project=/home/someone/Music/my track.mmp\n"));
	}

	// -----------------------------------------------------------------------
	// 2. Bounds: absurd input cannot push the file past the cap.
	// -----------------------------------------------------------------------
	void bounds_report_cannot_exceed_cap()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		// A project path far longer than the hint buffer, plus maximum-width
		// numeric fields, is the worst case the writer can be handed.
		setProjectPath(std::string(4096, 'A'));

		CrashInfo info;
		info.signal = SIGSEGV;
		info.faultAddress = ~0ULL;
		info.programCounter = ~0ULL;
		info.threadId = ~0ULL;
		info.processId = ~0ULL;
		info.unixTime = ~0ULL;

		QVERIFY(writeReportIfIdle(info));

		const QFileInfo fi(reportPathIn(dir));
		QVERIFY(fi.exists());
		QVERIFY2(static_cast<unsigned long>(fi.size()) <= kMaxReportBytes,
			"the report exceeded kMaxReportBytes");
		// The cap is a real cap, not a coincidence of short input.
		QVERIFY(static_cast<unsigned long>(fi.size()) > 400);
	}

	// -----------------------------------------------------------------------
	// 3. Re-entrancy: a crash while handling a crash writes nothing else and
	//    terminates; it must not recurse and must not hang.
	//    (The nesting is driven through the documented test seam; the real
	//    end-to-end signal path is covered by tests 4-6.)
	// -----------------------------------------------------------------------
	void reentrancy_second_crash_is_refused()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		CrashInfo info;
		info.signal = SIGABRT;
		info.unixTime = 1700000001ULL;

		// Simulate "we are already inside the handler".
		setReentrancyGuardForTest(true);
		QVERIFY2(!writeReportIfIdle(info),
			"a nested crash must be refused, not written");
		QVERIFY(!QFileInfo::exists(reportPathIn(dir)));
		setReentrancyGuardForTest(false);

		// With the guard clear the same call does write, so the refusal above
		// is the guard working and not a broken writer.
		QVERIFY(writeReportIfIdle(info));
		QVERIFY(QFileInfo::exists(reportPathIn(dir)));
	}

	// -----------------------------------------------------------------------
	// 4. End to end: a real SIGSEGV in a child process is reported, with the
	//    real faulting address, and the child still dies by the signal.
	// -----------------------------------------------------------------------
	void child_real_sigsegv_is_reported()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		const std::string project = "/tmp/crash reporter test.mmp";
		QVERIFY(install(workDir));
		setProjectPath(project);

		::fflush(nullptr);
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");

		if (pid == 0)
		{
			// CHILD: install and then really fault.
			install(workDir);
			setProjectPath(project);
			volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1));
			*p = 1;                       // SIGSEGV with si_addr == 0x1
			::_exit(70);                  // must not be reached
		}

		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			"the child hung: a crash must not deadlock the process");
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			"the child must still die by SIGSEGV, as it would without the reporter");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for a real SIGSEGV");
		QVERIFY(report.contains("signal=SIGSEGV(11)\n"));
		// The real faulting address, straight from si_addr.
		QVERIFY2(report.contains("fault_addr=0x1\n"),
			"the report must carry the real faulting address");
		QVERIFY(report.contains("time_unix="));
		QVERIFY(report.contains("version=" + QByteArray(LMMS_VERSION) + "\n"));
		QVERIFY(report.contains("project=" + QByteArray(project.c_str()) + "\n"));
		QVERIFY(report.contains("thread="));
		QVERIFY(report.contains("pid="));
		QVERIFY(static_cast<unsigned long>(report.size()) <= kMaxReportBytes);
	}

	// -----------------------------------------------------------------------
	// 5. SIGABRT (abort(), i.e. an unhandled exception) is reported too.
	// -----------------------------------------------------------------------
	void child_abort_is_reported()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));

		::fflush(nullptr);
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");

		if (pid == 0)
		{
			install(workDir);
			::abort();
			::_exit(71);                  // unreachable
		}

		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status), "the child hung on SIGABRT");
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
			"the child must still die by SIGABRT");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for SIGABRT");
		QVERIFY(report.contains("signal=SIGABRT(6)\n"));
		QVERIFY(static_cast<unsigned long>(report.size()) <= kMaxReportBytes);
	}

	// -----------------------------------------------------------------------
	// 6. A repeated signal must not hang the process, and the report must stay
	//    bounded even when a session marker is already on disk.
	// -----------------------------------------------------------------------
	void repeated_signal_does_not_hang()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));

		::fflush(nullptr);
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");

		if (pid == 0)
		{
			install(workDir);
			// A marker plus an immediate second signal: the first must still
			// terminate the process.
			beginSession();
			::raise(SIGSEGV);
			::raise(SIGSEGV);
			::_exit(72);                  // unreachable
		}

		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			"a repeated signal hung the process");
		QVERIFY(WIFSIGNALED(status));

		const QFileInfo fi(reportPathIn(dir));
		QVERIFY(fi.exists());
		QVERIFY2(static_cast<unsigned long>(fi.size()) <= kMaxReportBytes,
			"the report exceeded kMaxReportBytes");
	}

	// -----------------------------------------------------------------------
	// 7. Negative control: a clean run leaves NO report and NO stale marker.
	// -----------------------------------------------------------------------
	void clean_run_leaves_no_state()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		// A clean run: open the session, then close it the way main.cpp does.
		beginSession();
		QVERIFY2(QFileInfo::exists(dir.filePath(QString::fromUtf8(kSessionMarkerName))),
			"beginSession() must create the marker, or the negative control is vacuous");
		endSession();

		QVERIFY2(!QFileInfo::exists(dir.filePath(QString::fromUtf8(kSessionMarkerName))),
			"a clean exit must clear the session marker");
		QVERIFY2(!QFileInfo::exists(reportPathIn(dir)),
			"a clean run must leave no crash report");
		QVERIFY2(!hasPendingReport(), "a clean run must leave nothing pending");
		QVERIFY2(!sessionMarkerExists(), "no stale 'crashed' marker may survive");

		// Nothing of ours is left behind, and the working directory itself was
		// not even given a crash-reports/ subdirectory.
		QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
		QVERIFY2(!QDir(dir.path()).exists(QString::fromUtf8(kReportDirName)),
			"a clean run must not create the crash-reports directory");
	}

	// -----------------------------------------------------------------------
	// 8. The next launch detects the report and offers it exactly once.
	//    The state under test was produced by a real crashing child, not by
	//    the test poking at files.
	// -----------------------------------------------------------------------
	void next_launch_detects_and_offers_once()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();

		QVERIFY(install(workDir));
		::fflush(nullptr);
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");
		if (pid == 0)
		{
			install(workDir);
			beginSession();
			::raise(SIGSEGV);
			::_exit(73);                  // unreachable
		}
		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status), "the crashing child hung");
		QVERIFY(WIFSIGNALED(status));

		// "Next launch": a fresh install over the same working directory.
		QVERIFY(install(workDir));

		QVERIFY2(sessionMarkerExists(),
			"an unclean exit must leave the session marker for diagnosis");
		QVERIFY2(hasPendingReport(), "the report from the crashed run must be pending");
		const std::string pending = pendingReportPath();
		QCOMPARE(QString::fromStdString(pending), reportPathIn(dir));
		QVERIFY(QFileInfo::exists(QString::fromStdString(pending)));

		// The offer happens once: acknowledging keeps the file but stops the
		// dialog coming back on every launch.
		acknowledgePendingReport();
		QVERIFY2(!hasPendingReport(), "an acknowledged report must not be offered again");
		QVERIFY2(QFileInfo::exists(QString::fromStdString(pending)),
			"acknowledging must not delete the report the user is about to attach");

		// Discarding is the explicit "no thanks" and removes it.
		discardPendingReport();
		QVERIFY(!QFileInfo::exists(QString::fromStdString(pending)));
		QVERIFY(!hasPendingReport());

		// A later clean session clears the stale marker from the crashed run.
		beginSession();
		endSession();
		QVERIFY(!sessionMarkerExists());
	}

#endif // Q_OS_WIN
};

QTEST_GUILESS_MAIN(CrashReporterTest)
#include "CrashReporterTest.moc"
