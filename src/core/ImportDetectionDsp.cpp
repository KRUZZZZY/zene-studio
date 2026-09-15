/*
 * ImportDetectionDsp.cpp - the detection arithmetic (see
 *                          include/ImportDetectionDsp.h for what this is and
 *                          why it carries no dependency).
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

namespace
{

/*! In-place iterative radix-2 FFT (decimation in time). \a re / \a im are the
 *  real and imaginary parts and their size MUST be a power of two - every call
 *  site here uses a constexpr frame size.
 *
 *  Hand-rolled on purpose: the alternative is a link dependency (FFTW is already
 *  in this build, but using it here would make this unit - and the standalone
 *  proof a developer can compile with one g++ command - depend on it), and a
 *  tempo/key estimate at import time does not need a planner. */
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

//! The Hann window for a frame size, precomputed once per call.
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

//! The band's weight at \a hz: 0 outside, 1 inside the full band, a
//! raised-cosine ramp across each edge (see ChromaBandLowHz in the header for
//! why a step edge is wrong).
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

//! How many frames of \a frameSize with \a hop fit into \a frames samples.
std::size_t frameCountFor(std::size_t frames, int frameSize, int hop)
{
	if (frames < static_cast<std::size_t>(frameSize)) { return 0; }
	return 1 + (frames - static_cast<std::size_t>(frameSize)) / static_cast<std::size_t>(hop);
}

//! The magnitude spectrum of one windowed frame; \a magnitudes is resized.
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

/*! The tempo prior: a log-Gaussian weight centred at 120 BPM.
 *
 *  WHY AN AUTOCORRELATION IS NOT ENOUGH. The envelope of a click track at
 *  128 BPM correlates with itself at ONE beat and at THREE just as well (the
 *  higher lags are actually better: the period is not an integer number of
 *  hops, and the rounding error is smallest at the longest lag), so "take the
 *  largest peak" reported 42.7 BPM for a track at 128. Measured, not feared: it
 *  is the first thing this lane's own harness caught.
 *
 *  The shape is the one beat trackers use for the same reason - a log-Gaussian
 *  centred on the tempo a listener would tap - and it is DECLARED here with its
 *  failure mode: a track whose true tempo is far from the centre (a 50 BPM
 *  ballad, a 200 BPM hardcore track) can still be reported at its metrical
 *  relative, and real-world accuracy is unverified on this box
 *  (docs/IMPORT-DETECTION.md section 5). */
constexpr double TempoPriorCentreBpm = 120.0;
constexpr double TempoPriorOctaveWidth = 0.9;

double tempoPrior(double bpm)
{
	const double octaves = std::log2(bpm / TempoPriorCentreBpm) / TempoPriorOctaveWidth;
	return std::exp(-0.5 * octaves * octaves);
}

/*! The lag the tempo estimate picks: the largest PRIOR-WEIGHTED autocorrelation
 *  in the band. Ties go to the SMALLER lag (the faster tempo), so the choice is
 *  reproducible. */
struct CombResult
{
	int lag = -1;
	double score = 0.0;
};

CombResult bestLag(const std::vector<double>& acf, int minLag, int maxLag, double hopSeconds)
{
	CombResult best;
	for (int lag = minLag; lag <= maxLag; ++lag)
	{
		const double bpm = 60.0 / (lag * hopSeconds);
		const double score = acf[static_cast<std::size_t>(lag)] * tempoPrior(bpm);
		if (score > best.score + 1e-12)
		{
			best.score = score;
			best.lag = lag;
		}
	}
	return best;
}

/*! The period in hops, refined against the LONGEST harmonic of the winning lag
 *  that still fits in the band.
 *
 *  A lag is an integer number of hops, so a 128 BPM click track's true period
 *  (40.37 hops) can only be read as 40 - a 0.9% error. The same period measured
 *  at three times the lag is 121.1 hops, which rounds to a 0.1% error, and the
 *  division by three carries that precision back. */
double refinedPeriodHops(const std::vector<double>& acf, int lag, int minLag, int maxLag)
{
	int bestMultiple = 0;
	double bestValue = -1.0;
	for (int multiple = std::max(1, maxLag / lag); multiple >= 1; --multiple)
	{
		const int centre = static_cast<int>(std::lround(static_cast<double>(multiple * lag)));
		for (int candidate = centre - 2; candidate <= centre + 2; ++candidate)
		{
			if (candidate < minLag || candidate > maxLag) { continue; }
			const double value = acf[static_cast<std::size_t>(candidate)];
			if (value > bestValue + 1e-12)
			{
				bestValue = value;
				bestMultiple = multiple;
			}
		}
		if (bestMultiple > 0) { break; }
	}
	if (bestMultiple <= 1) { return static_cast<double>(lag); }

	// The best integer lag near multiple * lag, then divided back.
	int bestCandidate = lag;
	double bestCandidateValue = -1.0;
	const int centre = static_cast<int>(std::lround(static_cast<double>(bestMultiple * lag)));
	for (int candidate = centre - 2; candidate <= centre + 2; ++candidate)
	{
		if (candidate < minLag || candidate > maxLag) { continue; }
		const double value = acf[static_cast<std::size_t>(candidate)];
		if (value > bestCandidateValue + 1e-12)
		{
			bestCandidateValue = value;
			bestCandidate = candidate;
		}
	}
	return static_cast<double>(bestCandidate) / bestMultiple;
}

} // namespace

