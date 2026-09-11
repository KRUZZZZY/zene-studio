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

#include "CrashReporter.h"

#include "lmmsconfig.h"
#include "lmmsversion.h"
#include "versioninfo.h"

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

// Re-entrancy guard.  `volatile sig_atomic_t` is the only type the standard
// blesses for this job; the handler never blocks, locks or waits.
volatile sig_atomic_t s_active = 0;

// Directory fd, opened once at install time.  The handler reuses it with
// openat() so it never has to resolve a path (or take the FS lock twice).
int s_dirFd = -1;

// "Currently open project" hint.  Always NUL-terminated; the reader scans for
// the NUL within the cap, so a torn read is still a safe read.
alignas(16) char s_projectPath[kMaxProjectPathBytes] = "(none)";

// A crash from an exhausted stack cannot run a handler on that stack, so the
// handlers are installed with SA_ONSTACK and this dedicated stack.  It is
// per-thread (POSIX sigaltstack is a thread attribute): the thread that calls
// install() gets it.  See the "not proven" list in docs/CRASH-REPORTER.md.
alignas(16) char s_altStack[32 * 1024];

bool s_installed = false;

// The directory the public, main-thread API uses.  The handler does not read
// this; it only uses s_dirFd.
std::string s_dir;

// ---- bounded, allocation-free formatting ----
// These touch one fixed buffer and never allocate, so they are safe to call
// from the signal handler.

unsigned long appendStr(char* buf, unsigned long cap, unsigned long pos, const char* s)
{
	while (s != nullptr && *s != '\0' && pos < cap) { buf[pos++] = *s++; }
	return pos;
}

// Like appendStr, but never reads past `limit` source bytes even if the source
// is not NUL-terminated (a torn read of s_projectPath).
unsigned long appendCapped(char* buf, unsigned long cap, unsigned long pos,
	const char* s, unsigned long limit)
{
	for (unsigned long i = 0; i < limit && pos < cap; ++i)
	{
		const char c = s[i];
		if (c == '\0') { break; }
		buf[pos++] = c;
	}
	return pos;
}

unsigned long appendUInt(char* buf, unsigned long cap, unsigned long pos, unsigned long long v)
{
	char tmp[24];
	int n = 0;
	if (v == 0) { tmp[n++] = '0'; }
	while (v != 0 && n < static_cast<int>(sizeof(tmp)))
	{
		tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10));
		v /= 10;
	}
	while (n > 0 && pos < cap) { buf[pos++] = tmp[--n]; }
	return pos;
}

unsigned long appendHex(char* buf, unsigned long cap, unsigned long pos, unsigned long long v)
{
	pos = appendStr(buf, cap, pos, "0x");
	char tmp[20];
	int n = 0;
	if (v == 0) { tmp[n++] = '0'; }
	while (v != 0 && n < static_cast<int>(sizeof(tmp)))
	{
		const unsigned int d = static_cast<unsigned int>(v & 0xF);
		tmp[n++] = static_cast<char>(d < 10 ? ('0' + static_cast<int>(d))
										   : ('a' + static_cast<int>(d - 10)));
		v >>= 4;
	}
	while (n > 0 && pos < cap) { buf[pos++] = tmp[--n]; }
	return pos;
}

const char* signalName(int sig)
{
	switch (sig)
	{
		case SIGSEGV: return "SIGSEGV";
		case SIGBUS:  return "SIGBUS";
		case SIGILL:  return "SIGILL";
		case SIGFPE:  return "SIGFPE";
		case SIGABRT: return "SIGABRT";
		default:      return "SIGUNKNOWN";
	}
}

// The faulting instruction pointer, where the architecture lays it out in the
// signal ucontext.  Falls back to 0 rather than guessing.
unsigned long long programCounterFrom(void* context)
{
#if defined(__aarch64__)
	if (context != nullptr)
	{
		const auto* uc = static_cast<const ucontext_t*>(context);
		return static_cast<unsigned long long>(uc->uc_mcontext.pc);
	}
#elif defined(__x86_64__) && defined(REG_RIP)
	if (context != nullptr)
	{
		const auto* uc = static_cast<const ucontext_t*>(context);
		return static_cast<unsigned long long>(uc->uc_mcontext.gregs[REG_RIP]);
	}
#else
	(void)context;
#endif
	return 0;
}

