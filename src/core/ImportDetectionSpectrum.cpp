/*
 * ImportDetectionSpectrum.cpp - the spectral helpers the detection arithmetic
 *                               shares (see src/core/ImportDetectionSpectrum.h):
 *                               a hand-rolled radix-2 FFT, the Hann window, the
 *                               magnitude spectrum of one frame and the chroma
 *                               band's ramp.
 *
 * THE TWO METHODS, NAMED, because a feature that cannot say how it works cannot
 * state its accuracy honestly:
 *
 *   tempo: spectral flux -> a local-mean-removed onset envelope -> AUTOCORRELATION
 *          over the 40..240 BPM band, with the metrical alternatives (half and
 *          third of the lag) compared by a comb score so a click track at 128 BPM
 *          reports 128 and not 64 or 42.7.
 *   key:   a 12-bin chroma vector over 110 Hz..3 kHz -> a tonic-weighted set match
 *          against the scale templates the CALLER passes in (the engine layer
 *          fills that list from the pre-existing ChordTable vocabulary).
 *
 * Both are classic, published approaches in outline (a spectral-flux onset
 * detector and a chroma/template key estimate); the constants this file chooses
 * (window sizes, the band, the comb weights, the tonic weighting) are stated at
 * their definitions and in docs/IMPORT-DETECTION.md, because they are this
 * project's choices and not measurements of anything.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ImportDetectionDsp.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace lmms
{

namespace detection
{

/*! In-place iterative radix-2 FFT (decimation in time). \a re / \a im are the
 *  real and imaginary parts and their size MUST be a power of two - every call
 *  site here uses a constexpr frame size.
 *
 *  Hand-rolled on purpose: the alternative is a link dependency (FFTW is already
 *  in this build, but using it here would make this unit - and the standalone
 *  proof a developer can compile with one g++ command - depend on it), and a
 *  tempo/key estimate at import time does not need a planner. */
namespace detail
{

void fftRadix2(std::vector<float>& re, std::vector<float>& im)
{
	const std::size_t n = re.size();
	if (n < 2) { return; }

	// Bit-reversal permutation.
	for (std::size_t i = 1, j = 0; i < n; ++i)
	{
		std::size_t bit = n >> 1;
		for (; (j & bit) != 0; bit >>= 1) { j ^= bit; }
		j ^= bit;
		if (i < j)
		{
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}

	for (std::size_t len = 2; len <= n; len <<= 1)
	{
		const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
		const float wRe = static_cast<float>(std::cos(angle));
		const float wIm = static_cast<float>(std::sin(angle));
		for (std::size_t i = 0; i < n; i += len)
		{
			float curRe = 1.0f;
			float curIm = 0.0f;
			for (std::size_t k = 0; k < len / 2; ++k)
			{
				const std::size_t a = i + k;
				const std::size_t b = a + len / 2;
				const float tRe = re[b] * curRe - im[b] * curIm;
				const float tIm = re[b] * curIm + im[b] * curRe;
				re[b] = re[a] - tRe;
				im[b] = im[a] - tIm;
				re[a] += tRe;
				im[a] += tIm;
				const float nextRe = curRe * wRe - curIm * wIm;
				curIm = curRe * wIm + curIm * wRe;
				curRe = nextRe;
			}
		}
	}
}

} // namespace detail

//! The Hann window for a frame size, precomputed once per call.
namespace detail
{

std::vector<float> hannWindow(int size)
{
	std::vector<float> window(static_cast<std::size_t>(size));
	for (int i = 0; i < size; ++i)
	{
		window[static_cast<std::size_t>(i)] = static_cast<float>(
			0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * i / (size - 1))));
	}
	return window;
}

} // namespace detail

//! The band's weight at \a hz: 0 outside, 1 inside the full band, a
//! raised-cosine ramp across each edge (see ChromaBandLowHz in the header for
//! why a step edge is wrong).
namespace detail
{

double chromaBandWeight(double hz)
{
	auto ramp = [](double value) { return 0.5 * (1.0 - std::cos(std::numbers::pi * value)); };
	if (hz <= ChromaBandLowHz || hz >= ChromaBandHighHz) { return 0.0; }
	if (hz < ChromaBandFullLowHz)
	{
		return ramp((hz - ChromaBandLowHz) / (ChromaBandFullLowHz - ChromaBandLowHz));
	}
	if (hz > ChromaBandFullHighHz)
	{
		return ramp((ChromaBandHighHz - hz) / (ChromaBandHighHz - ChromaBandFullHighHz));
	}
	return 1.0;
}

} // namespace detail

//! How many frames of \a frameSize with \a hop fit into \a frames samples.
namespace detail
{

std::size_t frameCountFor(std::size_t frames, int frameSize, int hop)
{
	if (frames < static_cast<std::size_t>(frameSize)) { return 0; }
	return 1 + (frames - static_cast<std::size_t>(frameSize)) / static_cast<std::size_t>(hop);
}

} // namespace detail

//! The magnitude spectrum of one windowed frame; \a magnitudes is resized.
namespace detail
{

void magnitudesAt(const float* mono, std::size_t start, const std::vector<float>& window,
	std::vector<float>& re, std::vector<float>& im, std::vector<float>& magnitudes)
{
	const std::size_t n = window.size();
	std::fill(im.begin(), im.end(), 0.0f);
	for (std::size_t i = 0; i < n; ++i) { re[i] = mono[start + i] * window[i]; }
	fftRadix2(re, im);
	magnitudes.resize(n / 2 + 1);
	for (std::size_t k = 0; k < magnitudes.size(); ++k)
	{
		magnitudes[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
	}
}

} // namespace detail

} // namespace detection

} // namespace lmms
