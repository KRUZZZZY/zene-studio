/*
 * CrashReporterFormat.h - the crash reporter's async-signal-safe formatting and
 *                         signal-context helpers, in their own translation unit.
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

#ifndef ZENE_CRASH_REPORTER_FORMAT_H
#define ZENE_CRASH_REPORTER_FORMAT_H

// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS (and why it is not in include/CrashReporter.h)
// ---------------------------------------------------------------------------
// These five functions and two context readers were the first ~128 lines of
// src/core/CrashReporter.cpp until the arm/disarm path (crash.enable /
// crash.disable, board task #643) needed room in that file. CrashReporter.cpp is
// grandfathered at 564 lines in tests/file-length-baseline.tsv with a
// zero-line tolerance, so the growth was PAID FOR by moving them out rather
// than by moving the ratchet: after the move that file is back under the
// 500-line limit and its baseline entry is gone, while the arm/disarm code has
// the room it needs. Nothing about the code changed in the move.
//
// NOT public API. include/CrashReporter.h is the module's public surface;
// this header is the internal seam between the two halves of the
// implementation, and it is included by src/core/CrashReporter.cpp and
// src/core/CrashReporterFormat.cpp only.
//
// ASYNC-SIGNAL-SAFETY is the reason they are separate from the rest: every
// function here is called from the signal handler (zeneCrashSignalHandler,
// src/core/CrashReporter.cpp) or from writeReportIfIdle, which the handler
// calls. They are the same functions, unchanged: no allocation, no C++ object
// construction, no stdio, no Qt call, no mutex, no unbounded loop. A fixed
// buffer with a hard cap and a bounded number of writes.

namespace lmms::crashreporter
{

//! Appends \p s at \p pos, never past \p cap; returns the new position.
unsigned long appendStr(char* buf, unsigned long cap, unsigned long pos, const char* s);

//! Like appendStr, but never reads past \p limit source bytes even if the
//! source is not NUL-terminated (a torn read of the project-path hint).
unsigned long appendCapped(char* buf, unsigned long cap, unsigned long pos,
	const char* s, unsigned long limit);

//! Decimal, then hexadecimal, both bounded and allocation-free.
unsigned long appendUInt(char* buf, unsigned long cap, unsigned long pos, unsigned long long v);
unsigned long appendHex(char* buf, unsigned long cap, unsigned long pos, unsigned long long v);

//! The signal's name, or "SIGUNKNOWN" - never a lookup table to index.
const char* signalName(int sig);

//! The faulting instruction pointer from the signal ucontext where the
//! architecture exposes it (x86_64, aarch64); 0 elsewhere.
unsigned long long programCounterFrom(void* context);

//! The thread id, straight from the kernel (gettid); 0 on Darwin, which has no
//! declared syscall() to reach one through - see the note in the .cpp.
unsigned long long currentThreadId();

} // namespace lmms::crashreporter

#endif // ZENE_CRASH_REPORTER_FORMAT_H
