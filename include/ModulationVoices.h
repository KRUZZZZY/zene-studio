/*
 * ModulationVoices.h - 0.5.0 MX-120: the sounding notes a per-voice modulator
 *                      runs one cycle for.
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

#ifndef LMMS_MODULATION_VOICES_H
#define LMMS_MODULATION_VOICES_H

#include <array>
#include <atomic>

#include "lmms_export.h"

namespace lmms
{

//! One sounding note, as a per-voice modulator sees it.
struct ModulationVoice
{
	int track = -1;          //!< the instrument track's persistent id
	int channel = 0;         //!< the MIDI channel the note-on went to the instrument on
	int key = 0;             //!< the key the instrument received
	double startedAt = 0.0;  //!< song seconds of the note-on: the voice's cycle runs from here
};

/**
 * @brief A FIXED pool of voices: one slot per note that a per-voice modulator follows.
 *
 * A per-voice modulator (ModulatorSource::perVoice) restarts its LFO or envelope for EVERY note of the
 * track it follows and runs one cycle per note at the same time; the note's own value goes to the
 * instrument as a per-note modulation (Instrument::modulateNote). This pool is what it counts the notes
 * with. Written by InstrumentTrack::processOutEvent on the audio workers (note-on, note-off), read by the
 * block that applies the modulation layer; every field is an atomic and a slot is claimed by a
 * compare-exchange on its state, so several workers may start notes at once. Nothing allocates and
 * nothing grows: a note that finds no free slot is not voiced (counted in dropped()), the rule
 * RackZoneInputs' voice pool follows.
 */
class LMMS_EXPORT ModulationVoices
{
public:
	static constexpr int VoiceCount = 64;

	//! A note started (or restarted on the same channel and key) on track @a track at song time @a seconds.
	void noteOn(int track, int channel, int key, double seconds) noexcept;
	//! The note on (@a channel, @a key) of track @a track ended: its slot is free again.
	void noteOff(int track, int channel, int key) noexcept;
	//! Every slot free (the transport stopped, a project closed).
	void clear() noexcept;

	//! Slot @a slot's voice; false when the slot is free (or being written).
	auto voice(int slot, ModulationVoice* out) const noexcept -> bool;
	auto activeCount() const noexcept -> int;
	auto dropped() const noexcept -> unsigned { return m_dropped.load(std::memory_order_relaxed); }

private:
	enum State : int
	{
		Free = 0,
		Writing = 1,
		Live = 2,
	};

	struct Slot
	{
		std::atomic<int> state{Free};
		std::atomic<int> track{-1};
		std::atomic<int> channel{0};
		std::atomic<int> key{0};
		std::atomic<double> startedAt{0.0};
	};

	//! The live slot holding (track, channel, key), or -1.
	auto find(int track, int channel, int key) const noexcept -> int;

	std::array<Slot, VoiceCount> m_slots{};
	std::atomic<unsigned> m_dropped{0};
};

} // namespace lmms

#endif // LMMS_MODULATION_VOICES_H
