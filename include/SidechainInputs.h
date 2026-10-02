/*
 * SidechainInputs.h - a hosted plug-in.s auxiliary input bus, fed from the mixer.s sidechain
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

#ifndef LMMS_SIDECHAIN_INPUTS_H
#define LMMS_SIDECHAIN_INPUTS_H

#include <algorithm>
#include <array>

#include "AudioBuffer.h"

namespace lmms
{

//! The most input channels a hosted plug-in is handed (the AudioPorts transport's own bound).
inline constexpr int MaxHostedInputChannels = 64;

/*! Engine audit E9: the input channel pointers a hosted effect hands its plug-in, with the
 *  plug-in's auxiliary (sidechain) input bus - which starts at flattened channel @a first -
 *  pointed at the mixer's sidechain signal for this block (Effect::sidechainBuffer(), what
 *  mixer.sidechain_to delivers). Before, the aux bus read whatever the track's extra channels
 *  held, so a VST3/CLAP compressor's key input could not be driven. Audio thread: copies
 *  pointers into @a out, allocates nothing; returns the channel count to pass on. With no
 *  sidechain this block, or no aux bus, @a in is passed through unchanged. */
inline auto sidechainInputs(const float* const* in, int channels, const AudioBuffer* sidechain, int first,
	std::array<const float*, MaxHostedInputChannels>& out) -> const float* const*
{
	if (sidechain == nullptr || sidechain->groupCount() == 0 || first < 0 || first >= channels
		|| channels > MaxHostedInputChannels) { return in; }
	std::copy(in, in + channels, out.begin());
	const AudioBuffer::ChannelGroup& key = sidechain->group(0);
	for (int c = 0; first + c < channels && c < static_cast<int>(key.channels()); ++c)
	{
		out[static_cast<std::size_t>(first + c)] = key.buffer(static_cast<ch_cnt_t>(c));
	}
	return out.data();
}

} // namespace lmms

#endif // LMMS_SIDECHAIN_INPUTS_H
