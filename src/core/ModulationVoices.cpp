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
	const std::uint32_t generation = currentGeneration();
	for (int i = 0; i < VoiceCount; ++i)
	{
		const Slot& slot = m_slots[static_cast<std::size_t>(i)];
		const std::uint32_t state = slot.state.load(std::memory_order_acquire);
		if (kindOf(state) != Live || generationOf(state) != generation) { continue; }
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
	publish(claim(), track, channel, key, seconds);
}

auto ModulationVoices::claimable(std::uint32_t state, std::uint32_t generation) noexcept -> bool
{
	// A slot another worker is filling is not ours to take. A voice of a pool that has since been cleared
	// is: its generation is behind the one this claim is stamped with.
	const State kind = kindOf(state);
	return kind != Writing && (kind != Live || generationOf(state) != generation);
}

auto ModulationVoices::claim() noexcept -> Claim
{
	const std::uint32_t generation = currentGeneration();
	for (int i = 0; i < VoiceCount; ++i)
	{
		Slot& slot = m_slots[static_cast<std::size_t>(i)];
		std::uint32_t state = slot.state.load(std::memory_order_acquire);
		for (;;)
		{
			if (!claimable(state, generation)) { break; }
			if (slot.state.compare_exchange_weak(state, stateWord(generation, Writing),
				std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return Claim{i, generation};
			}
		}
	}
	return Claim{};
}

void ModulationVoices::publish(const Claim& claim, int track, int channel, int key, double seconds) noexcept
{
	if (claim.slot < 0)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	Slot& slot = m_slots[static_cast<std::size_t>(claim.slot)];
	slot.track.store(track, std::memory_order_relaxed);
	slot.channel.store(channel, std::memory_order_relaxed);
	slot.key.store(key, std::memory_order_relaxed);
	slot.startedAt.store(seconds, std::memory_order_relaxed);
	// The claim's own generation, not the current one: a clear() that ran while this slot was being filled
	// retires the voice here rather than being waited for.
	slot.state.store(stateWord(claim.generation, Live), std::memory_order_release);
}

void ModulationVoices::noteOff(int track, int channel, int key) noexcept
{
	const int slot = find(track, channel, key);
	if (slot < 0) { return; }
	Slot& ended = m_slots[static_cast<std::size_t>(slot)];
	std::uint32_t state = ended.state.load(std::memory_order_acquire);
	while (kindOf(state) == Live)
	{
		// Frees only the voice that was found: a slot claimed by another note in between keeps its claim.
		if (ended.state.compare_exchange_weak(state, stateWord(generationOf(state), Free),
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return;
		}
	}
}

void ModulationVoices::clear() noexcept
{
	// The slots are not written: a note-on that is filling one keeps it (so no second worker can claim a
	// slot being filled), and the voice it publishes carries the generation this bump retires. One
	// writer (the control thread's edit), so a plain bump is enough.
	m_generation.fetch_add(1, std::memory_order_acq_rel);
}

auto ModulationVoices::voice(int slot, ModulationVoice* out) const noexcept -> bool
{
	if (slot < 0 || slot >= VoiceCount || out == nullptr) { return false; }
	const Slot& from = m_slots[static_cast<std::size_t>(slot)];
	const std::uint32_t state = from.state.load(std::memory_order_acquire);
	if (kindOf(state) != Live || generationOf(state) != currentGeneration()) { return false; }
	out->track = from.track.load(std::memory_order_relaxed);
	out->channel = from.channel.load(std::memory_order_relaxed);
	out->key = from.key.load(std::memory_order_relaxed);
	out->startedAt = from.startedAt.load(std::memory_order_relaxed);
	// Freed (and possibly re-claimed) while it was read: report it next block instead.
	return from.state.load(std::memory_order_acquire) == state;
}

auto ModulationVoices::activeCount() const noexcept -> int
{
	const std::uint32_t generation = currentGeneration();
	int count = 0;
	for (const Slot& slot : m_slots)
	{
		const std::uint32_t state = slot.state.load(std::memory_order_relaxed);
		count += kindOf(state) == Live && generationOf(state) == generation ? 1 : 0;
	}
	return count;
}

} // namespace lmms
