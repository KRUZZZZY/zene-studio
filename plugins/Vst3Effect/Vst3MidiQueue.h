/*
 * Vst3MidiQueue.h - the MIDI queue between LMMS' MIDI path and a plug-in
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_VST3_MIDI_QUEUE_H
#define LMMS_VST3_MIDI_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lmms::vst3
{

//! One MIDI event on its way from LMMS' MIDI path to a plug-in's input event
//! bus. Plain data by design: the queue between the two is a fixed-size array
//! of these, so an audio block that carries MIDI never allocates.
struct MidiEventIn
{
	std::uint8_t type = 0;        //!< lmms::MidiEventTypes (0x80 .. 0xEF)
	std::uint8_t channel = 0;     //!< 0 .. 15
	std::uint8_t data0 = 0;       //!< note key / controller number
	std::uint8_t data1 = 0;       //!< note velocity / controller value
	std::int32_t frameOffset = 0; //!< frames from the start of the block
};

//! How many MIDI events one audio block can carry. Fixed at compile time so
//! the audio thread sizes its VST3 event list once, in prepare(), and never
//! grows it while processing.
inline constexpr int kMaxMidiEventsPerBlock = 256;

//! Bound on the queue between the MIDI path and the audio thread. A power of
//! two: the ring masks its index. A full queue drops the event and counts it
//! (see HostedPlugin::droppedMidiEvents()) instead of growing or blocking.
inline constexpr std::size_t kMidiQueueCapacity = 1024;

/**
 * Bounded, lock free thread safe queue of MidiEventIn.
 *
 * The MIDI path pushes from wherever LMMS delivers the event - the audio
 * thread (a NotePlayHandle built during the current period calls
 * InstrumentTrack::processOutEvent at its own sample offset) and the MIDI/GUI
 * thread for live input - while HostedPlugin::process() pops on the audio
 * thread. That is a multi-producer / single-consumer mix, so a
 * single-producer ring would be wrong and a lock is not allowed on the audio
 * path. This is the standard bounded ring with a per-slot sequence number:
 * every operation is one compare-exchange, one plain store and one release
 * store. It allocates nothing, never grows, never blocks and never spins
 * unboundedly - a full queue refuses the event, and the caller counts it.
 */
class MidiQueue
{
public:
	MidiQueue()
	{
		// A cell's sequence number says which lap of the ring it belongs to:
		// cell i starts at i so that the very first push to any slot in the
		// first lap is recognised as "this slot is free for position i".
		// Leaving them all at zero would make every slot after the first
		// report "full" until the ring wrapped once.
		for (std::size_t i = 0; i < m_cells.size(); ++i)
		{
			m_cells[i].sequence.store(static_cast<std::uint32_t>(i),
				std::memory_order_relaxed);
		}
	}

	//! @returns false when the queue is full (the event is dropped)
	auto push(const MidiEventIn& event) -> bool
	{
		auto position = m_enqueue.load(std::memory_order_relaxed);
		while (true)
		{
			auto& cell = m_cells[position & kMask];
			const auto sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::int32_t>(sequence) -
				static_cast<std::int32_t>(position);
			if (difference == 0)
			{
				if (m_enqueue.compare_exchange_weak(position, position + 1,
						std::memory_order_relaxed))
				{
					cell.event = event;
					cell.sequence.store(position + 1, std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0)
			{
				return false;
			}
			else
			{
				position = m_enqueue.load(std::memory_order_relaxed);
			}
		}
	}

	//! @returns false when the queue is empty
	auto pop(MidiEventIn* event) -> bool
	{
		auto position = m_dequeue.load(std::memory_order_relaxed);
		while (true)
		{
			auto& cell = m_cells[position & kMask];
			const auto sequence = cell.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<std::int32_t>(sequence) -
				static_cast<std::int32_t>(position + 1);
			if (difference == 0)
			{
				if (m_dequeue.compare_exchange_weak(position, position + 1,
						std::memory_order_relaxed))
				{
					*event = cell.event;
					cell.sequence.store(position + kMidiQueueCapacity,
						std::memory_order_release);
					return true;
				}
			}
			else if (difference < 0)
			{
				return false;
			}
			else
			{
				position = m_dequeue.load(std::memory_order_relaxed);
			}
		}
	}

	//! GUI thread only, and only while the audio thread cannot be popping
	//! (prepare() calls it): forget everything queued.
	void reset()
	{
		// Same initial state as the constructor: cell i's sequence is i.
		for (std::size_t i = 0; i < m_cells.size(); ++i)
		{
			m_cells[i].sequence.store(static_cast<std::uint32_t>(i),
				std::memory_order_relaxed);
		}
		m_enqueue.store(0, std::memory_order_relaxed);
		m_dequeue.store(0, std::memory_order_relaxed);
	}

private:
	static constexpr std::uint32_t kMask =
		static_cast<std::uint32_t>(kMidiQueueCapacity) - 1;

	struct Cell
	{
		std::atomic<std::uint32_t> sequence{0};
		MidiEventIn event{};
	};

	std::array<Cell, kMidiQueueCapacity> m_cells;
	std::atomic<std::uint32_t> m_enqueue{0};
	std::atomic<std::uint32_t> m_dequeue{0};
};

} // namespace lmms::vst3

#endif // LMMS_VST3_MIDI_QUEUE_H
