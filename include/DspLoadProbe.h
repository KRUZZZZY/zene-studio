/*
 * DspLoadProbe.h - how much of a period a piece of the render took
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

#ifndef LMMS_DSP_LOAD_PROBE_H
#define LMMS_DSP_LOAD_PROBE_H

#include <atomic>
#include <chrono>

#include "LmmsTypes.h"

namespace lmms
{

/*! 0.5.0 W3 (WF-104, CPU load per channel): times the scope it lives in and folds the share of the
 *  period it took - in percent of the period's duration - into @a load, smoothed over recent periods.
 *  AUDIO THREAD: two steady-clock reads and one relaxed atomic load and store; it allocates nothing,
 *  takes no lock and makes no blocking call. The reader (mixer.cpu_load) does a relaxed load. */
class DspLoadProbe
{
public:
	DspLoadProbe(std::atomic<float>& load, f_cnt_t frames, sample_rate_t rate) noexcept :
		m_load(load),
		m_periodSeconds(rate > 0 ? static_cast<double>(frames) / rate : 0.0),
		m_start(std::chrono::steady_clock::now())
	{
	}

	~DspLoadProbe()
	{
		if (m_periodSeconds <= 0.0) { return; }
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
		const auto sample = static_cast<float>(100.0 * took / m_periodSeconds);
		const float previous = m_load.load(std::memory_order_relaxed);
		m_load.store(previous + (sample - previous) * Smoothing, std::memory_order_relaxed);
	}

	DspLoadProbe(const DspLoadProbe&) = delete;
	DspLoadProbe& operator=(const DspLoadProbe&) = delete;

	//! The weight of the newest period: about the last 20 periods count.
	static constexpr float Smoothing = 0.1f;

private:
	std::atomic<float>& m_load;
	double m_periodSeconds;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace lmms

#endif // LMMS_DSP_LOAD_PROBE_H
