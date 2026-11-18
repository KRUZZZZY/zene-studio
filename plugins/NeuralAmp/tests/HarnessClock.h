/*
 * HarnessClock.h - thread CPU clock for the headless NAM harness
 *
 * Copyright (c) 2026 AI-KOS Team
 *
 * This file is part of LMMS - https://lmms.io
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
 * The implementation is selected by plugins/NeuralAmp/tests/CMakeLists.txt:
 * HarnessClockPosix.cpp reads clock_gettime(CLOCK_THREAD_CPUTIME_ID) and
 * HarnessClockWindows.cpp reads GetThreadTimes(). The harness's own
 * translation unit must not name either platform clock: on Windows, <time.h>
 * includes winpthreads' <pthread_time.h>, whose inline clock_gettime() calls
 * clock_gettime64() - a symbol the MSYS2 clangarm64 link line does not resolve
 * (cluster ci-443a2931bcb5dacc).
 */

#ifndef LMMS_NAM_HARNESS_CLOCK_H
#define LMMS_NAM_HARNESS_CLOCK_H

namespace lmms::nam
{

/// Reads calling-thread CPU microseconds; leaves value untouched on failure.
/// Offline measurements only: never call from an audio-thread path.
bool tryThreadCpuTimeUs(double& value);

/// Returns calling-thread CPU microseconds, or 0 when the platform call fails.
double threadCpuTimeUs();

}  // namespace lmms::nam

#endif  // LMMS_NAM_HARNESS_CLOCK_H
