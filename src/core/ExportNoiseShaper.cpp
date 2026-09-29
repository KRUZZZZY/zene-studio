/*
 * ExportNoiseShaper.cpp - noise-shaped TPDF requantisation (include/ExportNoiseShaper.h)
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

#include "ExportNoiseShaper.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"

namespace lmms
{

ExportNoiseShaper::ExportNoiseShaper(int channels, std::uint64_t seed) :
	m_dither(seed),
	m_error(static_cast<std::size_t>(std::max(channels, 1)), std::array<float, 3>{0.f, 0.f, 0.f})
{
}

void ExportNoiseShaper::reset(std::uint64_t seed) noexcept
{
	m_dither.reseed(seed);
	for (auto& history : m_error) { history = {0.f, 0.f, 0.f}; }
}

std::int16_t ExportNoiseShaper::quantise16(float sample, int channel) noexcept
{
	auto& e = m_error[static_cast<std::size_t>(channel)];
	// The same scale and clip the plain 16-bit path uses (AudioDevice::convertToS16), so
	// a shaped render and an unshaped one differ only in the requantisation.
	const float target = AudioEngine::clip(sample) * OUTPUT_SAMPLE_MULTIPLIER;
	const float shaped = target
		- (Coefficients[0] * e[0] + Coefficients[1] * e[1] + Coefficients[2] * e[2]);
	const float dithered = shaped + m_dither.nextOffset(1.0f);
	const long rounded = std::clamp(std::lround(dithered), -32768L, 32767L);
	const float error = std::clamp(static_cast<float>(rounded) - shaped, -MaxFeedbackLsb, MaxFeedbackLsb);
	e = {error, e[0], e[1]};
	return static_cast<std::int16_t>(rounded);
}

void ExportNoiseShaper::quantiseFrames(const SampleFrame* in, f_cnt_t frames, int channels,
	int_sample_t* out, bool swapEndian) noexcept
{
	for (f_cnt_t frame = 0; frame < frames; ++frame)
	{
		for (int chnl = 0; chnl < channels; ++chnl)
		{
			const std::int16_t value = quantise16(in[frame][chnl], chnl);
			const auto bits = static_cast<std::uint16_t>(value);
			out[frame * channels + chnl] = static_cast<int_sample_t>(swapEndian
				? static_cast<std::uint16_t>((bits & 0x00ff) << 8 | (bits & 0xff00) >> 8) : bits);
		}
	}
}

} // namespace lmms
