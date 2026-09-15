/*
 * ImportDetectionSpectrum.h - the spectral helpers the detection arithmetic
 *                             shares: a hand-rolled radix-2 FFT, the Hann window,
 *                             the magnitude spectrum of one frame, and the chroma
 *                             band's ramp weight.
 *
 * PRIVATE to src/core: the public surface is include/ImportDetectionDsp.h, and
 * nothing outside the three detection translation units needs a transform. The
 * helpers live here rather than in the public header so the engine's dependency
 * story stays what it is - no FFTW, no new library, one file to read.
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

#ifndef LMMS_IMPORT_DETECTION_SPECTRUM_H
#define LMMS_IMPORT_DETECTION_SPECTRUM_H

#include <cstddef>
#include <vector>

namespace lmms
{

namespace detection
{

//! Private to src/core (see the file comment).
namespace detail
{

/*! In-place iterative radix-2 FFT (decimation in time). \a re / \a im are the
 *  real and imaginary parts and their size MUST be a power of two - every call
 *  site here uses a constexpr frame size.
 *
 *  Hand-rolled on purpose: the alternative is a link dependency (FFTW is already
 *  in this build, but using it here would make these units - and the standalone
 *  proof a developer can compile with one g++ command - depend on it), and a
 *  tempo/key estimate at import time does not need a planner. */
void fftRadix2(std::vector<float>& re, std::vector<float>& im);

//! The Hann window for a frame size, precomputed once per call.
std::vector<float> hannWindow(int size);

//! The magnitude spectrum of one windowed frame at \a start; \a magnitudes is
//! resized to frameSize / 2 + 1.
void magnitudesAt(const float* mono, std::size_t start, const std::vector<float>& window,
	std::vector<float>& re, std::vector<float>& im, std::vector<float>& magnitudes);

//! How many frames of \a frameSize with \a hop fit into \a frames samples.
std::size_t frameCountFor(std::size_t frames, int frameSize, int hop);

//! The chroma band's weight at \a hz: 0 outside, 1 inside the full band, a
//! raised-cosine ramp across each edge (see ChromaBandLowHz in
//! include/ImportDetectionDsp.h for why a step edge is wrong).
double chromaBandWeight(double hz);

} // namespace detail

} // namespace detection

} // namespace lmms

#endif // LMMS_IMPORT_DETECTION_SPECTRUM_H
