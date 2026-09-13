/*
 * ExportDither.h - TPDF dither for the integer export formats.
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

#ifndef LMMS_EXPORT_DITHER_H
#define LMMS_EXPORT_DITHER_H

#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/*! TPDF (triangular-PDF) dither for the integer export formats.
 *
 *  **What it is for.** Quantising a float to N bits replaces every sample with
 *  the nearest N-bit level. The error that leaves behind is a deterministic
 *  function of the signal, so at low levels it is not noise but *correlated
 *  distortion* - the granulation you hear on a decaying reverb tail rendered to
 *  16 bit. Dither removes the correlation by adding a small random offset
 *  BEFORE the quantiser; the error then becomes signal-independent noise at a
 *  known, slightly higher level.
 *
 *  **Why TPDF.** The offset is the SUM OF TWO independent uniform values on
 *  [-0.5, +0.5) LSB, i.e. a triangular distribution on [-1, +1) LSB with
 *  variance 1/6 LSB^2. That is the distribution which makes the *total* error
 *  (dither plus rounding) have variance exactly LSB^2 / 4 - standard deviation
 *  0.5 LSB - with zero mean INDEPENDENT of the signal, and decorrelates it from
 *  the input. A single uniform offset (RPDF) leaves the error's first moment
 *  signal-dependent, which is why RPDF is not what is implemented here.
 *
 *  **Determinism, deliberately.** The generator is a fixed splitmix64 seeded
 *  from a constant, so a dithered render is *reproducible*: two renders of the
 *  same project with dither on produce identical files. That matters because
 *  this project's claim is byte-identical renders (docs/RENDER-DETERMINISM.md);
 *  a non-deterministic dither would forfeit it for every dithered export. The
 *  seed is also an explicit member so a test can pin the bytes.
 *
 *  **OFF BY DEFAULT.** `OutputSettings::dither()` is false and stays false
 *  unless a caller asks (the `--dither` CLI flag, or `export.set_dither` over
 *  the control socket), so the bundled projects that render byte-identically
 *  keep doing so.
 *
 *  **Scope.** Applied by `AudioFileWave::writeBuffer` to the WAV export, at the
 *  bit depth actually written: 16-bit (the integer path) and 24-bit (the float
 *  path, which libsndfile quantises). 32-bit float is NOT dithered - a float
 *  format has no fixed quantisation step to dither against, and adding noise to
 *  it would only degrade it. The other file formats (FLAC/OGG/MP3) do not take
 *  the dither yet; that is written down in docs/KNOWN-LIMITATIONS.md.
 */
class LMMS_EXPORT ExportDither
{
public:
	//! The seed a render uses unless a caller says otherwise. Any fixed value
	//! would do; this one is written out so a reader can reproduce a render.
	static constexpr std::uint64_t DefaultSeed = 0x5A17E2D0D1A6E001ULL;

	explicit ExportDither(std::uint64_t seed = DefaultSeed) noexcept;

	//! Back to the starting state: the same seed gives the same sequence again.
	void reseed(std::uint64_t seed = DefaultSeed) noexcept;

	std::uint64_t seed() const noexcept { return m_seed; }

	/*! The quantisation step of \p bits-bit signed PCM, expressed in the float
	 *  domain `SampleFrame` uses (full scale = 1.0): 16 bit -> 1/32767,
	 *  24 bit -> 1/8388607. Zero for a depth with no fixed step (32-bit float)
	 *  or outside the integer range, which every caller reads as "nothing to
	 *  dither".
	 */
	static float lsbForBitDepth(int bits) noexcept;

	//! One TPDF offset, drawn in [-lsb, +lsb).
	float nextOffset(float lsb) noexcept;

	/*! Adds the dither in place, one independent offset per channel sample.
	 *  \p frameCount counts SampleFrames; every channel of every frame is
	 *  dithered. A no-op when `lsbForBitDepth(bits)` is 0.
	 */
	void ditherFrames(SampleFrame* frames, std::size_t frameCount, int bits) noexcept;

	/*! The same for an interleaved float buffer (the WAV 24-bit path writes
	 *  one). \p sampleCount counts floats, not frames.
	 */
	void ditherInterleaved(float* samples, std::size_t sampleCount, int bits) noexcept;

private:
	//! One uniform value on [0, 1).
	double nextUniform() noexcept;

	std::uint64_t m_seed;
	std::uint64_t m_state;
};

} // namespace lmms

#endif // LMMS_EXPORT_DITHER_H
