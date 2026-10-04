/*
 * InstrumentMidiParts.h - 0.5.0 MX-114: one instrument track addressing a
 *                         multitimbral plug-in on several MIDI channels.
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

#ifndef LMMS_INSTRUMENT_MIDI_PARTS_H
#define LMMS_INSTRUMENT_MIDI_PARTS_H

#include <array>
#include <atomic>
#include <cstdint>

#include "Note.h"
#include "RackZones.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/**
 * @brief 0.5.0 MX-114: a track's MIDI parts - key and velocity zones, each sending to one MIDI channel.
 *
 * A multitimbral plug-in (a General MIDI synth, a sampler with sixteen parts) listens on several MIDI
 * channels and plays a different sound on each. A part is a RackZone whose `chain` is the MIDI channel
 * (0..15): a note whose key and velocity a part claims is sent on that part's channel, FIRST MATCH WINS
 * (RackZones::resolve, the rule the rack zones state). A note no part claims keeps the channel the track
 * would have used without parts (the MIDI port's output channel, or the note's own). The zone list and
 * its persistence are RackZones' own - this class adds only the channel memory a note-off needs and the
 * set of channels a channel-wide message (pitch bend, a controller) is sent to.
 *
 * With no parts the track sends exactly what it always sent. The velocity range is the engine's note
 * volume (0..200, as in the rack zones); a MIDI velocity is converted (64 = volume 100).
 *
 * Realtime: noteOnChannel/noteOffChannel/channelMask run on the audio thread (InstrumentTrack::
 * processOutEvent); they read the zone list (edited under the model-change guard) and store per-key
 * atomics - no allocation, no lock.
 */
class LMMS_EXPORT InstrumentMidiParts
{
public:
	static constexpr int Channels = 16;

	InstrumentMidiParts();

	auto zones() -> RackZones& { return m_zones; }
	auto zones() const -> const RackZones& { return m_zones; }
	auto active() const -> bool { return m_zones.zoneCount() > 0; }

	//! The channel a note-on of @a key at MIDI velocity @a velocity is sent on; remembered for its note-off.
	auto noteOnChannel(int key, int velocity, int fallback) -> int;
	//! The channel the note-on of @a key went to (and forgets it), or @a fallback.
	auto noteOffChannel(int key, int fallback) -> int;
	//! The channel the sounding note on @a key went to (key pressure), or @a fallback.
	auto channelOfKey(int key, int fallback) const -> int;
	//! One bit per MIDI channel some part sends on.
	auto channelMask() const -> std::uint16_t;

	//! Writes <midiparts> with one <zone> per part when there are parts.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Reads the <midiparts> child of @a parent; none leaves no parts.
	void loadSettings(const QDomElement& parent);

private:
	RackZones m_zones;
	std::array<std::atomic<std::int8_t>, NumKeys> m_keyChannel;
};

} // namespace lmms

#endif // LMMS_INSTRUMENT_MIDI_PARTS_H
