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
// _XOPEN_SOURCE alone hides Darwin's BSD declarations (the Mach thread-port
// calls below), exactly as it hid O_NOFOLLOW in src/core/CrashReporter.cpp;
// _DARWIN_C_SOURCE brings them back.
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
	#define _DARWIN_C_SOURCE
#endif

#include "CrashReporterFormat.h"

#include "lmmsconfig.h"

#include <cstdint>
#include <cstring>

#ifndef LMMS_BUILD_WIN32
#include <csignal>
#include <ctime>

#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#endif // LMMS_BUILD_WIN32

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
// Darwin has no gettid and no declared syscall() to reach one through (the
// first release failed macos-arm64 with "no member named 'syscall'").  The
// portable-looking pthread_mach_thread_np(pthread_self()) is what Breakpad
// uses, but it is NOT async-signal-safe on Darwin: it can take the pthread
// lock and deadlock the handler (OpenJDK bug JDK-8235962).  The Mach port of
// the current thread is the same value, obtained through two direct Mach traps
// with no lock and no allocation: mach_thread_self() returns a send right and
// mach_port_deallocate() releases the reference we just took immediately, so
// the handler leaks nothing.  This is the fix OpenJDK shipped for exactly this
// hazard.
unsigned long long currentThreadId()
{
#if defined(__APPLE__)
	const mach_port_t port = ::mach_thread_self();
	::mach_port_deallocate(::mach_task_self(), port);
	return static_cast<unsigned long long>(port);
#else
	return static_cast<unsigned long long>(::syscall(SYS_gettid));
#endif
}

// ---- raw frames (Q050-224.2) -------------------------------------------------
//
// The walk is a RAW FRAME-POINTER WALK, not an unwinder.  The first version
// called _Unwind_Backtrace() here, and that was wrong for a signal handler:
// the general-purpose unwinder finds the FDE for every PC through the dynamic
// loader (object lookup under a loader lock) and can enter the allocator.  Its
// frame-count budget bounded the number of STEPS, not that work, so the
// handler was neither bounded nor async-signal-safe.  This version dereferences
// only the stack's own saved frame pointers: no unwinder, no loader, no
// unwind-table lookup, no allocation, no lock, no I/O.
//
// Bounds: (1) every dereference is inside [the interrupted SP, the stack
// ceiling recorded by recordThreadStackBounds()].  The ceiling is the address
// of a local in the recording frame: it is inside the same stack mapping, and
// every address below it that the crashing thread has used is mapped too, so
// the walk never touches an unmapped page; (2) each saved frame pointer must be
// 16-byte aligned and strictly greater than the previous one (the stack grows
// down, so a valid chain increases), which stops a corrupt or cyclic chain;
// (3) the frame budget.  Repeated return addresses are KEPT: a recursive chain
// reaches the same call site at every depth, and folding those would hide
// depth.
//
// Known limit, stated rather than hidden: a build compiled with frame pointers
// omitted yields fewer callers (the PC is still frame 0); a thread that never
// registered a stack, or whose crash is above the recorded ceiling (a very
// shallow frame reached after registration returned), yields the PC but no
// walk.  The reporter's own threads register through
// installAlternateStackForCurrentThread().

namespace
{

thread_local uintptr_t s_stackLow = 0;
thread_local uintptr_t s_stackCeiling = 0;

// A plausible single thread stack is never wider than this.  It is a guard
// against a registration on one stack and a fault on another (a fiber or a
// plug-in's own stack): the recorded ceiling would then be far away and the
// range check below must reject it rather than read across unmapped memory.
constexpr uintptr_t kMaxStackSpan = 32 * 1024 * 1024;

// The frame and stack pointers out of a signal ucontext, where the architecture
// lays them out.  Returns false where this build has no accessor.
bool contextStackPointers(void* context, uintptr_t& framePointer, uintptr_t& stackPointer)
{
	if (context == nullptr) { return false; }
	const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__APPLE__)
	// Darwin's ucontext_t holds a POINTER to the machine context, and names the
	// thread state __ss on both architectures.
#if defined(__aarch64__)
	framePointer = static_cast<uintptr_t>(uc->uc_mcontext->__ss.__fp);
	stackPointer = static_cast<uintptr_t>(uc->uc_mcontext->__ss.__sp);
	return true;
#else
	framePointer = static_cast<uintptr_t>(uc->uc_mcontext->__ss.__rbp);
	stackPointer = static_cast<uintptr_t>(uc->uc_mcontext->__ss.__rsp);
	return true;
#endif
#elif defined(__aarch64__)
	framePointer = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
	stackPointer = static_cast<uintptr_t>(uc->uc_mcontext.sp);
	return true;
#elif defined(__x86_64__) && defined(REG_RBP) && defined(REG_RSP)
	framePointer = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
	stackPointer = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
	return true;
#else
	(void)uc;
	return false;
#endif
}

} // namespace

