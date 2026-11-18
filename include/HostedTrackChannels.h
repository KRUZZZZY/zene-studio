/*
 * HostedTrackChannels.h - the track channel count a hosted plug-in's audio ports need
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

#ifndef LMMS_HOSTED_TRACK_CHANNELS_H
#define LMMS_HOSTED_TRACK_CHANNELS_H

#include <algorithm>

#include "LmmsTypes.h"

namespace lmms
{

/*! Cluster stress-3: the track channels a VST3/CLAP host gives AudioPortsModel::setAllChannelCounts() for a
 *  plug-in with @a inputs and @a outputs channels - the larger of the two, never fewer than the stereo pair,
 *  rounded up to whole L/R pairs. AudioPortsModel refuses an odd track channel count (it throws), so a
 *  plug-in with three, five, nine or seventeen channels on one side (a mono x4 mixer, a stereo compressor
 *  with a mono side-chain) was refused as "the engine could not instantiate" before it was ever hosted. */
constexpr auto hostedTrackChannels(ch_cnt_t inputs, ch_cnt_t outputs) -> ch_cnt_t
{
	const auto wanted = std::max<int>({2, inputs, outputs});
	return static_cast<ch_cnt_t>(wanted + wanted % 2);
}

} // namespace lmms

#endif // LMMS_HOSTED_TRACK_CHANNELS_H
