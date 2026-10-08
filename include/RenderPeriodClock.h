/*
 * RenderPeriodClock.h - when the audio period being rendered started
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_RENDER_PERIOD_CLOCK_H
#define LMMS_RENDER_PERIOD_CLOCK_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

namespace lmms
{

//! The steady-clock instant the audio engine began the period it is rendering.
/*!
 * The render thread stores it at the start of renderNextPeriod(); a remote plugin reads it to budget its
 * wait for the client to the end of that period. Only the render thread writes it; a single atomic word,
 * so reading it never locks or allocates.
 */
class RenderPeriodClock
{
public:
	static void markPeriodStart() noexcept
	{
		s_renderThread.store(threadKey(), std::memory_order_relaxed);
		s_startNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
		s_inPeriod.store(true, std::memory_order_release);
	}

	static void markPeriodEnd() noexcept
	{
		s_inPeriod.store(false, std::memory_order_release);
	}

	//! Whether the calling thread is the one rendering a period right now. Only then is periodStart() the start of
	//! the period the caller is part of; elsewhere (tests, offline callers) it is not.
	static auto renderingOnThisThread() noexcept -> bool
	{
		return s_inPeriod.load(std::memory_order_acquire)
			&& s_renderThread.load(std::memory_order_relaxed) == threadKey();
	}

	static auto periodStart() noexcept -> std::chrono::steady_clock::time_point
	{
		return std::chrono::steady_clock::time_point{
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::nanoseconds{s_startNs.load(std::memory_order_relaxed)})};
	}

private:
	static std::uint64_t threadKey() noexcept
	{
		return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	}

	static inline std::atomic<std::int64_t> s_startNs{0};
	static inline std::atomic<std::uint64_t> s_renderThread{0};
	static inline std::atomic<bool> s_inPeriod{false};
};

} // namespace lmms

#endif // LMMS_RENDER_PERIOD_CLOCK_H
