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
	//! Audio thread: queue @a midi as a parameter change when it is a mapped controller.
	//! True when it was (and is then not to be sent as an event).
	auto apply(const MidiEventIn& midi, Steinberg::Vst::IParameterChanges* changes) const -> bool;
	auto mappedCount() const -> int { return m_mapped; }

private:
	static constexpr std::uint32_t Unmapped = 0xffffffffu;
	std::array<std::uint32_t, 16 * Controllers> m_params{};
	int m_mapped = 0;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_MIDI_MAPPING_H
