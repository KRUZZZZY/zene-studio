/*
 * RackZoneFeed.h - routes an instrument track's notes through the key/velocity zones of the rack on the mixer
 *                  channel it plays into
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

#ifndef LMMS_RACK_ZONE_FEED_H
#define LMMS_RACK_ZONE_FEED_H

#include <cstdint>
#include <span>

#include "PlayHandle.h"
#include "RackZoneInputs.h"
#include "lmms_export.h"

namespace lmms
{

class EffectChain;
class Rack;
struct RackZone;

/**
 * @brief 0.5.0 E20/E38: the note path a rack's key/velocity zones consult.
 *
 * Made on the stack by AudioBusHandle::doProcessing (an audio worker) for the mixer channel the handle plays
 * into. Each NotePlayHandle has its own rendered buffer, so a note is the unit a zone routes: a note whose key
 * and velocity a zone claims (RackZones::resolve, first match wins) leaves the handle's common sum and plays
 * into the zone's CHAIN only - summed with the other notes of that chain, given the handle's own volume and
 * pan, and delivered to the rack's input block for the chain (RackZoneInputs). A zone with a loaded sample
 * plays the sample for the note instead of the instrument's sound for it.
 *
 * Notes no zone claims, and every note while the feed is not active, take the handle's common path exactly
 * as before zones existed: into the channel, then through the rack's selector (all chains, or the selected
 * one). The feed is NOT active - the whole handle takes the common path - when:
 *  - the channel is a bus or muted, or its rack has no zones or is not on the signal path;
 *  - the handle has effects of its own (they would have to run once per zone block);
 *  - the channel's latency compensation delays the handle (a zone block is not delayed with it).
 * A zone whose chain the graph does not render (a selected rack's other chains, a chain past 63) claims
 * nothing, so its notes are heard through the selected chain rather than lost.
 *
 * What a zone cannot split: an instrument that renders every note into one stream (IsSingleStreamed - most
 * hosted plug-ins) has no per-note buffers, so its notes stay on the common path; its notes still play a
 * zone's SAMPLE, which needs no buffer.
 *
 * Realtime: reads of the zone list, the rack and the handles, adds into pre-allocated blocks; no allocation.
 */
class LMMS_EXPORT RackZoneFeed
{
public:
	//! The feed for a handle that plays into mixer channel @a channel and owns @a effects (nullptr: none).
	RackZoneFeed(mix_ch_t channel, const EffectChain* effects);
	//! The feed into @a rack itself, without the channel's conditions (tests, and a caller that checked them).
	explicit RackZoneFeed(Rack* rack);

	auto active() const -> bool { return m_rack != nullptr; }

	//! The chain @a note plays on, with its zone in @a zone; -1 when no zone claims it or its chain is not wired.
	auto chainFor(const RackZoneNote& note, const RackZone** zone = nullptr) const -> int;

	//! @a handle as the zones see it: its key, velocity, pitch and state after this period's render.
	static auto noteOf(PlayHandle* handle, f_cnt_t framesPerPeriod) -> RackZoneNote;

	/*! Routes the claimed notes of @a handles one chain at a time: renders them into @a scratch, calls
	 *  @a gain() (the handle's volume and pan, applied to @a scratch in place), delivers @a scratch to the
	 *  chain. The claimed notes' buffers are released, so the handle's own pass skips them. True when any
	 *  note was claimed: @a scratch then holds the last chain's block, for the caller to silence. */
	template <typename Gain>
	bool run(const PlayHandleList& handles, std::span<SampleFrame> scratch, Gain&& gain)
	{
		if (!active()) { return false; }
		const std::uint64_t chains = chainsUsed(handles, scratch.size());
		for (int chain = 0; chain < 64 && chains >> chain != 0; ++chain)
		{
			if ((chains >> chain & 1) == 0) { continue; }
			if (!renderChain(chain, handles, scratch)) { continue; }
			gain();
			deliver(chain, scratch);
		}
		releaseClaimed(handles, scratch.size());
		return chains != 0;
	}

private:
	//! One bit per chain some note of @a handles plays on this period.
	auto chainsUsed(const PlayHandleList& handles, std::size_t frames) const -> std::uint64_t;
	//! Silences @a scratch and renders @a chain's notes into it; false when nothing was rendered.
	auto renderChain(int chain, const PlayHandleList& handles, std::span<SampleFrame> scratch) -> bool;
	//! Adds one claimed note: its zone's sample, or its own buffer. True when something was added.
	auto addNote(PlayHandle* handle, const RackZoneNote& note, const RackZone& zone, std::span<SampleFrame> scratch)
		-> bool;
	void deliver(int chain, std::span<const SampleFrame> block);
	void releaseClaimed(const PlayHandleList& handles, std::size_t frames);

	Rack* m_rack = nullptr;
};

} // namespace lmms

#endif // LMMS_RACK_ZONE_FEED_H
