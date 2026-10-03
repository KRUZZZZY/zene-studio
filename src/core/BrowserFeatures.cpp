/*
 * BrowserFeatures.cpp - what a sound is like, measured from its file, and how far apart two sounds are
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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <vector>

#include "BrowserIndex.h"
#include "ImportDetection.h"
#include "ImportDetectionSpectrum.h"
#include "SampleDecoder.h"

namespace lmms
{

namespace
{

constexpr int CentroidFrame = 2048;
constexpr std::size_t MaxCentroidFrames = 64;

double rmsDb(const std::vector<float>& mono, std::size_t count)
{
	double sum = 0.0;
	for (std::size_t i = 0; i < count; ++i) { sum += static_cast<double>(mono[i]) * mono[i]; }
	return count > 0 ? 10.0 * std::log10(std::max(1e-12, sum / static_cast<double>(count))) : -120.0;
}

double crossingsPerSecond(const std::vector<float>& mono, std::size_t count, int rate)
{
	std::size_t crossings = 0;
	for (std::size_t i = 1; i < count; ++i) { crossings += (mono[i - 1] < 0.f) != (mono[i] < 0.f) ? 1 : 0; }
	return count > 1 ? static_cast<double>(crossings) * rate / static_cast<double>(count) : 0.0;
}

//! The mean spectral centroid over up to MaxCentroidFrames frames spread across the first @a count samples.
double meanCentroid(const std::vector<float>& mono, std::size_t count, int rate)
{
	if (count < static_cast<std::size_t>(CentroidFrame)) { return 0.0; }
	const std::vector<float> window = detection::detail::hannWindow(CentroidFrame);
	std::vector<float> re(CentroidFrame);
	std::vector<float> im(CentroidFrame);
	std::vector<float> magnitudes;
	const std::size_t frames = std::min(MaxCentroidFrames, count / CentroidFrame);
	// The last frame ends at the last sample: the gaps between frame starts are one fewer than the frames.
	const std::size_t step = frames > 1 ? (count - CentroidFrame) / (frames - 1) : 0;
	double total = 0.0;
	std::size_t used = 0;
	for (std::size_t f = 0; f < frames; ++f)
	{
		detection::detail::magnitudesAt(mono.data(), f * step, window, re, im, magnitudes);
		double weighted = 0.0;
		double sum = 0.0;
		for (std::size_t k = 1; k < magnitudes.size(); ++k)
		{
			weighted += magnitudes[k] * (static_cast<double>(k) * rate / CentroidFrame);
			sum += magnitudes[k];
		}
		if (sum > 1e-6) { total += weighted / sum; ++used; }
	}
	return used > 0 ? total / static_cast<double>(used) : 0.0;
}

int keyOf(const std::vector<float>& mono, std::size_t count, int rate)
{
	std::vector<std::uint32_t> masks;
	for (const ScaleCandidate& candidate : candidateScales()) { masks.push_back(candidate.mask); }
	const detection::KeyEstimate key = detection::estimateKey(mono.data(), count, rate, masks, BrowserFeatures::FeatureSeconds);
	return key.found ? key.tonicPitchClass : -1;
}

double logRatio(double a, double b, double floor)
{
	return std::abs(std::log2((a + floor) / (b + floor)));
}

//! Tempo distance, forgiving the half/double ambiguity the detection states (ImportDetectionDsp.h).
double tempoDistance(double a, double b)
{
	if (a <= 0.0 || b <= 0.0) { return 0.0; }
	const double octaves = std::abs(std::log2(a / b));
	return std::min(octaves, std::abs(octaves - 1.0)) / 0.25;
}

double keyDistance(int a, int b)
{
	if (a < 0 || b < 0) { return 0.0; }
	const int apart = std::abs(a - b) % 12;
	return std::min(apart, 12 - apart) / 6.0;
}

} // namespace

BrowserFeatures BrowserFeatures::analyse(const QString& path)
{
	BrowserFeatures features;
	const std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(path);
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0) { return features; }
	const int rate = decoded->sampleRate;
	const std::vector<float> mono = monoMix(decoded->data.data(), decoded->data.size());
	const std::size_t count = std::min(mono.size(), static_cast<std::size_t>(FeatureSeconds * rate));
	features.valid = true;
	features.durationSeconds = static_cast<double>(decoded->data.size()) / rate;
	features.loudnessDb = rmsDb(mono, count);
	features.zeroCrossings = crossingsPerSecond(mono, count, rate);
	features.centroidHz = meanCentroid(mono, count, rate);
	const detection::TempoEstimate tempo = detection::estimateTempo(mono.data(), count, rate, FeatureSeconds);
	features.bpm = tempo.found ? tempo.bpm : 0.0;
	features.keyPitchClass = keyOf(mono, count, rate);
	return features;
}

double BrowserFeatures::distance(const BrowserFeatures& a, const BrowserFeatures& b)
{
	const double brightness = a.centroidHz > 0.0 && b.centroidHz > 0.0 ? logRatio(a.centroidHz, b.centroidHz, 0.0) / 2.0 : 0.0;
	const double parts[] = {logRatio(a.durationSeconds, b.durationSeconds, 0.05) / 3.0, std::abs(a.loudnessDb - b.loudnessDb) / 24.0,
		brightness, logRatio(a.zeroCrossings, b.zeroCrossings, 1.0) / 3.0, tempoDistance(a.bpm, b.bpm), keyDistance(a.keyPitchClass, b.keyPitchClass)};
	const double weights[] = {1.0, 1.0, 2.0, 1.0, 0.5, 0.5};
	double sum = 0.0;
	for (std::size_t i = 0; i < std::size(parts); ++i) { sum += weights[i] * parts[i] * parts[i]; }
	return std::sqrt(sum / 6.0);
}

} // namespace lmms
