/*
 * RealtimeScope.h - mark a stretch of the audio thread as realtime for RealtimeSanitizer
 *
 * Copyright (c) 2026 Zene Studio contributors
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
 */

#ifndef LMMS_REALTIME_SCOPE_H
#define LMMS_REALTIME_SCOPE_H

#ifdef LMMS_RTSAN
// The RealtimeSanitizer runtime's entry points. They are what the compiler itself calls around a
// [[clang::nonblocking]] function, so the runtime exports them, but clang 20's public
// <sanitizer/rtsan_interface.h> does not declare them (hosted run 8: "use of undeclared identifier
// '__rtsan_realtime_enter'"). Declared here, with the runtime's C linkage.
extern "C" void __rtsan_realtime_enter();
extern "C" void __rtsan_realtime_exit();
#endif

namespace lmms
{

/*! R7.2 (docs/RELEASE-0.4.0-PLAN.md M1): while one of these is alive, a build configured with
 *  WANT_DEBUG_RTSAN (clang >= 20, -fsanitize=realtime) reports every allocation, lock, blocking
 *  system call and unbounded wait the thread makes - the realtime rule of AGENTS.md checked at
 *  run time, where gate 12 (tests/rt-safety-sweep.py) checks it in the source. In every other
 *  build it is an empty object and costs nothing.
 *
 *  Scopes are placed where the audio thread's work is, not where its bookkeeping is: the render
 *  of one period AFTER AudioEngine::renderNextPeriod has taken the model lock (the upstream design
 *  every model change synchronises on), and each job a worker thread takes. */
class RealtimeScope
{
public:
	RealtimeScope() noexcept
	{
#ifdef LMMS_RTSAN
		__rtsan_realtime_enter();
#endif
	}
	~RealtimeScope() noexcept
	{
#ifdef LMMS_RTSAN
		__rtsan_realtime_exit();
#endif
	}
	RealtimeScope(const RealtimeScope&) = delete;
	RealtimeScope& operator=(const RealtimeScope&) = delete;
};

} // namespace lmms

#endif // LMMS_REALTIME_SCOPE_H
