/*
 * ExportNoiseShaper.h - noise-shaped TPDF requantisation to 16-bit PCM
 *
 * Plain TPDF dither (include/ExportDither.h) makes the requantisation error white:
 * signal-independent, but spread flat across the whole band. Noise shaping keeps
 * that independence and moves the error's ENERGY: the error each sample leaves is
 * fed back, filtered, into the next samples before they are quantised, so the
 * noise that reaches the file is the white error shaped by the noise transfer
 * function
 *
 *     NTF(z) = 1 - 1.623 z^-1 + 0.982 z^-2 - 0.109 z^-3
 *
 * - the 3-tap psychoacoustically weighted error-feedback filter from the
 * noise-shaping literature (Wannamaker, "Psychoacoustically Optimal Noise Shaping",
 * JAES 40(7/8), 1992). Its gain is 0.25 (-12 dB) at DC and 3.71 (+11.4 dB) at
 * Nyquist: less noise where hearing is most sensitive, more where it is least.
 *
 * The TPDF offsets come from ExportDither's own seeded generator, so a noise-shaped
 * render is exactly as reproducible as a TPDF one. The fed-back error is bounded
 * (+-2 LSB): a clipped sample would otherwise feed a large error back and the loop
 * could ring.
 *
 * Only the 16-bit path uses it (the encoder quantises 16-bit itself, so the error is
 * known); see include/ExportDitherMode.h.
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

#ifndef LMMS_EXPORT_NOISE_SHAPER_H
#define LMMS_EXPORT_NOISE_SHAPER_H

#include <array>
#include <cstdint>
#include <vector>

#include "ExportDither.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class LMMS_EXPORT ExportNoiseShaper
{
public:
	//! The error-feedback coefficients h1..h3 (NTF = 1 - h1 z^-1 - h2 z^-2 - h3 z^-3).
	static constexpr std::array<float, 3> Coefficients{1.623f, -0.982f, 0.109f};
	//! The largest error, in LSB, that is fed back.
	static constexpr float MaxFeedbackLsb = 2.0f;

	explicit ExportNoiseShaper(int channels, std::uint64_t seed = ExportDither::DefaultSeed);

	//! One sample of @a channel (full scale 1.0) requantised to signed 16-bit.
	std::int16_t quantise16(float sample, int channel) noexcept;

	/*! @a frames SampleFrames to interleaved 16-bit PCM in @a out, @a channels wide,
	 *  byte-swapped when @a swapEndian - the layout AudioDevice::convertToS16 writes. */
	void quantiseFrames(const SampleFrame* in, f_cnt_t frames, int channels,
		int_sample_t* out, bool swapEndian) noexcept;

	//! Back to the starting state (the same seed gives the same output again).
	void reset(std::uint64_t seed = ExportDither::DefaultSeed) noexcept;

private:
	ExportDither m_dither;
	//! Per channel, the last three fed-back errors in LSB, newest first.
	std::vector<std::array<float, 3>> m_error;
};

} // namespace lmms

#endif // LMMS_EXPORT_NOISE_SHAPER_H
