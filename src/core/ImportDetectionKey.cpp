/*
 * ImportDetectionKey.cpp - the KEY half of the detection arithmetic: the chroma
 *                          vector and the tonic-weighted template correlation
 *                          over the caller's vocabulary.
 *
 * The spectral helpers are src/core/ImportDetectionSpectrum.{h,cpp}; the tempo
 * half and the public surface are src/core/ImportDetectionDsp.cpp and
 * include/ImportDetectionDsp.h. The split is where the file-length ratchet put
 * it, and it is the seam a reader wants: one file answers "what is the tempo",
 * this one answers "what is the key".
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

#include "ImportDetectionSpectrum.h"

namespace lmms
{

namespace detection
{

using namespace detail; // the spectral helpers, in ImportDetectionSpectrum.cpp

KeyEstimate estimateKey(const float* mono, std::size_t frames, int sampleRate,
	std::span<const std::uint32_t> candidateMasks, double maxSeconds)
{
	KeyEstimate estimate;
	if (mono == nullptr || frames == 0 || sampleRate <= 0 || candidateMasks.empty()) { return estimate; }

	const std::size_t limit = static_cast<std::size_t>(
		std::min<double>(static_cast<double>(frames), maxSeconds * sampleRate));
	const std::size_t frameCount = frameCountFor(limit, ChromaFrameSize, ChromaHopSize);
	if (frameCount == 0) { return estimate; }

	const std::vector<float> window = hannWindow(ChromaFrameSize);
	std::vector<float> re(ChromaFrameSize, 0.0f);
	std::vector<float> im(ChromaFrameSize, 0.0f);
	std::vector<float> magnitudes;
	std::array<double, 12> accumulated{};
	for (std::size_t frame = 0; frame < frameCount; ++frame)
	{
		magnitudesAt(mono, frame * ChromaHopSize, window, re, im, magnitudes);
		std::array<double, 12> frameChroma{};
		double frameSum = 0.0;
		for (std::size_t k = 1; k < magnitudes.size(); ++k)
		{
			const double frequency = static_cast<double>(k) * sampleRate / ChromaFrameSize;
			const double band = chromaBandWeight(frequency);
			if (band <= 0.0) { continue; }
			const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
			const double nearest = std::round(midi);
			// A triangular weight over the half semitone either side: a bin
			// between two notes contributes to both rather than to the nearer one.
			const double weight = band * std::max(0.0, 1.0 - 2.0 * std::abs(midi - nearest));
			if (weight <= 0.0) { continue; }
			const int pitchClass = ((static_cast<int>(nearest) % 12) + 12) % 12;
			frameChroma[static_cast<std::size_t>(pitchClass)] += magnitudes[k] * weight;
			frameSum += magnitudes[k] * weight;
		}
		// Per-frame normalisation: without it the loudest section of a track
		// decides the key on its own.
		if (frameSum <= 0.0) { continue; }
		for (std::size_t pc = 0; pc < 12; ++pc) { accumulated[pc] += frameChroma[pc] / frameSum; }
	}

	double largest = 0.0;
	for (const double value : accumulated) { largest = std::max(largest, value); }
	if (largest <= 0.0) { return estimate; }
	for (std::size_t pc = 0; pc < 12; ++pc) { estimate.chroma[pc] = accumulated[pc] / largest; }

	double bestScore = -1.0;
	double secondScore = -1.0;
	int bestIndex = -1;
	int bestTonic = 0;
	for (std::size_t index = 0; index < candidateMasks.size(); ++index)
	{
		const std::uint32_t mask = candidateMasks[index];
		const int degrees = maskDegreeCount(mask);
		if (degrees == 0 || degrees > MaxTemplateDegrees) { continue; }
		for (int tonic = 0; tonic < 12; ++tonic)
		{
			const double score = templateScore(estimate.chroma, mask, tonic);
			// Ties are broken by the FIRST candidate in the caller's order and,
			// within it, the lowest tonic: the same input always reports the same
			// key, which is what makes the result reproducible.
			if (score > bestScore + 1e-12)
			{
				secondScore = bestScore;
				bestScore = score;
				bestIndex = static_cast<int>(index);
				bestTonic = tonic;
			}
			else if (score > secondScore + 1e-12) { secondScore = score; }
		}
	}

	// A key is reported only when the winning template correlates POSITIVELY with
	// the chroma. This is a FLOOR, not a calibrated threshold: it says "the
	// recording looks more like this scale than like no scale", and it is stated
	// rather than tuned because no real-music corpus was measured to tune it on.
	if (bestIndex < 0 || bestScore <= 0.0) { return estimate; }
	estimate.found = true;
	estimate.candidateIndex = bestIndex;
	estimate.tonicPitchClass = bestTonic;
	estimate.templateDegrees = maskDegreeCount(candidateMasks[static_cast<std::size_t>(bestIndex)]);
	estimate.score = bestScore;
	estimate.margin = bestScore - std::max(0.0, secondScore);
	return estimate;
}

} // namespace detection

} // namespace lmms
