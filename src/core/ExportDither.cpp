/*
 * ExportDither.cpp - TPDF dither for the integer export formats.
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

#include "ExportDither.h"

#include "lmms_constants.h"

namespace lmms
{

namespace
{

/*! splitmix64, written out rather than taken from <random>.
 *
 *  Two reasons, and both are about the property this file exists to hold:
 *  `std::uniform_real_distribution` is *not* specified to produce the same
 *  sequence on two implementations, so a render could differ between libstdc++
 *  and libc++; and the engine's reproducibility claim is byte-identity. A fixed
 *  64-bit mixer has neither problem and costs nothing measurable on the export
 *  path, which is not the real-time path.
 */
inline std::uint64_t mix(std::uint64_t& state) noexcept
{
	state += 0x9E3779B97F4A7C15ULL;
	std::uint64_t z = state;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

} // namespace

ExportDither::ExportDither(std::uint64_t seed) noexcept
{
	reseed(seed);
}

void ExportDither::reseed(std::uint64_t seed) noexcept
{
	m_seed = seed;
	m_state = seed;
}

float ExportDither::lsbForBitDepth(int bits) noexcept
{
	// The float domain's full scale is 1.0 and the largest magnitude an N-bit
	// signed sample can hold is 2^(N-1) - 1, which is the multiplier the
	// engine's own float -> int conversion uses (OUTPUT_SAMPLE_MULTIPLIER is
	// 32767.0f for 16 bit). Deriving the step any other way would put the
	// dither at a different level than the quantiser it is meant to dither.
	switch (bits)
	{
	case 16: return 1.0f / 32767.0f;
	case 24: return 1.0f / 8388607.0f;
	default: return 0.0f;
	}
}

double ExportDither::nextUniform() noexcept
{
	// 53 significant bits, the same construction <random> uses for a double:
	// every value in [0, 1) is representable and none is favoured by rounding.
	return static_cast<double>(mix(m_state) >> 11) * (1.0 / 9007199254740992.0);
}

float ExportDither::nextOffset(float lsb) noexcept
{
	// TPDF: the sum of two independent uniforms on [-0.5, +0.5) lsb, i.e. a
	// triangle on [-1, +1) lsb. Variance lsb^2 / 6, which is what makes the
	// total error's variance lsb^2 / 4 (tests/src/core/ExportDitherTest.cpp
	// measures both).
	const double u1 = nextUniform() - 0.5;
	const double u2 = nextUniform() - 0.5;
	return static_cast<float>((u1 + u2) * static_cast<double>(lsb));
}

void ExportDither::ditherFrames(SampleFrame* frames, std::size_t frameCount, int bits) noexcept
{
	const float lsb = lsbForBitDepth(bits);
	if (lsb <= 0.0f) { return; }
	for (std::size_t i = 0; i < frameCount; ++i)
	{
		for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch)
		{
			frames[i][ch] += nextOffset(lsb);
		}
	}
}

void ExportDither::ditherInterleaved(float* samples, std::size_t sampleCount, int bits) noexcept
{
	const float lsb = lsbForBitDepth(bits);
	if (lsb <= 0.0f) { return; }
	for (std::size_t i = 0; i < sampleCount; ++i)
	{
		samples[i] += nextOffset(lsb);
	}
}

} // namespace lmms
