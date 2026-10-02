/*
 * SampleBufferResample.cpp - a whole sample buffer at another rate (offline)
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

#include "SampleBufferResample.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "AudioResampler.h"

namespace lmms
{

auto resampleBuffer(const std::shared_ptr<const SampleBuffer>& input, sample_rate_t rate)
	-> std::shared_ptr<const SampleBuffer>
{
	if (!input || input->empty() || input->sampleRate() == rate || rate == 0) { return input; }
	AudioResampler resampler(AudioResampler::Mode::SincBest, 2);
	resampler.setRatio(input->sampleRate(), rate);
	const double ratio = static_cast<double>(rate) / input->sampleRate();
	const auto wanted = static_cast<f_cnt_t>(std::llround(static_cast<double>(input->size()) * ratio));
	// Room for the filter's own delay; trimmed to `wanted` at the end.
	std::vector<SampleFrame> output(static_cast<std::size_t>(wanted) + 8192);
	f_cnt_t outPos = 0;
	const auto run = [&](const SampleFrame* in, f_cnt_t inFrames) {
		while (inFrames > 0 && outPos < static_cast<f_cnt_t>(output.size()))
		{
			const auto result = resampler.process(
				InterleavedBufferView<const float>(reinterpret_cast<const float*>(in), 2, inFrames),
				InterleavedBufferView<float>(reinterpret_cast<float*>(output.data() + outPos), 2,
					static_cast<f_cnt_t>(output.size()) - outPos));
			if (result.inputFramesUsed == 0 && result.outputFramesGenerated == 0) { break; }
			in += result.inputFramesUsed;
			inFrames -= result.inputFramesUsed;
			outPos += result.outputFramesGenerated;
		}
	};
	run(input->data(), static_cast<f_cnt_t>(input->size()));
	// Flush the filter's delay with silence, so the end of the audio is not cut.
	const std::vector<SampleFrame> silence(4096);
	while (outPos < wanted)
	{
		const f_cnt_t before = outPos;
		run(silence.data(), static_cast<f_cnt_t>(silence.size()));
		if (outPos == before) { break; }
	}
	output.resize(static_cast<std::size_t>(std::min(outPos, wanted)));
	return std::make_shared<const SampleBuffer>(std::move(output), static_cast<int>(rate), input->audioFile());
}

} // namespace lmms
