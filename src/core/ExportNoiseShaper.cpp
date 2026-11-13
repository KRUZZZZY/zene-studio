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
	// The error history is sized by the constructor's channel count, so a channel
	// outside it has no history to shape against - and indexing it read (and, via
	// the write-back below, corrupted the heap). Such a channel is silence.
	if (channel < 0 || static_cast<std::size_t>(channel) >= m_error.size()) { return 0; }
	auto& e = m_error[static_cast<std::size_t>(channel)];
	// AudioEngine::clip passes NaN through (both `s > 1` and `s < -1` are
	// false for it), so lround would receive NaN - and the resulting NaN
	// error would live in e and poison every later sample of this channel
	// (lround prints -32768, the shown output, and NaN sticks). A
	// non-finite clipped sample is dropped as silence and this channel's
	// history reset; Infinities are not affected, because clip already
	// maps them to +-1 before the check.
	const float clipped = AudioEngine::clip(sample);
	if (!std::isfinite(clipped))
	{
		e = {0.f, 0.f, 0.f};
		return 0;
	}
	// Belt-and-braces: a channel history must never feed NaN back in, even
	// if one was corrupted by an earlier bad sample before this guard.
	if (!std::isfinite(e[0]) || !std::isfinite(e[1]) || !std::isfinite(e[2]))
	{
		e = {0.f, 0.f, 0.f};
	}
	// The same scale and clip the plain 16-bit path uses (AudioDevice::convertToS16), so
	// a shaped render and an unshaped one differ only in the requantisation.
	const float target = clipped * OUTPUT_SAMPLE_MULTIPLIER;
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
