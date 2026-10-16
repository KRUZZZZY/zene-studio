/*
 * MidiJackOutSchedule.h - JACK MIDI output: messages written at their frame, in time order
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

#ifndef LMMS_MIDI_JACK_OUT_SCHEDULE_H
#define LMMS_MIDI_JACK_OUT_SCHEDULE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

/*! 0.5.0 W4: the part of MidiJack's output path that does not need a JACK server, so a test can
 *  hold it to its contract. MidiJack queues each message with its source cycle and frame offset
 *  (see MidiJack::processOutEventAt); the JACK callback drains into a cycle-then-frame ordered
 *  list. If an event is deferred past its cycle, it is written at frame 0 of the current cycle.
 *  jack_midi_event_write() refuses an event earlier than the one before it, which is why this
 *  ordering is needed when senders on several threads queue out of time order. Everything here is
 *  realtime safe: fixed storage, no allocation, no lock. */

namespace lmms::midijack
{

//! One short MIDI message on its way to the JACK output port.
struct OutMessage
{
	std::uint8_t bytes[3];
	std::uint8_t length;
	std::uint32_t frame;  //!< frames from the start of the engine period it was produced in
	std::uint64_t cycle = 0;  //!< source JACK callback cycle, used to keep deferred messages ordered
};

//! The messages one JACK cycle writes at most; the rest wait in the queue for the next cycle.
inline constexpr std::size_t MaxOutPerCycle = 256;

//! Pops up to @a capacity messages from @a queue into @a list, ordered by frame. Stable: two
//! messages for the same frame keep the order they were queued in (a note-off then a note-on of
//! the same key stays a retrigger). @returns how many were taken.
template <typename Queue>
auto drainByFrame(Queue& queue, OutMessage* list, std::size_t capacity) -> std::size_t
{
	std::size_t count = 0;
	OutMessage message{};
	while (count < capacity && queue.pop(&message))
	{
		std::size_t position = count;
		while (position > 0 &&
			(list[position - 1].cycle > message.cycle ||
				(list[position - 1].cycle == message.cycle && list[position - 1].frame > message.frame)))
		{
			list[position] = list[position - 1];
			--position;
		}
		list[position] = message;
		++count;
	}
	return count;
}

//! The frame of a cycle of @a nframes a message for @a frame is written at: its own frame, or the
//! cycle's last one when it lies past the cycle (the same bound the plug-in hosts apply).
inline auto cycleFrame(std::uint32_t frame, std::uint32_t nframes) -> std::uint32_t
{
	return nframes == 0 ? 0 : std::min(frame, nframes - 1);
}

//! An event from an earlier cycle is overdue; write it before this cycle's frame-timed events.
inline auto cycleFrame(const OutMessage& message, std::uint64_t cycle, std::uint32_t nframes) -> std::uint32_t
{
	return message.cycle < cycle ? 0 : cycleFrame(message.frame, nframes);
}

} // namespace lmms::midijack

#endif // LMMS_MIDI_JACK_OUT_SCHEDULE_H
