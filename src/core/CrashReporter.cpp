/*
 * CrashReporter.cpp - minimal, offline, local-only crash reporter
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

// Darwin's <ucontext.h> refuses to declare the context accessors below unless
// _XOPEN_SOURCE is defined before it is first included (macos-arm64 said so:
// "ucontext.h:51:2: error: The deprecated ucontext routines require _XOPEN_SOURCE
// to be defined"). Apple-only, so glibc's feature visibility is untouched.
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
	#define _XOPEN_SOURCE 700
#endif
// _XOPEN_SOURCE alone hides Darwin's BSD extensions (O_NOFOLLOW below: macos-arm64 "use of undeclared
// identifier 'O_NOFOLLOW'", hosted run 37892087548); _DARWIN_C_SOURCE brings them back.
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
	#define _DARWIN_C_SOURCE
#endif

#include "CrashReporter.h"
#include "CrashReporterFormat.h"

#include "lmmsconfig.h"
#include "lmmsversion.h"
#include "versioninfo.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#ifndef LMMS_BUILD_WIN32
#include <csignal>
#include <cstring>
#include <ctime>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace lmms::crashreporter
{

#ifndef LMMS_BUILD_WIN32

namespace
{

// ---- handler-visible state (written on the main thread, read by the handler) ----

// Lock-free atomics are async-signal-safe, and unlike `volatile sig_atomic_t`
// they are also safe across threads: two threads faulting together race on
// these, and setReportDirectory()/uninstall() run beside a crashing thread.
static_assert(std::atomic<int>::is_always_lock_free);

// Re-entrancy guard, claimed with exchange() so exactly one handler - on any
// thread - writes the report; the handler never blocks, locks or waits.
std::atomic<int> s_active{0};

// Directory fd, opened once at install time.  The handler reuses it with
// openat() so it never has to resolve a path (or take the FS lock twice).
// Replaced by publishing the new fd BEFORE the old one is closed.
std::atomic<int> s_dirFd{-1};

// "Currently open project" hint, double-buffered: setProjectPath() fills the
// slot the handler is not reading and then publishes it, so the handler never
// reads a buffer while it is being cleared.  Always NUL-terminated; the reader
// scans for the NUL within the cap.
alignas(16) char s_projectPath[2][kMaxProjectPathBytes] = {"(none)", "(none)"};
std::atomic<int> s_projectSlot{0};

// A crash from an exhausted stack cannot run a handler on that stack, so the
// handlers are installed with SA_ONSTACK and this dedicated stack.  It is
// per-thread (POSIX sigaltstack is a thread attribute): the thread that calls
// install() gets it.  See the "not proven" list in docs/CRASH-REPORTER.md.
alignas(16) char s_altStack[32 * 1024];

bool s_installed = false;

// The directory the public, main-thread API uses.  The handler does not read
// this; it only uses s_dirFd.
std::string s_dir;

// ---- the signal handler ----
// Calls only async-signal-safe entry points: openat, write, close, sigaction,
// sigemptyset, sigprocmask, raise, getpid, time, and the raw gettid syscall.
// No malloc, no C++ object construction, no stdio, no Qt, no lock.

void zeneCrashSignalHandler(int signum, siginfo_t* info, void* context)
{
	CrashInfo ci;
	ci.signal = signum;
	switch (signum)
	{
		case SIGSEGV:
		case SIGBUS:
		case SIGILL:
		case SIGFPE:
			// The synchronous signals carry the faulting address in si_addr.
			if (info != nullptr)
			{
				ci.faultAddress = static_cast<unsigned long long>(
					reinterpret_cast<uintptr_t>(info->si_addr));
			}
			break;
		default:
			// SIGABRT carries no meaningful address; leave it 0.
			break;
	}
	ci.programCounter = programCounterFrom(context);
	ci.threadId = currentThreadId();
	ci.processId = static_cast<unsigned long long>(::getpid());
	ci.unixTime = static_cast<unsigned long long>(::time(nullptr));

	writeReportIfIdle(ci);

	// Die exactly the way this process would have died without the reporter:
	// restore the default disposition, unblock the signal and re-raise it, so
	// the parent and the shell still observe death-by-signal.
	struct sigaction dfl;
	std::memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(signum, &dfl, nullptr);

	sigset_t unblock;
	sigemptyset(&unblock);
	sigaddset(&unblock, signum);
	sigprocmask(SIG_UNBLOCK, &unblock, nullptr);

	raise(signum);

	// Unreachable in practice; kept so the handler can never return into a
	// corrupt frame even if the re-raise is swallowed.
	_exit(128 + signum);
}

bool pathExists(const std::string& path)
{
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

bool writeSmallFile(const std::string& path, const std::string& contents)
{
	// O_NOFOLLOW: a symlink planted at the marker's name is refused, not truncated through.
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) { return false; }
	std::size_t off = 0;
	while (off < contents.size())
	{
		const ssize_t n = ::write(fd, contents.data() + off, contents.size() - off);
		if (n <= 0) { ::close(fd); return false; }
		off += static_cast<std::size_t>(n);
	}
	::close(fd);
	return true;
}

std::string fileInDir(const std::string& dir, const char* name)
{
	if (dir.empty()) { return std::string(); }
	std::string p = dir;
	p += '/';
	p += name;
	return p;
}

// Report files live one level down, in <dir>/crash-reports/.
std::string reportFileInDir(const std::string& dir, const char* name)
{
	if (dir.empty()) { return std::string(); }
	std::string p = dir;
	p += '/';
	p += kReportDirName;
	p += '/';
	p += name;
	return p;
}

/*! The signals the reporter claims - the ONE place the set is written down.
 *  install(), uninstall() and handlersArmed() all read it: a second copy of the
 *  set is how a disarmed reporter leaves one signal armed, or an armed one
 *  misses one. LMMS_DEBUG_FPE keeps its exception (main.cpp) here.
 */
