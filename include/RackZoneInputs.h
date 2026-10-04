/*
 * RackZoneInputs.h - the notes a rack's key/velocity zones route: one input block per chain, and the voices
 *                    a zone's sample plays through
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

#ifndef LMMS_RACK_ZONE_INPUTS_H
#define LMMS_RACK_ZONE_INPUTS_H

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;

/*! One note as a rack zone sees it (RackZoneFeed reads it off a NotePlayHandle after the note rendered this
 *  period). Plain data, so the routing rules can be proved without an instrument. */
struct RackZoneNote
{
	const void* id = nullptr;   //!< the play handle: a voice follows its note by this
	int key = 0;                //!< the engine key the note plays
	int velocity = 0;           //!< the note's volume, 0..200 (the zones' velocity space)
	f_cnt_t framesPlayed = 0;   //!< frames the note has played, this period's included
	f_cnt_t startOffset = 0;    //!< frames of this period before the note starts (its first period only)
	double pitch = 1.0;         //!< the note's frequency over DefaultBaseFreq (the track's base note sounds 1.0)
	float gain = 1.0f;          //!< the velocity as a level (volume / DefaultVolume)
	bool released = false;      //!< the note is off and in its release
	bool sounding = true;       //!< false for an arpeggio's/chord's master note and for a muted note
};

/*! One voice of a zone's sample: plays the decoded file from its start, linearly interpolated at the note's
 *  pitch, until the file ends or the note is released (then a FadeFrames fade). No resampler object and no
 *  buffer of its own, so restarting it for the next note is three stores. */
struct RackZoneVoice
{
	static constexpr f_cnt_t FadeFrames = 256;

	std::atomic<const void*> owner{nullptr};  //!< the note playing it; nullptr when free
	std::atomic<unsigned> touched{0};         //!< the inputs' period it last played in
	double position = 0.0;                    //!< the source frame it reads next
	f_cnt_t lastFramesPlayed = 0;             //!< its note's frame count last period (a reused handle restarts)
	float fade = 1.0f;
	bool ended = false;

	void restart();
	//! Adds @a frames of @a sample into @a dst at @a step source frames per frame and @a gain; false once ended.
	bool render(const SampleBuffer& sample, SampleFrame* dst, f_cnt_t frames, double step, float gain, bool released);
};

/**
 * @brief The per-chain note inputs of a Rack (0.5.0 E20/E38), and the voices a zone's sample plays through.
 *
 * Built by the rack on the control thread, inside the model-change guard, whenever it re-wires: a block per
 * chain the graph renders (`prepare`), nothing for a chain it leaves out. In a period:
 *
 *  1. the audio workers that render instrument tracks (AudioBusHandle, through RackZoneFeed) DELIVER each
 *     chain's zoned notes into its block - several workers may feed one rack, so the add is under a spin
 *     flag held for the length of one block add, the shape of Mixer::mixToChannel's channel lock;
 *  2. the mixer worker that renders the rack's channel reads each chain's block (`liveBlock`) as an extra
 *     input of that chain's node (RackChainNode), after the channel's common input and the split;
 *  3. the same worker ENDS the period: delivered blocks are zeroed and voices no note played are freed.
 *
 * No step allocates. The voices are a fixed pool of VoiceCount: a note that finds none free is not voiced
 * (no stealing), and says so through busyVoices().
 */
class LMMS_EXPORT RackZoneInputs
{
public:
	static constexpr int VoiceCount = 32;

	RackZoneInputs();
	~RackZoneInputs();

	RackZoneInputs(const RackZoneInputs&) = delete;
	auto operator=(const RackZoneInputs&) -> RackZoneInputs& = delete;

	// --- control thread, inside the model-change guard ---

	//! A block of @a frames for each of @a routedChains (indexes into a rack of @a chainCount chains).
	void prepare(const std::vector<int>& routedChains, int chainCount, f_cnt_t frames, sample_rate_t outputRate);
	//! No blocks: nothing is routed (the rack is off the signal path).
	void clear();

	auto active() const -> bool { return m_frames > 0; }
	auto frames() const -> f_cnt_t { return m_frames; }

	// --- audio: the workers that render notes ---

	//! Whether @a chain has a block (it is in the graph this period).
	auto routes(int chain) const -> bool;
	//! Adds @a block (frames() frames) to @a chain's input.
	void deliver(int chain, const SampleFrame* block);
	//! Adds @a note's voice of @a sample to @a dst (frames() frames); false when no voice is free for it.
	auto playVoice(const RackZoneNote& note, const SampleBuffer& sample, SampleFrame* dst) -> bool;

	// --- audio: the worker that renders the rack ---

	//! @a chain's delivered block this period, or nullptr when nothing was delivered.
	auto liveBlock(int chain) const -> const SampleFrame*;
	//! Zeroes what was delivered and frees the voices no note played this period.
	void endPeriod();

	//! Voices held by a note (introspection).
	auto busyVoices() const -> int;

private:
	struct Block
	{
		std::vector<SampleFrame> frames;
		std::atomic<bool> live{false};
	};

	auto voiceFor(const RackZoneNote& note) -> RackZoneVoice*;

	//! Index = chain; nullptr for a chain the graph does not render.
	std::vector<std::unique_ptr<Block>> m_blocks;
	std::array<RackZoneVoice, VoiceCount> m_voices;
	std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
	std::atomic<unsigned> m_period{1};
	f_cnt_t m_frames = 0;
	sample_rate_t m_outputRate = 44100;
};

} // namespace lmms

#endif // LMMS_RACK_ZONE_INPUTS_H
