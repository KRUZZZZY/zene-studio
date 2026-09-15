/*
 * CrashReporterFormat.cpp - the crash reporter's async-signal-safe formatting
 *                           and signal-context helpers.
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
 */

// Darwin's <ucontext.h> refuses to declare the context accessors below unless
// _XOPEN_SOURCE is defined before it is first included (macos-arm64 said so:
// "ucontext.h:51:2: error: The deprecated ucontext routines require _XOPEN_SOURCE
// to be defined"). Apple-only, so glibc's feature visibility is untouched. This
// preamble follows the accessors: it moved here with them from
// src/core/CrashReporter.cpp, which keeps its own copy for its own <ucontext.h>
// include.
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
	#define _XOPEN_SOURCE 700
#endif

#include "CrashReporterFormat.h"

#include "lmmsconfig.h"

#include <cstdint>
#include <cstring>

#ifndef LMMS_BUILD_WIN32
#include <csignal>
#include <ctime>

#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace lmms::crashreporter
{

#ifndef LMMS_BUILD_WIN32

// The four append helpers and the three context readers are the SAME CODE that
// lived in src/core/CrashReporter.cpp, moved verbatim so the arm/disarm path
// could fit in that file at its zero-tolerance length ratchet (see
// include/CrashReporterFormat.h). They are called from the signal handler and
// from writeReportIfIdle: fixed buffers, bounded loops, no allocation, no lock.

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
#if defined(__APPLE__)
	// Darwin's ucontext_t holds a POINTER to the machine context (Linux's is a struct)
	// and names the thread state __ss on both architectures, while the program counter
	// inside it is __pc on arm64 and __rip on x86_64. Taking the Linux shape here is
	// what failed macos-arm64 ("no member named 'pc' in '__darwin_mcontext64'"); note
	// that Darwin defines no REG_RIP, so the x86_64 half returned 0 before this branch.
	if (context != nullptr)
	{
		const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__aarch64__)
		return static_cast<unsigned long long>(uc->uc_mcontext->__ss.__pc);
#else
		return static_cast<unsigned long long>(uc->uc_mcontext->__ss.__rip);
#endif
	}
#elif defined(__aarch64__)
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
//
// Darwin is the exception, and it is a deliberate one: it has no gettid and no
// declared syscall() to reach one through, so the report carries 0 rather than
// calling into pthread from a signal handler -- macos-arm64 and macos-x86_64
// both failed this file with
//   CrashReporter.cpp:204:43: error: no member named 'syscall' in the global
//   namespace; did you mean 'sysconf'?
// A tid from pthread_mach_thread_np(pthread_self()) is the obvious candidate and
// is what Breakpad uses, but this box cannot compile Darwin code, so the release
// does not depend on an API choice nobody here can test. The pc, the signal and
// the backtrace are the report's payload; on macOS the tid field reads 0.
unsigned long long currentThreadId()
{
#if defined(__APPLE__)
	return 0;
#else
	return static_cast<unsigned long long>(::syscall(SYS_gettid));
#endif
}

#endif // LMMS_BUILD_WIN32

} // namespace lmms::crashreporter
