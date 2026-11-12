/*
 * VstChannelCount.h - the channel counts a VST plugin reports, as the remote
 *                     client may hand them to the host
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_VST_CHANNEL_COUNT_H
#define LMMS_VST_CHANNEL_COUNT_H

#include <optional>

#include "LmmsTypes.h"
#include "lmms_constants.h"

namespace lmms::vst
{

//! Converts a channel count the plugin reports (AEffect::numInputs / numOutputs,
//! a signed int) to the unsigned ch_cnt_t the host protocol carries. Returns
//! nullopt for a count the host cannot hold: a negative count would wrap to an
//! unrelated one (-1 -> 65535), and the host refuses anything above
//! MaxChannelsPerAudioBuffer (RemotePlugin.cpp, IdChangeInputOutputCount).
inline auto toChannelCount(int reported) -> std::optional<ch_cnt_t>
{
	if (reported < 0 || reported > static_cast<int>(MaxChannelsPerAudioBuffer))
	{
		return std::nullopt;
	}
	return static_cast<ch_cnt_t>(reported);
}

} // namespace lmms::vst

#endif // LMMS_VST_CHANNEL_COUNT_H
