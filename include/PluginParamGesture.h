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

#include <atomic>
#include <cstdint>

namespace lmms
{

/**
 * The begin and end marks a plug-in reported for one parameter (Q050-214). A CLAP plug-in reports them
 * from process(), so writing them is two atomic increments and nothing else: no journal work and no
 * model write happens where they arrive. The GUI poll reads them with the value (read()).
 */
class PluginParamGesture
{
public:
	struct Marks
	{
		std::uint32_t begins = 0;
		std::uint32_t ends = 0;
	};

	//! Any thread, realtime safe. Call before storing the gesture's first value.
	void begin()
	{
		m_begins.fetch_add(1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
	}

	//! Any thread, realtime safe. Call after storing the gesture's last value.
	void end() { m_ends.fetch_add(1, std::memory_order_release); }

	/*! The parameter's value and the marks, in the order that keeps a gesture in one step when the plug-in reports
	 *  during the read: an end seen means its last value is seen, and a value seen means its begin is seen. */
	template<typename T>
	auto read(const std::atomic<T>& value, T* out) const -> Marks
	{
		Marks marks;
		marks.ends = m_ends.load(std::memory_order_acquire);
		*out = value.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		marks.begins = m_begins.load(std::memory_order_relaxed);
		return marks;
	}

private:
	std::atomic<std::uint32_t> m_begins{0};
	std::atomic<std::uint32_t> m_ends{0};
};

} // namespace lmms

#endif // LMMS_PLUGIN_PARAM_GESTURE_H
