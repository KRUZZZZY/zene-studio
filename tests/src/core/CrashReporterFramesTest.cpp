/*
 * CrashReporterFramesTest.cpp - the raw stack context a crash report carries
 *                               (Q050-224.2, stage 2 of 4 of Q050-224).
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

// WHY THIS IS ITS OWN BINARY. The proofs belong to the crash reporter, next to
// CrashReporterTest, but they are a different subject (the report's frame
// sequence rather than the offer lifecycle) and CrashReporterTest.cpp is an
// inherited file: adding them there took it past the 500-code-line ratchet
// (412 -> 534), and the ratchet is not moved for a new feature. This is the same
// arrangement SafeStartTest / SafeStartLoadPathTest use.
//
// WHAT IS PROVEN, against a REAL fault in a forked child, never against a
// hand-made report:
//   1. a real SIGSEGV's report carries the thread id, the faulting address, the
//      PC and a BOUNDED sequence of raw frame addresses, and the sequence starts
//      at the faulting instruction (the crash site, not just our own frames);
//   2. the sequence is bounded: a crash 200 frames deep keeps exactly
//      kMaxCrashFrames addresses, the report stays inside kMaxReportBytes, and
//      the walk stops at a smaller budget when it is given one;
//   3. a crash while the dynamic loader is busy (inside dl_iterate_phdr's
//      callback, i.e. the shape of a crash during plug-in loading) still writes
//      the report and still dies by the signal - the walk does not hang on the
//      loader's lock.
//
// Criterion 2's other half - a clean run still leaves no report and no stale
// marker - is CrashReporterTest::clean_run_leaves_no_state, in the binary that
// carries the same code; it is unchanged and still passes.
//
// The signal half is POSIX-only; on Windows the reporter is a documented no-op
// and this file asserts that instead of pretending to exercise signals.

#include "CrashReporter.h"
#include "CrashReporterFormat.h"

#include <QtTest>
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
// dl_iterate_phdr() is the glibc/musl loader walk; Darwin has no <link.h>
// equivalent and no such call, so the loader-lock proof is Linux-only.
#include <link.h>
#endif
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

// Wait for a child, bounded to `timeoutMs`: a timeout is a HANG, which the
// reporter must never cause, and every caller asserts on it.
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

// ---- reading the report's frame block ---------------------------------------
// Read the way a human or an offline symbolizer reads it: the declared count and
// the raw addresses in file order.  Nothing here resolves a symbol - the report
// carries addresses and the reporter never looks a module up.

// The value of a "key=0x<hex>" field, or 0 when the field is absent.
unsigned long long hexField(const QByteArray& report, const char* key)
{
	const QByteArray prefix(key);
	const int at = report.indexOf(prefix);
	if (at < 0) { return 0; }
	const int start = at + prefix.size();
	const int end = report.indexOf('\n', start);
	if (end <= start) { return 0; }
	bool ok = false;
	const unsigned long long value = report.mid(start, end - start).toULongLong(&ok, 16);
	return ok ? value : 0;
}

struct Frames
{
	// -1 when the report carries no frame block at all: the shape a report had
	// before Q050-224.2, which every proof below must reject rather than read
	// as a count of zero.
	int declared = -1;
	QList<unsigned long long> addresses;
};

Frames readFrames(const QByteArray& report)
{
	Frames out;
	const int at = report.indexOf("frames=");
	if (at < 0) { return out; }
	const int start = at + 7;
	const int end = report.indexOf('\n', start);
	if (end <= start) { return out; }
	bool ok = false;
	out.declared = report.mid(start, end - start).toInt(&ok);
	if (!ok) { out.declared = -1; return out; }
	for (int i = 0; i < out.declared; ++i)
	{
		const QByteArray key = QByteArray("frame[") + QByteArray::number(i) + "]=";
		out.addresses.append(hexField(report, key.constData()));
	}
	return out;
}

// ---- the faulting child ------------------------------------------------------

// A fault reached through a real call chain, so the walk has callers to cross
// into rather than one frame. The recursion goes through a VOLATILE function
// pointer: GCC turns `f(n-1) + 1` into a loop and inlines what it can, and a
// depth the optimiser collapsed would make the "bounded" proof measure nothing.
int crashAtDepth(int depth, volatile int* sink);
int (*volatile s_crashAtDepth)(int, volatile int*) = &crashAtDepth;

__attribute__((noinline)) int crashAtDepth(int depth, volatile int* sink)
{
	if (depth <= 0)
	{
		*sink = 1;                 // SIGSEGV, si_addr == 1
		return 0;
	}
	return s_crashAtDepth(depth - 1, sink) + 1;
}

// Fork a child that installs the reporter and faults `depth` frames down.
// `status` receives the child's wait status, or -1 when it hung.
void crashInChild(const std::string& workDir, int depth, int& status)
{
	::fflush(nullptr);
	const pid_t pid = ::fork();
	if (pid == 0)
	{
		install(workDir);
		crashAtDepth(depth, reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1)));
		::_exit(76);               // unreachable
	}
	status = 0;
	if (pid < 0 || !waitBounded(pid, 15000, status)) { status = -1; }
}

#if defined(__linux__)
// The loader walk's own callback, used to crash WHILE the dynamic loader is
// busy - the shape of a crash during plug-in loading, where the loader's lock is
// already held by the thread that is now dying.
int faultDuringLoaderWalk(struct dl_phdr_info*, size_t, void*)
{
	static bool went = false;
	if (went) { return 0; }
	went = true;
	*reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1)) = 1;
	return 0;
}
#endif

#endif // !Q_OS_WIN

} // namespace


class CrashReporterFramesTest : public QObject
{
	Q_OBJECT

private slots:

#ifdef Q_OS_WIN
	// Windows must degrade honestly: no handler, so no frame sequence either.
	void windows_build_captures_no_frames()
	{
		QVERIFY(!install("C:/does/not/matter"));
		unsigned long long frames[4] = {0, 0, 0, 0};
		QCOMPARE(captureFrames(nullptr, frames, 4), 0);
		CrashInfo info;
		info.signal = 6;
		QVERIFY(!writeReportIfIdle(info));
		QCOMPARE(info.frameCount, 0);
	}
#else

	// -----------------------------------------------------------------------
	// 1. A real fault leaves RAW STACK CONTEXT behind: the thread id, the
	//    faulting address, the PC and a bounded sequence of frame addresses,
	//    starting at the crash site itself (Q050-224.2, criterion 1).
	// -----------------------------------------------------------------------
	void report_contains_bounded_raw_frames()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));

		int status = 0;
		crashInChild(workDir, 3, status);
		QVERIFY2(status != -1, "the crashing child hung: a crash must not deadlock");
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			"the child must still die by SIGSEGV, as it would without the reporter");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for a real SIGSEGV");
		// The context a report already carried stays put ...
		QVERIFY2(hexField(report, "thread=") != 0, "the report must carry the crashing thread's id");
		QVERIFY2(report.contains("fault_addr=0x1\n"), "the real si_addr must still be reported");
		const unsigned long long pc = hexField(report, "pc=");
		// ... and the frame sequence this unit adds is present and bounded.
		const Frames frames = readFrames(report);
		QVERIFY2(frames.declared >= 1, "the report carries no frame sequence");
		QVERIFY2(frames.declared <= kMaxCrashFrames,
			"the frame sequence exceeded kMaxCrashFrames");
		QCOMPARE(static_cast<int>(frames.addresses.size()), frames.declared);
		for (unsigned long long address : frames.addresses)
		{
			QVERIFY2(address != 0, "an empty frame address must not be written");
		}
		// The sequence starts at the faulting instruction where the architecture
		// exposes one, so it names the crash site and not only our own frames.
		QVERIFY2(pc != 0, "the test binary's architecture must expose a program counter");
		QCOMPARE(frames.addresses.value(0), pc);
#if defined(__linux__)
		// glibc carries CFI for its signal trampoline, so the walk crosses the
		// signal frame and reaches the caller chain. Darwin's unwinder stops at
		// its trampoline - that is a documented limit, not a claim made here.
		QVERIFY2(frames.declared >= 3,
			"the walk must cross the signal frame into the caller chain on Linux");
#endif
		QVERIFY2(static_cast<unsigned long>(report.size()) <= kMaxReportBytes,
			"the report exceeded kMaxReportBytes");
	}

	// -----------------------------------------------------------------------
	// 2. The walk is BOUNDED (Q050-224.2, criterion 2): a crash 200 frames deep
	//    keeps exactly kMaxCrashFrames addresses rather than one per frame, the
	//    report stays inside kMaxReportBytes, and a smaller budget truncates the
	//    walk - which is what stops a corrupt or cyclic unwind.
	// -----------------------------------------------------------------------
	void frames_are_capped_on_a_deep_crash()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));

		int status = 0;
		crashInChild(workDir, 200, status);
		QVERIFY2(status != -1, "the crashing child hung: a crash must not deadlock");
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			"the child must still die by SIGSEGV");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for a real SIGSEGV");
		const Frames frames = readFrames(report);
		QVERIFY2(frames.declared >= 1, "the report carries no frame sequence");
		QVERIFY2(frames.declared <= kMaxCrashFrames,
			"a deeper stack must not push the frame sequence past its budget");
		QCOMPARE(static_cast<int>(frames.addresses.size()), frames.declared);
#if defined(__linux__)
		// 200 real frames are there to walk, so the budget - not the stack depth -
		// is what ends the sequence: exactly the cap, not a shorter accident.
		QCOMPARE(frames.declared, kMaxCrashFrames);
#endif
		QVERIFY2(static_cast<unsigned long>(report.size()) <= kMaxReportBytes,
			"the report exceeded kMaxReportBytes");

		// The cap is the walk's own budget, read from the walk itself: a caller
		// who asks for three addresses gets at most three.
		unsigned long long small[3] = {0, 0, 0};
		const int kept = captureFrames(nullptr, small, 3);
		QVERIFY2(kept >= 1 && kept <= 3, "the walk must stop at the budget it was given");
	}

	// -----------------------------------------------------------------------
	// 3. A crash while the dynamic loader is busy still reports and still dies
	//    (Q050-224.2, criterion 2): the frame walk consults the loaded modules'
	//    unwind tables through the loader's RECURSIVE lock, so a fault on the
	//    thread that already holds it must not hang the handler. Crashing inside
	//    dl_iterate_phdr's callback is that state, and it is the shape of a crash
	//    during plug-in loading.
	// -----------------------------------------------------------------------
	void crash_while_the_loader_is_busy_still_reports()
	{
#if !defined(__linux__)
		// Stated, never silent: there is no dl_iterate_phdr() outside Linux, so
		// this platform cannot be put into the state the case is about. The
		// report's frame sequence is still proven here by cases 1 and 2.
		QSKIP("dl_iterate_phdr() is Linux-only; the loader-lock case cannot be built elsewhere");
#else
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
			dl_iterate_phdr(&faultDuringLoaderWalk, nullptr);
			::_exit(77);               // unreachable
		}

		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			"the handler hung on the loader's lock: it must never wait");
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			"the child must still die by SIGSEGV");

		const QByteArray report = slurp(reportPathIn(dir));
		QVERIFY2(!report.isEmpty(), "no report was written for a fault inside the loader");
		const Frames frames = readFrames(report);
		QVERIFY2(frames.declared >= 1, "the report carries no frame sequence");
		QVERIFY2(frames.declared <= kMaxCrashFrames,
			"the frame sequence exceeded kMaxCrashFrames");
		QVERIFY2(static_cast<unsigned long>(report.size()) <= kMaxReportBytes,
			"the report exceeded kMaxReportBytes");
#endif // __linux__
	}

#endif // Q_OS_WIN
};

QTEST_GUILESS_MAIN(CrashReporterFramesTest)
#include "CrashReporterFramesTest.moc"
