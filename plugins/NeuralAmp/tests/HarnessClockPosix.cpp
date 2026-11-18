/*
 * HarnessClockPosix.cpp - thread CPU clock for the headless NAM harness (POSIX)
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
 */

#include "HarnessClock.h"

#include <ctime>

namespace lmms::nam
{

bool tryThreadCpuTimeUs(double& value)
{
	timespec now{};
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) { return false; }
	value = static_cast<double>(now.tv_sec) * 1e6 +
		static_cast<double>(now.tv_nsec) / 1e3;
	return true;
}

double threadCpuTimeUs()
{
	double value = 0.0;
	tryThreadCpuTimeUs(value);
	return value;
}

}  // namespace lmms::nam
