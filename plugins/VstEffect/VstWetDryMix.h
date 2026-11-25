/*
 * VstWetDryMix.h - wet/dry mixing of a VST effect's planar output
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

#ifndef LMMS_VST_WET_DRY_MIX_H
#define LMMS_VST_WET_DRY_MIX_H

#include <algorithm>

#include "AudioBufferView.h"

namespace lmms
{

//! Mixes `dry` into `wet` in place for every output channel: out = w * wet + d * dry.
//! Output channel `c` takes its dry signal from input channel `c`; an output channel
//! without a matching input (more outputs than inputs) uses the last input channel,
//! which is also how a mono input feeds both channels of a stereo output.
//! Allocation-free; safe on the audio thread. Does nothing when there is no input.
inline void mixVstWetDry(PlanarBufferView<const float> dry, PlanarBufferView<float> wet, float w, float d) noexcept
{
	if (dry.channels() == 0) { return; }

	const auto mixableOutputs = std::min<ch_cnt_t>(wet.channels(), 2);
	for (ch_cnt_t channel = 0; channel < mixableOutputs; ++channel)
	{
		auto wetBuffer = wet.buffer(channel);
		auto dryBuffer = dry.buffer(std::min(channel, static_cast<ch_cnt_t>(dry.channels() - 1)));
		for (f_cnt_t f = 0; f < wet.frames(); ++f)
		{
			wetBuffer[f] = w * wetBuffer[f] + d * dryBuffer[f];
		}
	}
}

} // namespace lmms

#endif // LMMS_VST_WET_DRY_MIX_H