const int* handlerSignals(int& count)
{
	static const int kSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE };
	count = 5;
#ifdef LMMS_DEBUG_FPE
	count = 4;
#endif
	return kSignals;
}

bool writeReportFile(int fd, int subFd, const char* buf, unsigned long size)
{
	// Bounded write loop: at most 8 write() calls and never more than
	// kMaxReportBytes bytes, whatever the filesystem does.
	unsigned long off = 0;
	int attempts = 0;
	while (off < size && attempts < 8)
	{
		const ssize_t n = ::write(fd, buf + off, size - off);
		if (n > 0) { off += static_cast<unsigned long>(n); }
		else if (n < 0 && errno == EINTR) { /* retry, still capped */ }
		else { break; }
		++attempts;
	}

	const bool wrote = (off == size);
	::close(fd);
	if (!wrote) { ::unlinkat(subFd, kReportFileName, 0); }
	return wrote;
}

} // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

bool setReportDirectory(const std::string& workingDirectory)
{
	if (workingDirectory.empty()) { return false; }

	// Open only: the reporter must never create the user's working directory,
	// or it would silently answer the first-run "create it?" prompt for them.
	// If the directory does not exist yet, no handlers are installed and the
	// caller can retry once it does (main.cpp does exactly that after
	// GuiApplication has run the prompt).
	const int fd = ::open(workingDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) { return false; }

	const int old = s_dirFd.exchange(fd);
	if (old >= 0 && old != fd) { ::close(old); }
	s_dir = workingDirectory;
	return true;
}

bool install(const std::string& workingDirectory)
{
	if (!setReportDirectory(workingDirectory)) { return false; }

	if (s_installed) { return true; }

	// The handler's own stack, so an exhausted stack is still reportable.
	stack_t ss;
	std::memset(&ss, 0, sizeof(ss));
	ss.ss_sp = s_altStack;
	ss.ss_size = sizeof(s_altStack);
	ss.ss_flags = 0;
	if (::sigaltstack(&ss, nullptr) != 0)
	{
		// Non-fatal: without it a stack-overflow crash is not reportable.
	}

	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = &zeneCrashSignalHandler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);

	int count = 0;
	const int* signals = handlerSignals(count);
	for (int i = 0; i < count; ++i)
	{
		sigaction(signals[i], &sa, nullptr);
	}

	s_installed = true;
	return true;
}

