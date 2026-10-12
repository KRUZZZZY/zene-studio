/*
 * SessionAudioBake.cpp - warp and repitch of an audio cell's clip, off the audio thread (Q050-013)
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

#include "SessionAudioBake.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "AudioResampler.h"
#include "AudioStretcher.h"
#include "SessionModel.h"

namespace lmms::sessionaudio
{

bool BakeParams::isIdentity() const noexcept
{
	return std::fabs(warpSpeed - 1.0) < 1e-9 && std::fabs(pitchRatio - 1.0) < 1e-9;
}

BakeParams bakeParamsFor(const ClipSlot& slot, double tempo) noexcept
{
	BakeParams params;
	if (slot.warp() && slot.warpBpm() > 0.0 && tempo > 0.0) { params.warpSpeed = tempo / slot.warpBpm(); }
	params.pitchRatio = std::exp2((static_cast<double>(slot.transpose()) + slot.detune() / 100.0) / 12.0);
	return params;
}

namespace
{

std::vector<SampleFrame> stretched(const SampleFrame* source, std::size_t frames, double speed)
{
	const auto out = static_cast<std::size_t>(std::max(1.0, std::round(static_cast<double>(frames) / speed)));
	std::vector<SampleFrame> dst(out);
	auto stretcher = std::make_unique<AudioStretcher>();  // ~30 KB of state: not for the stack
	stretcher->prepare();
	stretcher->process(source, static_cast<f_cnt_t>(frames), dst.data(), static_cast<f_cnt_t>(out), speed);
	return dst;
}

std::vector<SampleFrame> repitched(const SampleFrame* source, std::size_t frames, double ratio)
{
	// Playing `ratio` times faster is the same as resampling to 1/ratio of the length.
	AudioResampler resampler(AudioResampler::Mode::SincMedium);
	resampler.setRatio(1.0 / ratio);
	std::vector<SampleFrame> out(static_cast<std::size_t>(std::ceil(static_cast<double>(frames) / ratio)) + 8);
	const auto result = resampler.process({&source[0][0], 2, frames}, {&out[0][0], 2, out.size()});
	out.resize(static_cast<std::size_t>(result.outputFramesGenerated));
	return out;
}

} // namespace

std::vector<SampleFrame> bakeClip(const SampleFrame* source, std::size_t frames, const BakeParams& params)
{
	if (frames == 0) { return {}; }
	std::vector<SampleFrame> current;
	const SampleFrame* from = source;
	std::size_t count = frames;
	if (std::fabs(params.warpSpeed - 1.0) >= 1e-9 && params.warpSpeed > 0.0)
	{
		current = stretched(from, count, params.warpSpeed);
		from = current.data();
		count = current.size();
	}
	if (std::fabs(params.pitchRatio - 1.0) >= 1e-9 && params.pitchRatio > 0.0)
	{
		current = repitched(from, count, params.pitchRatio);
		return current;
	}
	if (!current.empty()) { return current; }
	return std::vector<SampleFrame>(source, source + frames);
}

} // namespace lmms::sessionaudio
