/*
 * Vst3MidiMapping.h - MIDI controllers to a VST3 plug-in.s parameters (IMidiMapping)
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

#ifndef LMMS_VST3_MIDI_MAPPING_H
#define LMMS_VST3_MIDI_MAPPING_H

#include <array>
#include <cstdint>

#include "Vst3MidiQueue.h"

namespace Steinberg::Vst
{
class IEditController;
class IParameterChanges;
class IParamValueQueue;
}

namespace lmms::vst3
{

/*! A VST3 plug-in takes no MIDI controllers as events: it maps them to its own parameters through
 *  IMidiMapping on its edit controller. That query is a GUI-thread call, so the whole table - 16
 *  channels x (128 CCs, channel pressure, pitch bend) - is read once, when the plug-in is
 *  prepared, and the audio thread only looks it up. Engine audit E10: this is how an MPE member
 *  channel's pressure (channel pressure), timbre (CC74) and pitch (pitch bend) reach a VST3
 *  instrument; before, channel pressure and pitch bend were dropped and a CC went out as a
 *  "legacy CC out" event no synth reads. */
class MidiControllerMap
{
public:
	//! The VST3 controller numbers past the 128 CCs (ivstmidicontrollers.h).
	static constexpr int AfterTouch = 128;
	static constexpr int PitchBend = 129;
	static constexpr int Controllers = 130;

	//! GUI thread: read the plug-in's mapping for event bus @a busIndex; empty when the
	//! controller does not implement IMidiMapping.
	void build(Steinberg::Vst::IEditController* controller, std::int32_t busIndex);
	//! Audio thread: queue @a midi as a parameter change when it is a mapped controller, as a point
	//! at @a sampleOffset (the event's own frame, which the caller bounds to the request). True when
	//! it was (and is then not to be sent as an event). A parameter whose queue is full keeps its
	//! later points deferred; flush() writes the newest of them, so call it after the last apply().
	auto apply(const MidiEventIn& midi, Steinberg::Vst::IParameterChanges* changes,
		std::int32_t sampleOffset = 0) -> bool;
	//! Audio thread: write each deferred parameter's newest point at its own frame, then forget them.
	//! @a changes may be nullptr, in which case nothing was deferred and only the bookkeeping runs.
	void flush(Steinberg::Vst::IParameterChanges* changes);
	auto mappedCount() const -> int { return m_mapped; }

private:
	static constexpr std::uint32_t Unmapped = 0xffffffffu;
	//! The newest point of a parameter whose queue was full, waiting for flush().
	struct Deferred
	{
		bool pending = false;
		std::int32_t sampleOffset = 0;
		double value = 0.0;
	};
	void defer(std::size_t key, std::int32_t sampleOffset, double value);
	std::array<std::uint32_t, 16 * Controllers> m_params{};
	std::array<Deferred, 16 * Controllers> m_deferred{};
	//! The keys with a pending point, so flush() visits only those.
	std::array<std::uint16_t, 16 * Controllers> m_deferredKeys{};
	int m_deferredCount = 0;
	int m_mapped = 0;
};

/*! The points one parameter's queue takes in one request. The SDK's ParameterValueQueue reserves
 *  this many (kQueueReservedPoints, public.sdk/source/vst/hosting/parameterchanges.cpp), so a queue
 *  that stays within it never reallocates on the audio thread. */
inline constexpr int kMaxPointsPerParameter = 5;

/*! Audio thread: add a point at @a sampleOffset to @a queue, unless the queue already holds
 *  kMaxPointsPerParameter - 1 points. The last slot stays free for the newest point, which
 *  MidiControllerMap::flush() adds at its own frame; a point refused here is deferred, never moved
 *  earlier. @returns false when the point was not added. */
bool addBoundedPoint(Steinberg::Vst::IParamValueQueue* queue, std::int32_t sampleOffset, double value);

/*! Audio thread: copy into @a to the points of @a from that fall in the chunk [offset, offset + chunk),
 *  rebased to the chunk's start - the parameter half of the per-chunk slicing HostedPlugin::process()
 *  does for a request longer than the prepared block (the event half is Impl::sliceEvents). A
 *  parameter with no point in the chunk is left out. @a to is cleared by the caller. Every point of a
 *  request lies in [0, frames), so each one reaches exactly one chunk. No allocation while @a to has
 *  a queue per parameter (prepare() sizes it) and @a from keeps kMaxPointsPerParameter. */
void sliceParameterChanges(Steinberg::Vst::IParameterChanges& from, Steinberg::Vst::IParameterChanges& to,
	std::int32_t offset, std::int32_t chunk);

} // namespace lmms::vst3

#endif // LMMS_VST3_MIDI_MAPPING_H
