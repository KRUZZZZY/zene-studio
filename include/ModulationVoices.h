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
#include <cstdint>

#include "lmms_export.h"

class InstrumentRoutingTest;

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
 *
 * clear() runs on the control thread, where an edit that disables per-voice modulation lands, and the
 * audio workers are in noteOn() (and noteOff()) at the same time. It therefore retires the pool by
 * generation instead of writing the slots: a claim is stamped with the generation it was made in, it
 * keeps its slot for as long as it is Writing (no second worker can claim a slot being filled, so two
 * notes never mix their fields), and a claim that finishes after the generation moved on publishes a
 * voice of the retired pool, which no reader sees and any later claim may take. Neither side waits.
 * The generation is 30 bits; a claim would have to span 2^30 clears to be mistaken for a live voice.
 */
class LMMS_EXPORT ModulationVoices
{
public:
	static constexpr int VoiceCount = 64;

	//! A note started (or restarted on the same channel and key) on track @a track at song time @a seconds.
	void noteOn(int track, int channel, int key, double seconds) noexcept;
	//! The note on (@a channel, @a key) of track @a track ended: its slot is free again.
	void noteOff(int track, int channel, int key) noexcept;
	//! Every voice of the pool is retired (the transport stopped, a project closed, a layer edit disabled
	//! per-voice modulation). A note-on that is filling a slot right now keeps writing it: its voice
	//! belongs to the pool this call retired, so it is not visible and its slot is claimable again.
	void clear() noexcept;

	//! Slot @a slot's voice; false when the slot is free (or being written).
	auto voice(int slot, ModulationVoice* out) const noexcept -> bool;
	auto activeCount() const noexcept -> int;
	auto dropped() const noexcept -> unsigned { return m_dropped.load(std::memory_order_relaxed); }

private:
	friend class ::InstrumentRoutingTest;
	enum State : std::uint32_t
	{
		Free = 0,
		Writing = 1,
		Live = 2,
	};

	/*! A slot a note-on has claimed and is still filling: what noteOn() passes from claim() to publish().
	 *  The generation is the pool it was claimed in - a claim whose pool has since been cleared publishes
	 *  a voice nobody can see, so the clearing side never has to wait for it. */
	struct Claim
	{
		int slot = -1;                    //!< -1: the pool was full, so the note was not voiced
		std::uint32_t generation = 0;     //!< the pool generation the slot was claimed in
	};

	struct Slot
	{
		//! The pool generation above the State: (generation << 2) | State.
		std::atomic<std::uint32_t> state{Free};
		std::atomic<int> track{-1};
		std::atomic<int> channel{0};
		std::atomic<int> key{0};
		std::atomic<double> startedAt{0.0};
	};

	static constexpr std::uint32_t stateMask = 0x3;
	static constexpr auto kindOf(std::uint32_t state) noexcept -> State
	{
		return static_cast<State>(state & stateMask);
	}
	static constexpr auto generationOf(std::uint32_t state) noexcept -> std::uint32_t { return state >> 2; }
	static constexpr auto stateWord(std::uint32_t generation, State kind) noexcept -> std::uint32_t
	{
		return (generation << 2) | static_cast<std::uint32_t>(kind);
	}

	//! The pool generation a slot must be Live in to be visible: clear() bumps it.
	auto currentGeneration() const noexcept -> std::uint32_t
	{
		return m_generation.load(std::memory_order_acquire);
	}

	//! The live slot of the current generation holding (track, channel, key), or -1.
	auto find(int track, int channel, int key) const noexcept -> int;
	//! Whether the slot word @a state - as a claimer observed it - may be taken by a claim that has read
	//! pool @a generation. The single decision claim() makes per attempt.
	static auto claimable(std::uint32_t state, std::uint32_t generation) noexcept -> bool;
	//! A claimable slot marked Writing, or {-1} when every slot is taken: the half of a note-on that runs
	//! before its fields are written. A slot another worker is writing is never claimable.
	auto claim() noexcept -> Claim;
	//! Fills a claimed slot and publishes it as a live voice; a {-1} claim counts a dropped note.
	void publish(const Claim& claim, int track, int channel, int key, double seconds) noexcept;

	std::array<Slot, VoiceCount> m_slots{};
	std::atomic<unsigned> m_dropped{0};
	//! The pool's generation: clear() bumps it, and every slot carries the generation it was claimed in.
	std::atomic<std::uint32_t> m_generation{0};
};

} // namespace lmms

#endif // LMMS_MODULATION_VOICES_H