const char* pitchClassName(int pitchClass)
{
	static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F",
		"F#", "G", "G#", "A", "A#", "B"};
	if (pitchClass < 0 || pitchClass > 11) { return ""; }
	return names[pitchClass];
}

int maskDegreeCount(std::uint32_t mask)
{
	int count = 0;
	for (int bit = 0; bit < 12; ++bit)
	{
		if ((mask & (1u << bit)) != 0u) { ++count; }
	}
	return count;
}

/*! The score of one candidate: the PEARSON CORRELATION between the chroma vector
 *  and the template's tonic-weighted degree pattern.
 *
 *  WHY THE TONIC IS WEIGHTED. The template's degree SET alone cannot tell
 *  relative keys apart - C major and A aeolian are the same seven pitch classes
 *  - so a plain set match ties on exactly the question a key estimate exists to
 *  answer. The chroma mass on the tonic breaks that tie, and it is the same
 *  signal the ear uses: music in C major sits on C.
 *
 *  WHY A CORRELATION AND NOT A MEAN OF THE DEGREES. The first version of this
 *  scored `chroma[tonic] + 0.5 * mean(chroma over the other degrees)`, and the
 *  registered test measured what that does: the mean is diluted by every degree
 *  the recording does not play, so a SMALLER template wins on a fixture it does
 *  not describe - an A major scale over an A bass was reported as "Neopolitan"
 *  with a 0.003 margin over the right answer. A correlation over all twelve
 *  pitch classes is scale-free with respect to the degree count: a template is
 *  rewarded for the notes it explains AND penalised for the ones it expects and
 *  the recording does not have, which is the whole question.
 *
 *  The weights (tonic 2, other degrees 1, everything else 0) are this project's
 *  own and DECLARED rather than borrowed: no published key-profile constant
 *  (Krumhansl-Kessler or otherwise) is copied into this file, because a borrowed
 *  profile would carry a claim about real music this lane cannot measure. See
 *  docs/IMPORT-DETECTION.md section 4. */
double templateScore(const std::array<double, 12>& chroma, std::uint32_t mask, int tonic)
{
	if (maskDegreeCount(mask) == 0) { return 0.0; }
	std::array<double, 12> weights{};
	for (int degree = 0; degree < 12; ++degree)
	{
		if ((mask & (1u << degree)) == 0u) { continue; }
		weights[static_cast<std::size_t>(((tonic + degree) % 12 + 12) % 12)] =
			degree == 0 ? TonicWeight : 1.0;
	}

	double meanChroma = 0.0;
	double meanWeight = 0.0;
	for (std::size_t pc = 0; pc < 12; ++pc)
	{
		meanChroma += chroma[pc];
		meanWeight += weights[pc];
	}
	meanChroma /= 12.0;
	meanWeight /= 12.0;

	double covariance = 0.0;
	double chromaVariance = 0.0;
	double weightVariance = 0.0;
	for (std::size_t pc = 0; pc < 12; ++pc)
	{
		const double chromaDeviation = chroma[pc] - meanChroma;
		const double weightDeviation = weights[pc] - meanWeight;
		covariance += chromaDeviation * weightDeviation;
		chromaVariance += chromaDeviation * chromaDeviation;
		weightVariance += weightDeviation * weightDeviation;
	}
	if (chromaVariance <= 0.0 || weightVariance <= 0.0) { return 0.0; }
	return covariance / std::sqrt(chromaVariance * weightVariance);
}

