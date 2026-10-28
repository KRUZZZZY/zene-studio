/*
 * RenderDiagnostics.h - each instrument track's synthesis load and the engine's late periods
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

#ifndef LMMS_RENDER_DIAGNOSTICS_H
#define LMMS_RENDER_DIAGNOSTICS_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

/*! Q050-074.1 (WF-104, R-performance-48/71): how much of the audio period one instrument track's
 *  synthesis takes - the engine's Instruments stage (STAGE 1) spent in that track's play handles, the
 *  notes and the instrument's own stream, in percent of the period. It is NOT the track's mixer channel
 *  or effect load (MixerChannel::dspLoad, Effect::dspLoad): those run after it, in STAGE 2 and 3.
 *  Play handles run on several render workers at once, so the figure is CPU time per period, which can
 *  pass 100 on a machine with more than one core.
 *  AUDIO THREAD: add() is one relaxed fetch_add from any render worker; fold() runs once per period on
 *  the render thread after the workers finished; both allocate nothing and take no lock. */
class LMMS_EXPORT SynthesisLoad
{
	static_assert(std::atomic<float>::is_always_lock_free, "the synthesis load must be a lock-free atomic");

public:
	//! Times its scope into @a load; a null @a load (a handle that is not an instrument's) does nothing.
	class Probe
	{
	public:
		explicit Probe(SynthesisLoad* load) noexcept :
			m_load(load),
			m_start(load != nullptr ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{})
		{
		}
		~Probe()
		{
			if (m_load != nullptr) { m_load->add(std::chrono::steady_clock::now() - m_start); }
		}
		Probe(const Probe&) = delete;
		Probe& operator=(const Probe&) = delete;

	private:
		SynthesisLoad* m_load;
		std::chrono::steady_clock::time_point m_start;
	};

	void add(std::chrono::steady_clock::duration took) noexcept
	{
		const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(took).count();
		m_nanos.fetch_add(nanos > 0 ? static_cast<std::uint64_t>(nanos) : 0u, std::memory_order_relaxed);
	}

	/*! Folds what this period's handles took into the smoothed figure (DspLoadProbe's smoothing). A track's
	 *  handles call it once each, so the second call of a period finds @a period already folded and returns.
	 *  The periods in which the track had no handle count as zero: they decay the figure when it next folds,
	 *  and percent() decays it for a reader in between. Render thread only. */
	void fold(std::uint64_t period, double periodSeconds) noexcept;

	//! The smoothed share of the period, as of the engine's period @a currentPeriod. Any thread.
	float percent(std::uint64_t currentPeriod) const noexcept;

private:
	std::atomic<std::uint64_t> m_nanos{0};
	std::atomic<float> m_percent{0.0f};
	std::atomic<std::uint64_t> m_foldedPeriod{0};
};

/*! Q050-074.1 (R-performance-78): the engine's deadline misses - render periods that took longer than the
 *  period lasts - each at the song position its period started at. These are the ENGINE's late periods,
 *  not the audio backend's xruns (a device can drop a period for its own reasons, and a late render can
 *  still be absorbed by the device's buffer) and not the overload shedding of AudioEngine::criticalXRuns().
 *  A fixed ring of the newest Capacity marks plus a running count: the render thread writes, any thread
 *  reads, each slot under its own sequence number so a reader never takes half of a mark.
 *  AUDIO THREAD: record() is a handful of relaxed/release atomic stores; no allocation, no lock. */
class LMMS_EXPORT LatePeriodLog
{
public:
	static constexpr std::size_t Capacity = 64;

	struct Mark
	{
		std::uint64_t serial = 0;  //!< 1 for the engine's first late period, counting up
		tick_t tick = 0;           //!< the song position the period started at
		bool playing = false;      //!< the transport was rolling (otherwise tick is where it stood)
		std::uint32_t tookMicros = 0;
		std::uint32_t periodMicros = 0;
	};

	//! One late period. Render thread only (a single writer).
	void record(tick_t tick, bool playing, std::uint32_t tookMicros, std::uint32_t periodMicros) noexcept;

	//! Every late period since the engine started, including those the ring no longer holds. Any thread.
	std::uint64_t count() const noexcept { return m_count.load(std::memory_order_acquire); }

	//! The newest marks still held, oldest first. Off the audio thread (it allocates the vector).
	std::vector<Mark> recent() const;

private:
	struct Slot
	{
		std::atomic<std::uint64_t> sequence{0};  //!< odd while written; 2 * (serial) once complete
		std::atomic<tick_t> tick{0};
		std::atomic<bool> playing{false};
		std::atomic<std::uint32_t> tookMicros{0};
		std::atomic<std::uint32_t> periodMicros{0};
	};

	std::array<Slot, Capacity> m_slots;
	std::atomic<std::uint64_t> m_count{0};
};

} // namespace lmms

#endif // LMMS_RENDER_DIAGNOSTICS_H