/*! Restore the default disposition for exactly the signals install() claimed
 *  (one shared list: handlerSignals), drop the report-directory fd and clear
 *  s_installed. It deletes nothing and keeps the remembered directory, so
 *  crash.enable re-arms to the same place. See include/CrashReporter.h for the
 *  full statement of what "disarmed" does and does not mean.
 */
bool uninstall()
{
	if (!s_installed) { return false; }

	struct sigaction dfl;
	std::memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);

	int count = 0;
	const int* signals = handlerSignals(count);
	for (int i = 0; i < count; ++i)
	{
		sigaction(signals[i], &dfl, nullptr);
	}

	if (const int old = s_dirFd.exchange(-1); old >= 0) { ::close(old); }
	s_installed = false;
	return true;
}

/*! Is the handler ACTUALLY armed? Reads the kernel's own dispositions, not a
 *  flag: install() sets every signal of handlerSignals() to the crash handler,
 *  and anything else (SIG_DFL after uninstall(), SIG_IGN, a handler someone
 *  installed over ours) reads false. See include/CrashReporter.h.
 */
bool handlersArmed()
{
	int count = 0;
	const int* signals = handlerSignals(count);
	for (int i = 0; i < count; ++i)
	{
		struct sigaction current;
		std::memset(&current, 0, sizeof(current));
		if (sigaction(signals[i], nullptr, &current) != 0) { return false; }
		if (current.sa_sigaction != &zeneCrashSignalHandler) { return false; }
	}
	return count > 0;
}

//! The working directory the reporter was last handed (main.cpp's
//! ConfigManager::workingDir()), or empty when it has never been installed. What
//! crash.enable re-arms to with no arguments, because uninstall() keeps it.
std::string reportDirectory()
{
	return s_dir;
}

//! The signals the reporter claims, as text, in the order install() sets them:
//! "SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE" (SIGFPE absent under
//! LMMS_DEBUG_FPE). Built from the SAME list install(), uninstall() and
//! handlersArmed() read, so the control surface cannot name a set the handler
//! does not catch. Empty on Windows, where nothing is ever installed.
std::string handledSignalList()
{
	std::string out;
	int count = 0;
	const int* signals = handlerSignals(count);
	for (int i = 0; i < count; ++i)
	{
		if (i > 0) { out += ", "; }
		out += signalName(signals[i]);
	}
	return out;
}

bool isInstalled()
{
	return s_installed;
}

void setProjectPath(const std::string& path)
{
	const std::string value = path.empty() ? std::string("(none)") : path;
	const int slot = 1 - s_projectSlot.load(std::memory_order_relaxed);
	char* target = s_projectPath[slot];
	std::memset(target, 0, kMaxProjectPathBytes);
	const std::size_t n = value.size() < (kMaxProjectPathBytes - 1)
		? value.size() : (kMaxProjectPathBytes - 1);
	std::memcpy(target, value.data(), n);
	target[n] = '\0';
	s_projectSlot.store(slot, std::memory_order_release);
}

void beginSession()
{
	if (s_dir.empty()) { return; }
	const std::string marker = fileInDir(s_dir, kSessionMarkerName);
	const std::string body = "pid=" + std::to_string(static_cast<long>(::getpid())) + "\n";
	writeSmallFile(marker, body);
}

void endSession()
{
	if (s_dir.empty()) { return; }
	::unlink(fileInDir(s_dir, kSessionMarkerName).c_str());
}

bool sessionMarkerExists()
{
	return !s_dir.empty() && pathExists(fileInDir(s_dir, kSessionMarkerName));
}

bool hasPendingReport()
{
	if (s_dir.empty()) { return false; }
	return pathExists(reportFileInDir(s_dir, kReportFileName))
		&& !pathExists(reportFileInDir(s_dir, kOfferedMarkerName));
}

std::string pendingReportPath()
{
	if (s_dir.empty()) { return std::string(); }
	return reportFileInDir(s_dir, kReportFileName);
}

void acknowledgePendingReport()
{
	if (s_dir.empty()) { return; }
	writeSmallFile(reportFileInDir(s_dir, kOfferedMarkerName), "offered\n");
}