// The thread id, straight from the kernel.  syscall() is not on POSIX's
// async-signal-safe list, but SYS_gettid is a direct kernel entry: no
// allocation, no lock, no errno other than a thread-local write.  This is the
// same call Breakpad and Crashpad make from their handlers.
unsigned long long currentThreadId()
{
	return static_cast<unsigned long long>(::syscall(SYS_gettid));
}

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
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
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

	if (s_dirFd >= 0 && s_dirFd != fd) { ::close(s_dirFd); }
	s_dirFd = fd;
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

	int signals[5] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE };
	int count = 5;
#ifdef LMMS_DEBUG_FPE
	// A debug build has already claimed SIGFPE with its own backtrace handler
	// (main.cpp); do not steal it, and do not pretend to handle something we
	// deliberately did not install.
	count = 4;
#endif
	for (int i = 0; i < count; ++i)
	{
		sigaction(signals[i], &sa, nullptr);
	}

	s_installed = true;
	return true;
}

bool isInstalled()
{
	return s_installed;
}

void setProjectPath(const std::string& path)
{
	const std::string value = path.empty() ? std::string("(none)") : path;
	std::memset(s_projectPath, 0, sizeof(s_projectPath));
	const std::size_t n = value.size() < (kMaxProjectPathBytes - 1)
		? value.size() : (kMaxProjectPathBytes - 1);
	std::memcpy(s_projectPath, value.data(), n);
	s_projectPath[n] = '\0';
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
	if (s_active != 0)
	{
		// A crash arrived while a crash was being handled.  Refuse to write:
		// recurse no further, hang on nothing, and let the caller re-raise.
		return false;
	}
	s_active = 1;

	bool wrote = false;
	int fd = -1;
	if (s_dirFd >= 0)
	{
		// The report directory is created here, at crash time, and only here:
		// mkdirat() is on POSIX's async-signal-safe list, so a run that never
		// crashes never creates it.  EEXIST is the normal case after the first
		// crash in a working directory.
		::mkdirat(s_dirFd, kReportDirName, 0700);
		const int subFd = ::openat(s_dirFd, kReportDirName,
			O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (subFd >= 0)
		{
			fd = ::openat(subFd, kReportFileName,
				O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
			::close(subFd);
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
		pos = appendCapped(buf, cap, pos, s_projectPath, kMaxProjectPathBytes);
		pos = appendStr(buf, cap, pos, "\n");

		// Bounded write loop: at most 8 write() calls and never more than
		// kMaxReportBytes bytes, whatever the filesystem does.
		unsigned long off = 0;
		int attempts = 0;
		while (off < pos && attempts < 8)
		{
			const ssize_t n = ::write(fd, buf + off, pos - off);
			if (n > 0) { off += static_cast<unsigned long>(n); }
			else if (n < 0 && errno == EINTR) { /* retry, still capped */ }
			else { break; }
			++attempts;
		}
		wrote = (off == pos);
		::close(fd);
	}

	s_active = 0;
	return wrote;
}

bool reentrancyGuardActive()
{
	return s_active != 0;
}

void setReentrancyGuardForTest(bool active)
{
	s_active = active ? 1 : 0;
}

#else // LMMS_BUILD_WIN32

// ---------------------------------------------------------------------------
// Windows: deliberately a no-op for v1.
//
// A Windows crash reporter is a different implementation (minidumps via
// SetUnhandledExceptionFilter / vectored exception handling), not a port of
// this one.  Shipping a SIGSEGV handler that CRT Windows does not deliver
// would look like coverage while providing none, so nothing is installed and
// every predicate says so.
// ---------------------------------------------------------------------------

bool setReportDirectory(const std::string&) { return false; }
bool install(const std::string&) { return false; }
bool isInstalled() { return false; }
void setProjectPath(const std::string&) {}
void beginSession() {}
void endSession() {}
bool sessionMarkerExists() { return false; }
bool hasPendingReport() { return false; }
std::string pendingReportPath() { return std::string(); }
void acknowledgePendingReport() {}
void discardPendingReport() {}
bool writeReportIfIdle(const CrashInfo&) { return false; }
bool reentrancyGuardActive() { return false; }
void setReentrancyGuardForTest(bool) {}

#endif // LMMS_BUILD_WIN32

} // namespace lmms::crashreporter
