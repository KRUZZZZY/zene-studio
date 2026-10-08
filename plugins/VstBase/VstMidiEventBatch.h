/*
 * VstMidiEventBatch.h - allocation-free helpers for the MIDI events the VST
 *                       remote client hands to its plugin once per period
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_VST_MIDI_EVENT_BATCH_H
#define LMMS_VST_MIDI_EVENT_BATCH_H

#include <cstddef>
#include <vector>

namespace lmms::vst
{

//! Sorts \a events by deltaFrames, in place and stably, without allocating.
/*!
 * Some plugins (e.g. Sinnah) hang on out-of-order events, so the client sorts
 * every period. std::stable_sort allocates a temporary buffer on each call, and
 * this sort runs while the host's audio callback waits for the client
 * (RemotePlugin::process()). An insertion sort is O(n^2), which is acceptable
 * for the bounded batch of at most one period's MIDI events, and it moves an
 * element only when it is strictly earlier than its predecessor, so equal
 * deltas keep their arrival order.
 */
template<class Event>
void sortByDeltaFrames(Event* events, std::size_t count)
{
	for (std::size_t i = 1; i < count; ++i)
	{
		const Event key = events[i];
		std::size_t j = i;
		while (j > 0 && key.deltaFrames < events[j - 1].deltaFrames)
		{
			events[j] = events[j - 1];
			--j;
		}
		events[j] = key;
	}
}

//! Appends \a event to \a queue unless the period already holds \a capacity events.
/*!
 * The queue is reserved to \a capacity in the client's constructor, so the append
 * does not allocate. A full queue drops the newest event and counts it in
 * \a dropped; the caller sends at most \a capacity events per period.
 *
 * @return true if the event was queued
 */
template<class Event>
bool appendBounded(std::vector<Event>& queue, const Event& event, std::size_t capacity, std::size_t& dropped)
{
	if (queue.size() >= capacity)
	{
		++dropped;
		return false;
	}
	queue.push_back(event);
	return true;
}

} // namespace lmms::vst

#endif // LMMS_VST_MIDI_EVENT_BATCH_H
