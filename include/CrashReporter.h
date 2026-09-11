/*
 * CrashReporter.h - minimal, offline, local-only crash reporter
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product); it is derived
 * from LMMS - https://lmms.io
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

#ifndef ZENE_CRASH_REPORTER_H
#define ZENE_CRASH_REPORTER_H

#include <string>

namespace lmms::crashreporter
{

// ---------------------------------------------------------------------------
// What this is
// ---------------------------------------------------------------------------
// The smallest honest crash reporter: a POSIX signal handler writes ONE
// bounded text file next to the user's working directory and the process then
// dies exactly as it would have without us.  The next launch notices the file
// and offers it to the user, who can attach it to a bug report by hand.
//
// There is no upload, no socket, no DNS, no telemetry and no network code of
// any kind in this translation unit.  If it ever needs a network, that is a
// different product decision (see the opt-in telemetry design) and a fail here.
//
// ---------------------------------------------------------------------------
// Async-signal-safety (the design, stated so it can be checked)
// ---------------------------------------------------------------------------
// The handler runs on the failing thread, which may be the audio thread, while
// arbitrary state is corrupt.  It therefore calls ONLY functions on POSIX's
// async-signal-safe list:
//
//     openat(), write(), close(), raise(), sigaction(), sigemptyset(),
//     getpid(), time()
// plus the raw syscall gettid(), which allocates nothing and takes no lock.
//
// There is no malloc, no C++ object construction, no stdio, no mutex, no Qt
// call and no unbounded loop in the handler path.  The report is assembled in
// a fixed-size stack buffer and written with a bounded number of write() calls.
//
// A pre-opened *report* fd would avoid even openat(), but it would create the
// report file on every clean run, which this task forbids (behaviour-
// preserving: a clean run must leave no crash state behind).  Instead the
// report *directory* fd is opened once on the main thread at install time and
// reused in the handler with openat(); the file itself is created only when a
// crash actually happens.
//
// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------
// kMaxReportBytes is a hard cap.  The writer stops at the cap, so the report
// file can never exceed it no matter what the crash looks like.  The project
// path is truncated to kMaxProjectPathBytes when it is copied on the main
// thread, so the cap is reachable only in principle, never by long input.
//
// ---------------------------------------------------------------------------
// Re-entrancy
// ---------------------------------------------------------------------------
// A crash while handling a crash (e.g. SIGSEGV inside the SIGABRT handler) must
// not recurse and must not hang.  A single `volatile sig_atomic_t` guard makes
// the second entry refuse to write and go straight to re-raise-and-die.  The
// signal being handled is also masked for the duration, so the same signal
// cannot re-enter; a different signal can, and is caught by the guard.
//
// ---------------------------------------------------------------------------
// Which signals, and why
// ---------------------------------------------------------------------------
//   SIGSEGV, SIGBUS, SIGILL, SIGFPE  - synchronous faults; si_addr is the
//                                      faulting address.  A DAW's classic
//                                      crashes (bad plugin, bad buffer) land
//                                      here.  SIGFPE is skipped when the
//                                      debug-only LMMS_DEBUG_FPE build flag
//                                      has already claimed it with a
//                                      backtrace handler.
//   SIGABRT                          - abort().  This is how the C++ runtime
//                                      reports an unhandled exception
//                                      (std::terminate -> abort) and how
//                                      Qt's qFatal() dies, so it is the
//                                      "unhandled exception" case.
// Deliberately NOT handled, with reasons:
//   SIGINT, SIGTERM - a shutdown *request*, not a crash; SIGINT already has a
//                     clean-quit path (GuiApplication::sigintHandler).
//   SIGKILL, SIGSTOP - uncatchable by definition.
//   SIGPIPE - already SIG_IGN at startup (main.cpp).
// On Windows this whole file is a no-op (see the implementation); the alpha
// ships the same handler everywhere it is meaningful and says so.

// Report / marker file names, relative to the report directory.
// The report lives in <working dir>/crash-reports/ and that directory is
// created lazily, by the handler, only when a crash is actually written — so a
// run that never crashes creates no crash state at all.
constexpr const char* kReportDirName = "crash-reports";
constexpr const char* kReportFileName = "zene-crash-report.txt";
constexpr const char* kOfferedMarkerName = "zene-crash-report.offered";
constexpr const char* kSessionMarkerName = "zene-session-open.marker";

// Hard cap on the bytes ever written to a report file.  Proven by
// CrashReporterTest.bounds_*.
constexpr unsigned long kMaxReportBytes = 4096;

// Hard cap on the "currently open project" hint kept for the handler.
constexpr unsigned long kMaxProjectPathBytes = 512;

// POD snapshot of a crash.  Trivially copyable on purpose: it is filled in
// inside the handler and passed by const reference.
struct CrashInfo
{
	int signal = 0;
	// si_addr for the synchronous signals; 0 for signals that carry no address.
	unsigned long long faultAddress = 0;
	// Instruction pointer from the signal ucontext where the architecture
	// exposes it (x86_64, aarch64); 0 elsewhere.
	unsigned long long programCounter = 0;
	unsigned long long threadId = 0;
	unsigned long long processId = 0;
	unsigned long long unixTime = 0;
};

// Install the handlers.  `workingDirectory` must already exist (the reporter
// never creates it, so it cannot pre-empt the first-run "create the working
// directory?" prompt); an fd to it is kept open for the handler.  Safe to call
// again (re-points the fd).  Returns false if the directory cannot be opened,
// in which case nothing at all is installed or created.
// No-op on Windows.
bool install(const std::string& workingDirectory);

// Re-point the working directory (main thread, safe points only, after the
// user's real working directory is known).  Does not create anything.
bool setReportDirectory(const std::string& workingDirectory);

// True once install() has succeeded.
bool isInstalled();

// The "currently open project" hint, shown in the report so a maintainer knows
// which file was loaded.  Main thread only; the handler only reads the copy.
void setProjectPath(const std::string& path);

// --- session marker ---------------------------------------------------------
// beginSession() creates the marker; endSession() removes it on the clean exit
// path.  A marker left behind means the previous run did not exit cleanly.
void beginSession();
void endSession();
bool sessionMarkerExists();

// --- pending report ---------------------------------------------------------
// A report is "pending" until the user has been offered it once.  Offering
// writes an `offered` sentinel so the dialog is not repeated every launch;
// the report file itself stays on disk so it can still be attached.
bool hasPendingReport();
std::string pendingReportPath();
void acknowledgePendingReport();
void discardPendingReport();

// --- handler body (also the test entry point) -------------------------------
// Writes one bounded report.  Async-signal-safe.  Returns true when a report
// was written; false when the re-entrancy guard refused or no directory fd is
// open (in which case nothing is written at all).
bool writeReportIfIdle(const CrashInfo& info);

// Test seam for the re-entrancy guard: lets a test drive the "a second crash
// arrives while the first is being handled" case deterministically.  The test
// build proves the real guard; the real nesting is also exercised by
// CrashReporterTest.no_hang_on_repeated_signal.
bool reentrancyGuardActive();
void setReentrancyGuardForTest(bool active);

} // namespace lmms::crashreporter

#endif // ZENE_CRASH_REPORTER_H