void recordThreadStackBounds()
{
	// The exact bounds, from the thread's own metadata.  This runs only from
	// install() (the main thread at startup, or a test thread), never from the
	// render/JACK callbacks: pthread_getattr_np allocates (its CPU-set buffer)
	// and is not real-time safe.  The cheap marker below covers those callers.
#if defined(__APPLE__)
	void* top = ::pthread_get_stackaddr_np(::pthread_self());
	const size_t size = ::pthread_get_stacksize_np(::pthread_self());
	if (top != nullptr && size > 0)
	{
		const uintptr_t high = reinterpret_cast<uintptr_t>(top);
		s_stackLow = high - size;
		s_stackCeiling = high;
	}
#elif defined(__linux__)
	pthread_attr_t attr;
	if (::pthread_getattr_np(::pthread_self(), &attr) != 0) { return; }
	void* base = nullptr;
	size_t size = 0;
	if (::pthread_attr_getstack(&attr, &base, &size) == 0 && base != nullptr && size > 0)
	{
		s_stackLow = reinterpret_cast<uintptr_t>(base);
		s_stackCeiling = s_stackLow + size;
	}
	::pthread_attr_destroy(&attr);
#endif
}

void noteThreadStackMarker()
{
	// The whole real-time-safe half is taking one local's address: no allocation,
	// no lock, no I/O.  The first registration on a thread is its shallowest
	// call, so keeping the maximum gives a ceiling above every later frame (and
	// never lowers the exact ceiling install() recorded on the main thread).
	const uintptr_t marker = reinterpret_cast<uintptr_t>(&marker);
	if (marker > s_stackCeiling) { s_stackCeiling = marker; }
}

int captureFrames(void* context, unsigned long long* out, int capacity)
{
	if (out == nullptr || capacity <= 0) { return 0; }

	// The faulting instruction first: it is the crash site, and the one address
	// that must never be missing from the sequence.
	int count = 0;
	const unsigned long long pc = programCounterFrom(context);
	if (pc != 0) { out[count++] = pc; }
	if (count >= capacity) { return count; }

	if (context == nullptr)
	{
		// Outside a signal there is no ucontext to read the frame pointer from,
		// and __builtin_frame_address() is not trustworthy in a build that
		// omits frame pointers. __builtin_return_address(0) is always a real
		// return address, so it is the one honest caller we can add here.
#if defined(__GNUC__) && !defined(_WIN32)
		const uintptr_t caller =
			reinterpret_cast<uintptr_t>(__builtin_return_address(0));
		if (caller != 0 && count < capacity)
		{
			out[count++] = static_cast<unsigned long long>(caller);
		}
#endif
		return count;
	}

	uintptr_t framePointer = 0;
	uintptr_t stackPointer = 0;
	if (!contextStackPointers(context, framePointer, stackPointer)) { return count; }

	const uintptr_t word = sizeof(uintptr_t);
	const uintptr_t alignMask = (2 * word) - 1;
	const uintptr_t low = stackPointer;
	const uintptr_t high = s_stackCeiling;
	// No recorded ceiling means no dereference at all: a wrong read would fault
	// inside the handler and lose the report. PC is still captured above.
	if (high == 0 || high <= low + 2 * word) { return count; }
	if (high - low > kMaxStackSpan) { return count; }
	// With exact bounds (install-time), the interrupted SP must be on that same
	// stack.  A marker-only thread (render/JACK) has no recorded low, so the
	// interrupted SP is its own lower bound.
	if (s_stackLow != 0 && low < s_stackLow) { return count; }
	if (framePointer < low || framePointer > high - 2 * word) { return count; }

	while (count < capacity)
	{
		if ((framePointer & alignMask) != 0
			|| framePointer < low || framePointer > high - 2 * word)
		{
			break;
		}
		const uintptr_t savedFrame =
			*reinterpret_cast<const uintptr_t*>(framePointer);
		const uintptr_t returnAddress =
			*reinterpret_cast<const uintptr_t*>(framePointer + word);
		if (returnAddress != 0) { out[count++] = returnAddress; }
		if (savedFrame <= framePointer || savedFrame > high - 2 * word
			|| (savedFrame & alignMask) != 0)
		{
			break;
		}
		framePointer = savedFrame;
	}
	return count;
}

unsigned long appendFrames(char* buf, unsigned long cap, unsigned long pos,
	const unsigned long long* frames, int count)
{
	pos = appendStr(buf, cap, pos, "frames=");
	pos = appendUInt(buf, cap, pos,
		static_cast<unsigned long long>(count > 0 ? count : 0));
	pos = appendStr(buf, cap, pos, "\n");
	for (int i = 0; i < count && frames != nullptr; ++i)
	{
		pos = appendStr(buf, cap, pos, "frame[");
		pos = appendUInt(buf, cap, pos, static_cast<unsigned long long>(i));
		pos = appendStr(buf, cap, pos, "]=");
		pos = appendHex(buf, cap, pos, frames[i]);
		pos = appendStr(buf, cap, pos, "\n");
	}
	return pos;
}

#else // LMMS_BUILD_WIN32

// Windows installs no handler (see include/CrashReporter.h), so nothing captures
// or formats frames there; these stubs exist so the module links wherever it is
// compiled, exactly like the rest of the Windows half.
int captureFrames(void*, unsigned long long*, int) { return 0; }

unsigned long appendFrames(char* buf, unsigned long, unsigned long pos,
	const unsigned long long*, int) { return pos; }

#endif // LMMS_BUILD_WIN32

} // namespace lmms::crashreporter