TempoEstimate estimateTempo(const float* mono, std::size_t frames, int sampleRate, double maxSeconds)
{
	TempoEstimate estimate;
	if (mono == nullptr || frames == 0 || sampleRate <= 0) { return estimate; }

	const std::size_t limit = static_cast<std::size_t>(
		std::min<double>(static_cast<double>(frames), maxSeconds * sampleRate));
	const std::size_t frameCount = frameCountFor(limit, TempoFrameSize, TempoHopSize);
	if (frameCount < 4) { return estimate; }

	const std::vector<float> window = hannWindow(TempoFrameSize);
	std::vector<float> re(TempoFrameSize, 0.0f);
	std::vector<float> im(TempoFrameSize, 0.0f);
	std::vector<float> previous;
	std::vector<float> current;
	std::vector<double> flux(frameCount, 0.0);
	for (std::size_t frame = 0; frame < frameCount; ++frame)
	{
		magnitudesAt(mono, frame * TempoHopSize, window, re, im, current);
		if (frame > 0)
		{
			double sum = 0.0;
			for (std::size_t k = 1; k < current.size(); ++k)
			{
				const double difference = static_cast<double>(current[k]) - previous[k];
				if (difference > 0.0) { sum += difference; }
			}
			flux[frame] = sum / static_cast<double>(current.size());
		}
		previous = current;
	}

	// The onset envelope: the flux with a running mean (~0.35 s) taken off it and
	// the negative half discarded, so a slow crescendo is not a beat.
	const int meanWindow = std::max(1, static_cast<int>(0.35 * sampleRate / TempoHopSize));
	std::vector<double> novelty(flux.size(), 0.0);
	for (std::size_t i = 0; i < flux.size(); ++i)
	{
		const std::size_t from = i > static_cast<std::size_t>(meanWindow) ? i - meanWindow : 0;
		const std::size_t to = std::min(flux.size(), i + static_cast<std::size_t>(meanWindow) + 1);
		double mean = 0.0;
		for (std::size_t j = from; j < to; ++j) { mean += flux[j]; }
		mean /= static_cast<double>(to - from);
		novelty[i] = std::max(0.0, flux[i] - mean);
	}

	// The transients: local maxima above a declared fraction of the envelope's
	// own maximum AND above a floor derived from the signal's own RMS, so the
	// numerical residue of a STATIONARY signal is not reported as a beat (a
	// steady tone measured 112 "transients" without the floor). The first one is
	// where a clip's first hit is.
	double rms = 0.0;
	for (std::size_t i = 0; i < limit; ++i) { rms += static_cast<double>(mono[i]) * mono[i]; }
	rms = std::sqrt(rms / static_cast<double>(limit));
	const double floor = 1e-3 * rms;
	double peak = 0.0;
	for (const double value : novelty) { peak = std::max(peak, value); }
	if (peak <= floor || peak <= 0.0) { return estimate; }
	const double threshold = std::max(floor, 0.10 * peak);
	for (std::size_t i = 1; i + 1 < novelty.size(); ++i)
	{
		if (novelty[i] <= threshold || novelty[i] < novelty[i - 1] || novelty[i] < novelty[i + 1]) { continue; }
		++estimate.onsets;
		if (estimate.onsets == 1)
		{
			const double centre = (static_cast<double>(i) * TempoHopSize) + TempoFrameSize / 2.0;
			estimate.firstOnsetSeconds = centre / sampleRate;
			estimate.firstOnsetFrame = static_cast<std::int64_t>(centre);
		}
	}

	// The autocorrelation of the envelope over the declared band.
	const double hopSeconds = static_cast<double>(TempoHopSize) / sampleRate;
	const int minLag = std::max(1, static_cast<int>(std::floor((60.0 / MaxDetectionBpm) / hopSeconds)));
	const int maxLag = std::min(static_cast<int>(novelty.size()) - 1,
		static_cast<int>(std::ceil((60.0 / MinDetectionBpm) / hopSeconds)));
	if (maxLag <= minLag + 2) { return estimate; }

	double energy = 0.0;
	for (const double value : novelty) { energy += value * value; }
	if (energy <= 0.0) { return estimate; }

	std::vector<double> acf(static_cast<std::size_t>(maxLag) + 1, 0.0);
	for (int lag = minLag; lag <= maxLag; ++lag)
	{
		double sum = 0.0;
		for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < novelty.size(); ++i)
		{
			sum += novelty[i] * novelty[i + static_cast<std::size_t>(lag)];
		}
		acf[static_cast<std::size_t>(lag)] = sum / energy;
	}

	const CombResult best = bestLag(acf, minLag, maxLag, hopSeconds);
	if (best.lag < 0) { return estimate; }

	// The period: the winning lag, refined against its longest in-band harmonic
	// (see refinedPeriodHops) so a non-integer period is not read as the nearer
	// integer lag.
	const double refinedLag = refinedPeriodHops(acf, best.lag, minLag, maxLag);

	estimate.bpm = 60.0 / (refinedLag * hopSeconds);
	estimate.periodSeconds = refinedLag * hopSeconds;
	estimate.confidence = acf[static_cast<std::size_t>(best.lag)];
	// A tempo needs transients to have come from somewhere, and an envelope that
	// does not correlate with itself is not a beat: below four transients or a
	// 0.2 correlation this reports NOTHING rather than a number.
	estimate.found = estimate.onsets >= 4 && estimate.confidence >= 0.2
		&& estimate.bpm >= MinDetectionBpm && estimate.bpm <= MaxDetectionBpm;
	return estimate;
}

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
