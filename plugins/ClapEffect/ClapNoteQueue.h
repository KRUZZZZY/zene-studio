/*
 * ClapNoteQueue.h - the note-event path between LMMS' MIDI route and a CLAP
 *                   plug-in (SPEC A11-A14, feature row 79 / board task #669)
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

#ifndef LMMS_CLAP_NOTE_QUEUE_H
#define LMMS_CLAP_NOTE_QUEUE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <QString>

namespace lmms::clap
{

/*!
 * One note input port as clap.note-ports reports it.
 *
 * Deliberately free of CLAP types, like ClapBusMap.h's PortDescriptor: the
 * mapping is data, and the host/control halves read it without a clap.h
 * dependency. `supportedDialects`/`preferredDialect` are the CLAP dialect
 * bitfield values (clap_note_dialect) kept as plain integers for the same
 * reason.
 */
struct NotePortDescriptor
{
	//! plug-in scoped port id (clap_note_port_info_t::id)
	std::uint32_t id = 0;
	QString name;
	//! bitfield of clap_note_dialect values the port accepts
	std::uint32_t supportedDialects = 0;
	//! the one dialect the port would rather be driven with
	std::uint32_t preferredDialect = 0;
	//! true for the port the host delivers its notes to
	bool preferred = false;
};

//! clap_note_dialect values, named here so the note-path code and the control
//! surface do not have to include clap.h to read a dialect out of a report.
enum NoteDialect : std::uint32_t
{
	NoteDialectClap = 1u << 0,     //!< clap_event_note (the host's own dialect)
	NoteDialectMidi = 1u << 1,     //!< clap_event_midi
	NoteDialectMidiMpe = 1u << 2,  //!< clap_event_midi with MPE
	NoteDialectMidi2 = 1u << 3,    //!< clap_event_midi2
};

//! One note event on its way from LMMS' MIDI route to a plug-in's input event
//! list. Plain data by design: the queue between the two is a fixed-size array
//! of these, so a block that carries notes never allocates.
struct NoteEventIn
{
	std::uint8_t type = 0;        //!< CLAP_EVENT_NOTE_ON / _OFF / _CHOKE
	std::uint8_t channel = 0;     //!< 0 .. 15 (LMMS' MIDI channel)
	std::int16_t key = 0;         //!< 0 .. 127
	std::int16_t velocity = 0;    //!< 0 .. 127; ignored for note-off
	std::int32_t frameOffset = 0; //!< frames from the start of the block
};

//! How many note events one block can carry. Fixed at compile time so the
//! audio thread sizes its CLAP event list once, in prepare(), and never grows
//! it while processing.
inline constexpr int kMaxNoteEventsPerBlock = 256;

//! Bound on the queue between the MIDI route and the audio thread. A power of
//! two: the ring masks its index. A full queue drops the event and counts it
//! (NoteCounters::dropped) instead of growing or blocking.
inline constexpr std::size_t kNoteQueueCapacity = 1024;

//! What the note path has actually done, in relaxed atomics: the audio path
//! increments, `plugin.host_notes` reads. Unsigned and process-lifetime, like
//! the chunking counters (include/PluginHostChunking.h).
struct NoteCounters
{
	std::uint32_t pushed = 0;    //!< events handed to the queue
	std::uint32_t dropped = 0;   //!< events a full queue refused
	std::uint32_t delivered = 0; //!< events put into a plug-in's input list
	std::uint32_t ports = 0;     //!< note INPUT ports the loaded plug-in declares
	std::uint32_t played = 0;    //!< note-on events delivered (voices started)
};

/*!
 * Bounded, lock free, thread safe queue of NoteEventIn.
 *
 * The producers are LMMS' MIDI route - the audio thread when a NotePlayHandle
 * built during the current period delivers an event, the MIDI/GUI thread for
 * live input - and the consumer is HostedPlugin::process() on the audio
 * thread. That is a multi-producer / single-consumer mix, so a
 * single-producer ring would be wrong and a lock is not allowed on the audio
 * path. This is the same bounded ring with a per-slot sequence number the VST3
 * lane uses (plugins/Vst3Effect/Vst3MidiQueue.h): every operation is one
 * compare-exchange, one plain store and one release store. It allocates
 * nothing, never grows, never blocks and never spins unboundedly - a full
 * queue refuses the event, and the caller counts it.
 */
class NoteQueue
{
public:
	NoteQueue()
	{
		// A cell's sequence number says which lap of the ring it belongs to:
		// cell i starts at i so that the very first push to any slot in the
		// first lap is recognised as "this slot is free for position i".
		for (std::size_t i = 0; i < m_cells.size(); ++i)
		{
			m_cells[i].sequence.store(static_cast<std::uint32_t>(i),
				std::memory_order_relaxed);
		}
	}

	//! @returns false when the queue is full (the event is dropped)
	auto push(const NoteEventIn& event) -> bool
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
	auto pop(NoteEventIn* event) -> bool
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
					cell.sequence.store(position + kNoteQueueCapacity,
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

	//! Main thread only, and only while the audio thread cannot be popping
	//! (prepare() calls it): forget everything queued.
	void reset()
	{
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
		static_cast<std::uint32_t>(kNoteQueueCapacity) - 1;

	struct Cell
	{
		std::atomic<std::uint32_t> sequence{0};
		NoteEventIn event{};
	};

	std::array<Cell, kNoteQueueCapacity> m_cells;
	std::atomic<std::uint32_t> m_enqueue{0};
	std::atomic<std::uint32_t> m_dequeue{0};
};

} // namespace lmms::clap

#endif // LMMS_CLAP_NOTE_QUEUE_H
