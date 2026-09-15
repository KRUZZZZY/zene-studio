/*
 * CrashReporterWindows.cpp - the crash reporter's Windows half: deliberately a
 *                             no-op, defined so the module links everywhere.
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

// These stubs were the #else half of src/core/CrashReporter.cpp until the
// arm/disarm path (crash.enable / crash.disable, board task #643) needed room in
// that file: it is grandfathered at a zero-line tolerance in
// tests/file-length-baseline.tsv, so the room was made by splitting the file
// along the seam that was already there rather than by moving the ratchet. The
// stubs are the same definitions, unchanged; only their translation unit moved.
//
// WHY THE FAILURE IS THE FEATURE: a Windows crash reporter is a different
// implementation (minidumps via SetUnhandledExceptionFilter / vectored exception
// handling), not a port of the POSIX one. Shipping a SIGSEGV handler that CRT
// Windows does not deliver would look like coverage while providing none, so
// nothing is installed and every predicate says so - including crash.enable,
// which refuses rather than reporting an arming that did not happen.

#include "CrashReporter.h"

#include "lmmsconfig.h"

#ifdef LMMS_BUILD_WIN32

namespace lmms::crashreporter
{

bool setReportDirectory(const std::string&) { return false; }
bool install(const std::string&) { return false; }
bool uninstall() { return false; }
bool handlersArmed() { return false; }
std::string reportDirectory() { return std::string(); }
std::string handledSignalList() { return std::string(); }
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

} // namespace lmms::crashreporter

#endif // LMMS_BUILD_WIN32
