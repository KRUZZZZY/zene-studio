/*
 * CarlaMidiHandoff.h - fixed-capacity MIDI hand-off at the Carla process boundary
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CARLA_MIDI_HANDOFF_H
#define LMMS_CARLA_MIDI_HANDOFF_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CarlaNative.h>

#include "MidiEvent.h"
#include "MidiEventToByteSeq.h"
#include "MidiHandoffQueue.h"

namespace lmms
{

//! Queues MIDI from any caller and stages it for one Carla process call on the render thread.
/*! The process callback is a compile-time seam: the instrument passes Carla's native process function, while the
 *  test fixture passes a stand-in with the same NativeMidiEvent boundary. The queue and native event storage are
 *  fixed-capacity members; enqueue and processPeriod allocate and lock nothing.
 */
template<std::size_t Capacity>
class CarlaMidiHandoff
{
public:
	auto enqueue(const MidiEvent& event, std::uint32_t offset) noexcept -> bool
	{
		return pushOrRecord(m_queue, m_overflow, makeQueuedEvent(event, offset));
	}

#ifdef LMMS_TESTING
	//! Deterministic contention seam for the producer and refusal path tests.
	template<typename BeforeQueueCompareExchange, typename BeforeStampCompareExchange>
	auto enqueueWithTestHooks(const MidiEvent& event, std::uint32_t offset, std::uint32_t* pushAttempts,
		std::uint32_t* stampAttempts, BeforeQueueCompareExchange&& beforeQueueCompareExchange,
		BeforeStampCompareExchange&& beforeStampCompareExchange) noexcept -> bool
	{
		return pushOrRecordWithTestHooks(m_queue, m_overflow, makeQueuedEvent(event, offset), pushAttempts,
			stampAttempts, static_cast<BeforeQueueCompareExchange&&>(beforeQueueCompareExchange),
			static_cast<BeforeStampCompareExchange&&>(beforeStampCompareExchange));
	}
	#endif

	//! Events refused by the fixed queue, including those refused after the bounded contention retry budget.
	auto droppedEvents() const noexcept -> std::uint64_t { return m_queue.dropped(); }

	//! Refused events recorded for channel all-notes-off recovery.
	auto recordedOverflows() const noexcept -> std::uint64_t { return m_overflow.recorded(); }

#ifdef LMMS_TESTING
	//! Refusals whose channel cut could not be advanced within its bounded contention budget.
	auto failedOverflowStampUpdates() const noexcept -> std::uint64_t { return m_overflow.failedStampUpdates(); }
#endif

	template<typename Process>
	void processPeriod(Process&& process)
	{
		m_eventCount = 0;
		drainMidi(m_queue, m_overflow, Capacity,
			[this](const HandoffMidiEvent& queued) -> bool
			{
				if (stageMidiEvent(queued, m_events[m_eventCount])) { ++m_eventCount; }
				return true;
			},
			[this](int channel) -> bool
			{
				if (m_eventCount >= Capacity) { return false; }
				HandoffMidiEvent allNotesOff;
				allNotesOff.type = static_cast<std::uint8_t>(MidiControlChange);
				allNotesOff.channel = static_cast<std::int8_t>(channel);
				allNotesOff.param1 = 123;
				allNotesOff.param2 = 0;
				if (stageMidiEvent(allNotesOff, m_events[m_eventCount])) { ++m_eventCount; }
				return true;
			}, Capacity);

		process(m_events.data(), static_cast<std::uint32_t>(m_eventCount));
		m_eventCount = 0;
	}

private:
	static auto makeQueuedEvent(const MidiEvent& event, std::uint32_t offset) noexcept -> HandoffMidiEvent
	{
		HandoffMidiEvent queued;
		queued.type = static_cast<std::uint8_t>(event.type());
		queued.channel = event.channel();
		queued.param1 = event.param(0);
		queued.param2 = event.param(1);
		queued.offset = offset;
		return queued;
	}

	static auto stageMidiEvent(const HandoffMidiEvent& queued, NativeMidiEvent& native) -> bool
	{
		const MidiEvent event(static_cast<MidiEventTypes>(queued.type), queued.channel, queued.param1, queued.param2);
		std::memset(&native, 0, sizeof(NativeMidiEvent));
		native.port = 0;
		native.time = queued.offset;
		const std::size_t written = writeToByteSeq(event, native.data, sizeof(NativeMidiEvent::data));
		if (written) { native.size = static_cast<std::uint8_t>(written); }
		return written != 0;
	}

	MidiHandoffQueue<Capacity> m_queue;
	MidiChannelOverflow m_overflow;
	std::array<NativeMidiEvent, Capacity> m_events{};
	std::size_t m_eventCount = 0;
};

} // namespace lmms

#endif // LMMS_CARLA_MIDI_HANDOFF_H
