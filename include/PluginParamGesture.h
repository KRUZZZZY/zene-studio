/*
 * PluginParamGesture.h - the begin and end marks of a hosted plug-in's editor gesture (VST3, CLAP)
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

#ifndef LMMS_PLUGIN_PARAM_GESTURE_H
#define LMMS_PLUGIN_PARAM_GESTURE_H

#include <array>
#include <atomic>
#include <cstdint>

namespace lmms
{

/**
 * The begin and end marks a plug-in reported for one parameter (Q050-214), in order, each with the value the parameter
 * held then, so that the 100 ms GUI poll can tell apart any gestures that finished since the last one. A CLAP plug-in
 * reports them from process(), so a report touches only preallocated atomics: no allocation, lock, wait, journal or
 * model work. A VST3 editor should report from one thread, but any number of threads may report here.
 *
 * The handoff is a bounded queue (Dmitry Vyukov's bounded MPMC design, with one reader). When it is full a report is
 * dropped and counted, never waited for: the reader then learns that it lost some, and the parameter's value and the
 * next marks still carry the plug-in's latest state.
 */
class PluginParamGesture
{
public:
	//! 16 complete gestures on one parameter between two polls, far beyond a hand on a control.
	static constexpr std::uint32_t Capacity = 32;

	enum class Mark : std::uint8_t { Begin, End };

	struct Report
	{
		Mark mark = Mark::Begin;
		double value = 0.0;
	};

	//! What one read() found.
	struct Edits
	{
		std::array<Report, Capacity> reports;
		std::uint32_t count = 0;
		double value = 0.0;      //!< the parameter's value after the reports, when `settled`
		bool settled = false;    //!< false: more reports wait, newer than `value` may be: the next poll takes both
		bool lost = false;       //!< the queue was full since the last read, and `open` says where that left the gesture
		bool open = false;       //!< whether the plug-in's latest mark, kept or lost, was a begin
	};

	PluginParamGesture()
	{
		for (std::uint32_t i = 0; i < Capacity; ++i) { m_cells[i].sequence.store(i, std::memory_order_relaxed); }
	}

	//! Any thread, realtime safe. Call before storing the gesture's first value in @a value.
	template<typename T>
	void begin(const std::atomic<T>& value)
	{
		report(Mark::Begin, static_cast<double>(value.load(std::memory_order_relaxed)));
		std::atomic_thread_fence(std::memory_order_release);
	}

	//! Any thread, realtime safe. Call after storing the gesture's last value in @a value.
	template<typename T>
	void end(const std::atomic<T>& value)
	{
		report(Mark::End, static_cast<double>(value.load(std::memory_order_relaxed)));
	}

	/*! One thread only, the GUI's: takes the waiting reports, then the value. A value that a later begin's gesture
	 *  stored shows that begin's report as waiting (the fences pair with begin()'s), so a `settled` value belongs after
	 *  every report taken here and to no gesture still unseen. */
	template<typename T>
	void read(const std::atomic<T>& value, Edits* out)
	{
		out->count = 0;
		while (out->count < Capacity && take(&out->reports[out->count])) { ++out->count; }
		out->value = static_cast<double>(value.load(std::memory_order_relaxed));
		std::atomic_thread_fence(std::memory_order_acquire);
		out->settled = !waiting();
		const std::uint32_t lost = m_lost.load(std::memory_order_relaxed);
		out->lost = lost != m_lostSeen;
		m_lostSeen = lost;
		out->open = m_open.load(std::memory_order_relaxed);
	}

private:
	struct Cell
	{
		std::atomic<std::uint32_t> sequence{0};
		Report report;
	};

	void report(Mark mark, double value)
	{
		m_open.store(mark == Mark::Begin, std::memory_order_relaxed);
		std::uint32_t position = m_next.load(std::memory_order_relaxed);
		for (;;)
		{
			Cell& cell = m_cells[position % Capacity];
			const auto room = static_cast<std::int32_t>(cell.sequence.load(std::memory_order_acquire) - position);
			if (room < 0)
			{
				m_lost.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (room > 0) { position = m_next.load(std::memory_order_relaxed); }
			else if (m_next.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
			{
				cell.report = Report{mark, value};
				cell.sequence.store(position + 1, std::memory_order_release);
				return;
			}
		}
	}

	auto waiting() const -> bool
	{
		return m_cells[m_taken % Capacity].sequence.load(std::memory_order_acquire) == m_taken + 1;
	}

	auto take(Report* out) -> bool
	{
		if (!waiting()) { return false; }
		Cell& cell = m_cells[m_taken % Capacity];
		*out = cell.report;
		cell.sequence.store(m_taken + Capacity, std::memory_order_release);
		++m_taken;
		return true;
	}

	std::array<Cell, Capacity> m_cells;
	std::atomic<std::uint32_t> m_next{0};
	std::atomic<std::uint32_t> m_lost{0};
	std::atomic<bool> m_open{false};
	std::uint32_t m_taken = 0;     // the reader's
	std::uint32_t m_lostSeen = 0;  // the reader's
};

} // namespace lmms

#endif // LMMS_PLUGIN_PARAM_GESTURE_H
