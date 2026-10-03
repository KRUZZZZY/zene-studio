/*
 * ClipTransients.cpp - the transients of a sample clip's audio
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

#include "ClipTransients.h"

#include "ImportDetection.h"
#include "SampleClip.h"

namespace lmms
{

std::vector<f_cnt_t> clipTransientFrames(SampleClip& clip)
{
	const SampleWindow window = clip.sampleWindow();
	const Sample& sample = clip.sample();
	std::vector<f_cnt_t> out;
	if (sample.data() == nullptr || window.sourceOut <= window.sourceIn
		|| static_cast<std::size_t>(window.sourceOut) > sample.sampleSize()) { return out; }
	const std::vector<float> mono = monoMix(sample.data() + window.sourceIn, static_cast<std::size_t>(window.sourceOut - window.sourceIn));
	for (const std::int64_t frame : detection::detectOnsetFrames(mono.data(), mono.size(), sample.sampleRate()))
	{
		out.push_back(window.sourceIn + static_cast<f_cnt_t>(frame));
	}
	return out;
}

std::vector<int> clipTransientTicks(SampleClip& clip)
{
	const tick_t origin = clip.startPosition().getTicks() + clip.startTimeOffset().getTicks();
	std::vector<int> out;
	for (const f_cnt_t frame : clipTransientFrames(clip)) { out.push_back(clip.timelinePosAt(frame).getTicks() - origin); }
	return out;
}

} // namespace lmms
