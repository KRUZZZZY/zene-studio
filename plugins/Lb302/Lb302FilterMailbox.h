/*
 * Lb302FilterMailbox.h - the LB302 filter settings, control thread to audio thread
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

#ifndef LMMS_LB302_FILTER_MAILBOX_H
#define LMMS_LB302_FILTER_MAILBOX_H

#include <array>
#include <atomic>
#include <cstddef>

namespace lmms
{

//! The filter settings the control thread publishes and the audio thread adopts.
struct Lb302FilterSettings
{
	float cutoff = 0.f;
	float reso = 0.f;
	float envmod = 0.f;
	float envdecay = 0.f;
	float dist = 0.f;
	bool db24 = false;
};

/*! The latest complete Lb302FilterSettings, from any thread that moves a knob
 *  (the control thread, or the audio thread itself when automation moves it)
 *  to the audio thread: a sequence lock over atomics, so there is no data race
 *  and a torn snapshot is never adopted.
 *  The reader never waits: while a snapshot is being written it keeps the one
 *  it has and picks the new one up next block. Writers never wait either: one
 *  that finds another mid-write marks the snapshot dirty and leaves; the writer
 *  holding the lock then builds again from the current knobs, so the last
 *  snapshot published is always the one the knobs ask for. */
class Lb302FilterMailbox
{
public:
	//! Any thread: publishes @a make(), which builds from the current knob values.
	template<typename Make>
	void publish(const Make& make)
	{
		m_dirty.store(true);
		while (m_dirty.load())
		{
			unsigned version = m_version.load();
			if ((version & 1u) != 0u || !m_version.compare_exchange_strong(version, version + 1)) { return; }
			std::atomic_thread_fence(std::memory_order_release);
			m_dirty.exchange(false);  // a read: it sees the knob a writer that left early moved
			store(make());
			m_version.store(version + 2);
		}
	}

	//! Audio thread: a newer complete snapshot into @a out, or false (nothing new, or one mid-write).
	bool take(Lb302FilterSettings& out) noexcept
	{
		const unsigned version = m_version.load(std::memory_order_acquire);
		if ((version & 1u) != 0u || version == m_taken) { return false; }
		std::array<float, Fields> values;
		for (std::size_t i = 0; i < Fields; ++i) { values[i] = m_fields[i].load(std::memory_order_relaxed); }
		std::atomic_thread_fence(std::memory_order_acquire);
		if (m_version.load(std::memory_order_relaxed) != version) { return false; }
		m_taken = version;
		out = unpack(values);
		return true;
	}

private:
	static constexpr std::size_t Fields = 6;

	static_assert(std::atomic<float>::is_always_lock_free,
		"the LB302 filter settings payload must be lock-free");

	void store(const Lb302FilterSettings& settings) noexcept
	{
		const std::array<float, Fields> values{settings.cutoff, settings.reso, settings.envmod,
			settings.envdecay, settings.dist, settings.db24 ? 1.f : 0.f};
		for (std::size_t i = 0; i < Fields; ++i) { m_fields[i].store(values[i], std::memory_order_relaxed); }
	}

	static Lb302FilterSettings unpack(const std::array<float, Fields>& v) noexcept
	{
		return {v[0], v[1], v[2], v[3], v[4], v[5] != 0.f};
	}

	std::array<std::atomic<float>, Fields> m_fields{};
	std::atomic<unsigned> m_version{0};  //!< odd while a snapshot is being written
	std::atomic<bool> m_dirty{false};    //!< a writer found the lock taken: build again
	unsigned m_taken = 0;                //!< the version the audio thread last adopted (audio thread only)
};

} // namespace lmms

#endif // LMMS_LB302_FILTER_MAILBOX_H
