/*
 * HarnessClockWindows.cpp - thread CPU clock for the headless NAM harness (Win32)
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
 * Windows has no clock_gettime(CLOCK_THREAD_CPUTIME_ID). GetThreadTimes()
 * reports the calling thread's kernel and user time as FILETIMEs, i.e. in
 * 100 ns ticks, which is the same quantity the POSIX clock reports and does
 * not depend on winpthreads (cluster ci-443a2931bcb5dacc).
 */

#include "HarnessClock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

namespace lmms::nam
{

namespace
{

double fileTimeToUs(const FILETIME& time)
{
	const std::uint64_t ticks =
		(static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
		static_cast<std::uint64_t>(time.dwLowDateTime);
	return static_cast<double>(ticks) / 10.0;
}

}  // namespace

bool tryThreadCpuTimeUs(double& value)
{
	FILETIME creation{}, exitTime{}, kernel{}, user{};
	if (GetThreadTimes(GetCurrentThread(), &creation, &exitTime, &kernel, &user) == 0)
	{
		return false;
	}
	value = fileTimeToUs(kernel) + fileTimeToUs(user);
	return true;
}

double threadCpuTimeUs()
{
	double value = 0.0;
	tryThreadCpuTimeUs(value);
	return value;
}

}  // namespace lmms::nam
