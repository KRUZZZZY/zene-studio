/*
 * ModulationVoices.cpp - 0.5.0 MX-120: the sounding notes a per-voice
 *                        modulator runs one cycle for.
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
 */

#include "ModulationVoices.h"

namespace lmms
{

auto ModulationVoices::find(int track, int channel, int key) const noexcept -> int
{
	for (int i = 0; i < VoiceCount; ++i)
	{
		const Slot& slot = m_slots[static_cast<std::size_t>(i)];
		if (slot.state.load(std::memory_order_acquire) != Live) { continue; }
		const bool same = slot.track.load(std::memory_order_relaxed) == track
			&& slot.channel.load(std::memory_order_relaxed) == channel
			&& slot.key.load(std::memory_order_relaxed) == key;
		if (same) { return i; }
	}
	return -1;
}

void ModulationVoices::noteOn(int track, int channel, int key, double seconds) noexcept
{
	// A retriggered note restarts its own voice rather than taking a second one.
	const int existing = find(track, channel, key);
	if (existing >= 0)
	{
		m_slots[static_cast<std::size_t>(existing)].startedAt.store(seconds, std::memory_order_relaxed);
		return;
	}
	for (Slot& slot : m_slots)
	{
		int expected = Free;
		if (!slot.state.compare_exchange_strong(expected, Writing, std::memory_order_acq_rel)) { continue; }
		slot.track.store(track, std::memory_order_relaxed);
		slot.channel.store(channel, std::memory_order_relaxed);
		slot.key.store(key, std::memory_order_relaxed);
		slot.startedAt.store(seconds, std::memory_order_relaxed);
		slot.state.store(Live, std::memory_order_release);
		return;
	}
	m_dropped.fetch_add(1, std::memory_order_relaxed);
}

void ModulationVoices::noteOff(int track, int channel, int key) noexcept
{
	const int slot = find(track, channel, key);
	if (slot < 0) { return; }
	int expected = Live;
	m_slots[static_cast<std::size_t>(slot)].state.compare_exchange_strong(expected, Free, std::memory_order_acq_rel);
}

void ModulationVoices::clear() noexcept
{
	for (Slot& slot : m_slots) { slot.state.store(Free, std::memory_order_release); }
}

auto ModulationVoices::voice(int slot, ModulationVoice* out) const noexcept -> bool
{
	if (slot < 0 || slot >= VoiceCount || out == nullptr) { return false; }
	const Slot& from = m_slots[static_cast<std::size_t>(slot)];
	if (from.state.load(std::memory_order_acquire) != Live) { return false; }
	out->track = from.track.load(std::memory_order_relaxed);
	out->channel = from.channel.load(std::memory_order_relaxed);
	out->key = from.key.load(std::memory_order_relaxed);
	out->startedAt = from.startedAt.load(std::memory_order_relaxed);
	// Freed (and possibly re-claimed) while it was read: report it next block instead.
	return from.state.load(std::memory_order_acquire) == Live;
}

auto ModulationVoices::activeCount() const noexcept -> int
{
	int count = 0;
	for (const Slot& slot : m_slots) { count += slot.state.load(std::memory_order_relaxed) == Live ? 1 : 0; }
	return count;
}

} // namespace lmms