void discardPendingReport()
{
	if (s_dir.empty()) { return; }
	::unlink(reportFileInDir(s_dir, kReportFileName).c_str());
	::unlink(reportFileInDir(s_dir, kOfferedMarkerName).c_str());
}

bool writeReportIfIdle(const CrashInfo& info)
{
	if (s_active.exchange(1) != 0)
	{
		// A crash arrived while a crash was being handled (on this thread or
		// another).  Refuse to write: recurse no further, hang on nothing, and
		// let the caller re-raise.
		return false;
	}

	bool wrote = false;
	int fd = -1;
	int subFd = -1;
	const int dirFd = s_dirFd.load();
	if (dirFd >= 0)
	{
		// The report directory is created here, at crash time, and only here:
		// mkdirat() is on POSIX's async-signal-safe list, so a run that never
		// crashes never creates it.  EEXIST is the normal case after the first
		// crash in a working directory.
		// O_NOFOLLOW on both: a symlink planted at either name is refused
		// rather than followed to truncate whatever it points at.
		::mkdirat(dirFd, kReportDirName, 0700);
		subFd = ::openat(dirFd, kReportDirName,
			O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (subFd >= 0)
		{
			// This report is a NEW crash: clear the marker left by the last
			// offered one, or hasPendingReport() would call this crash already
			// offered.  unlinkat() is async-signal-safe, like the rest of here.
			::unlinkat(subFd, kOfferedMarkerName, 0);
			fd = ::openat(subFd, kReportFileName,
				O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
		}
	}

	if (fd >= 0)
	{
		char buf[kMaxReportBytes];
		const unsigned long cap = kMaxReportBytes;
		unsigned long pos = 0;

		pos = appendStr(buf, cap, pos, "Zene Studio crash report v1\n");
		pos = appendStr(buf, cap, pos, "signal=");
		pos = appendStr(buf, cap, pos, signalName(info.signal));
		pos = appendStr(buf, cap, pos, "(");
		pos = appendUInt(buf, cap, pos, static_cast<unsigned long long>(info.signal));
		pos = appendStr(buf, cap, pos, ")\n");
		pos = appendStr(buf, cap, pos, "fault_addr=");
		pos = appendHex(buf, cap, pos, info.faultAddress);
		pos = appendStr(buf, cap, pos, "\npc=");
		pos = appendHex(buf, cap, pos, info.programCounter);
		pos = appendStr(buf, cap, pos, "\nthread=");
		pos = appendUInt(buf, cap, pos, info.threadId);
		pos = appendStr(buf, cap, pos, "\npid=");
		pos = appendUInt(buf, cap, pos, info.processId);
		pos = appendStr(buf, cap, pos, "\ntime_unix=");
		pos = appendUInt(buf, cap, pos, info.unixTime);
		pos = appendStr(buf, cap, pos, "\nversion=");
		pos = appendStr(buf, cap, pos, LMMS_VERSION);
		pos = appendStr(buf, cap, pos, "\nplatform=");
		pos = appendStr(buf, cap, pos, LMMS_BUILDCONF_PLATFORM);
		pos = appendStr(buf, cap, pos, " ");
		pos = appendStr(buf, cap, pos, LMMS_BUILDCONF_MACHINE);
		pos = appendStr(buf, cap, pos, "\ncompiler=");
		pos = appendStr(buf, cap, pos, LMMS_BUILDCONF_COMPILER_VERSION);
		pos = appendStr(buf, cap, pos, "\nproject=");
		pos = appendCapped(buf, cap, pos, s_projectPath[s_projectSlot.load(std::memory_order_acquire)],
			kMaxProjectPathBytes);
		pos = appendStr(buf, cap, pos, "\n");

		wrote = writeReportFile(fd, subFd, buf, pos);
	}
	if (subFd >= 0) { ::close(subFd); }

	s_active.store(0);
	return wrote;
}

bool reentrancyGuardActive()
{
	return s_active.load() != 0;
}

void setReentrancyGuardForTest(bool active)
{
	s_active.store(active ? 1 : 0);
}

#endif // LMMS_BUILD_WIN32

} // namespace lmms::crashreporter
