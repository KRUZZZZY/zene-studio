/*
 * RackZoneFeed.cpp - routes an instrument track's notes through the key/velocity zones of the rack on the
 *                    mixer channel it plays into
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

#include "RackZoneFeed.h"

#include <algorithm>

#include "EffectChain.h"
#include "Engine.h"
#include "MixHelpers.h"
#include "Mixer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Rack.h"
#include "RackZones.h"
#include "SampleBuffer.h"

namespace lmms
{

namespace
{

//! The most chains a zone can route to: one bit each in a period's chain set.
constexpr int MaxZoneChains = 64;

//! Whether the handle feeding @a channel may route through the channel's zones (the conditions in the header).
bool feedable(Mixer& mixer, mix_ch_t channel, const EffectChain* effects)
{
	if (channel >= mixer.numChannels()) { return false; }
	MixerChannel* target = mixer.mixerChannel(channel);
	if (target == nullptr || target->isBus() || target->m_muteModel.value()) { return false; }
	if (effects != nullptr && !effects->effects().empty()) { return false; }
	if (mixer.channelInputLatency(channel) != 0) { return false; }
	const Rack& rack = target->m_rack;
	return rack.zones().zoneCount() > 0 && rack.zoneInputs().active() && rack.canProcessThroughRack(target->m_bus);
}

} // namespace

RackZoneFeed::RackZoneFeed(mix_ch_t channel, const EffectChain* effects)
{
	Mixer* mixer = Engine::mixer();
	if (mixer != nullptr && feedable(*mixer, channel, effects)) { m_rack = &mixer->mixerChannel(channel)->m_rack; }
}

RackZoneFeed::RackZoneFeed(Rack* rack) :
	m_rack(rack != nullptr && rack->zoneInputs().active() ? rack : nullptr)
{
}

auto RackZoneFeed::chainFor(const RackZoneNote& note, const RackZone** zone) const -> int
{
	if (!active() || note.id == nullptr) { return -1; }
	const RackZone* claimed = m_rack->zones().zone(m_rack->zones().resolve(note.key, note.velocity));
	if (claimed == nullptr || claimed->chain >= MaxZoneChains || !m_rack->zoneInputs().routes(claimed->chain))
	{
		return -1;
	}
	if (zone != nullptr) { *zone = claimed; }
	return claimed->chain;
}

auto RackZoneFeed::noteOf(PlayHandle* handle, f_cnt_t framesPerPeriod) -> RackZoneNote
{
	RackZoneNote note;
	if (handle == nullptr || handle->type() != PlayHandle::Type::NotePlayHandle) { return note; }
	const auto* nph = static_cast<const NotePlayHandle*>(handle);
	note.id = handle;
	note.key = nph->key();
	note.velocity = nph->getVolume();
	note.framesPlayed = nph->totalFramesPlayed();
	// The first period of a note starts at its offset; NotePlayHandle::play has already counted this period.
	note.startOffset = note.framesPlayed + nph->offset() <= framesPerPeriod ? nph->offset() : 0;
	note.pitch = nph->frequency() / DefaultBaseFreq;
	note.gain = static_cast<float>(nph->getVolume()) / static_cast<float>(DefaultVolume);
	note.released = nph->isReleased();
	// A note that has not started yet (its offset fell into a later period) does not sound this period either.
	note.sounding = !nph->isMasterNote() && !nph->isMuted() && note.framesPlayed > 0;
	return note;
}

auto RackZoneFeed::chainsUsed(const PlayHandleList& handles, std::size_t frames) const -> std::uint64_t
{
	std::uint64_t chains = 0;
	for (PlayHandle* handle : handles)
	{
		const int chain = chainFor(noteOf(handle, static_cast<f_cnt_t>(frames)));
		if (chain >= 0) { chains |= std::uint64_t{1} << chain; }
	}
	return chains;
}

auto RackZoneFeed::addNote(PlayHandle* handle, const RackZoneNote& note, const RackZone& zone,
	std::span<SampleFrame> scratch) -> bool
{
	if (zone.loaded != nullptr)
	{
		// E38: the zone's sample is the note's sound on this chain (the instrument's own buffer is dropped).
		return note.sounding && m_rack->zoneInputs().playVoice(note, *zone.loaded, scratch.data());
	}
	const std::span<SampleFrame> own = handle->buffer();
	if (own.data() == nullptr || !handle->usesBuffer() || own.size() != scratch.size()) { return false; }
	MixHelpers::add(scratch.data(), own.data(), static_cast<int>(scratch.size()));
	return true;
}

auto RackZoneFeed::renderChain(int chain, const PlayHandleList& handles, std::span<SampleFrame> scratch) -> bool
{
	std::fill(scratch.begin(), scratch.end(), SampleFrame{});
	bool rendered = false;
	for (PlayHandle* handle : handles)
	{
		const RackZoneNote note = noteOf(handle, static_cast<f_cnt_t>(scratch.size()));
		const RackZone* zone = nullptr;
		if (chainFor(note, &zone) != chain) { continue; }
		rendered = addNote(handle, note, *zone, scratch) || rendered;
	}
	return rendered;
}

void RackZoneFeed::deliver(int chain, std::span<const SampleFrame> block)
{
	if (static_cast<f_cnt_t>(block.size()) != m_rack->zoneInputs().frames()) { return; }
	m_rack->zoneInputs().deliver(chain, block.data());
}

void RackZoneFeed::releaseClaimed(const PlayHandleList& handles, std::size_t frames)
{
	for (PlayHandle* handle : handles)
	{
		if (chainFor(noteOf(handle, static_cast<f_cnt_t>(frames))) >= 0) { handle->releaseBuffer(); }
	}
}

} // namespace lmms
