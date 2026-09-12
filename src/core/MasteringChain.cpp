/*
 * MasteringChain.cpp - offline mastering chain and BS.1770-4 measurement
 *
 * Copyright (c) 2026 Zene Studio developers
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

#include "MasteringChain.h"

#include <algorithm>
#include <cmath>

#include "LufsMeter.h"
#include "lmms_constants.h"

namespace lmms
{

namespace
{

//! 100 ms: the meter closes one sub-block per this many frames, so polling the
//! short-term window once per 100 ms observes every value it ever publishes.
constexpr int SubBlockMilliseconds = 100;

//! Peak limiter release time. Fixed: it is a safety net, not a sound parameter.
constexpr double LimiterReleaseSeconds = 0.050;

void applyGainDb(std::vector<SampleFrame>& frames, float gainDb)
{
	const auto gain = static_cast<float>(std::pow(10.0, gainDb / 20.0));
	for (auto& frame : frames)
	{
		frame.setLeft(frame.left() * gain);
		frame.setRight(frame.right() * gain);
	}
}

float linearToDb(double linear)
{
	return linear > 0.0 ? static_cast<float>(20.0 * std::log10(linear)) : LufsMeter::MinusInfinity;
}

double dbToLinear(float db)
{
	return std::pow(10.0, static_cast<double>(db) / 20.0);
}

//! One-pole smoothing coefficient for a time constant in milliseconds.
double smoothingCoefficient(float milliseconds, sample_rate_t sampleRate)
{
	const double seconds = std::max(0.0005, static_cast<double>(milliseconds) * 0.001);
	return std::exp(-1.0 / (seconds * static_cast<double>(sampleRate)));
}

//! Measured-correction passes after the first gain. Limiting removes energy the
//! first gain cannot predict, and how much a correction pass actually recovers
//! depends on how hard the limiter is working, so the chain re-measures and adds
//! part of the residual it still sees. Five passes converge on material whose
//! crest factor is high enough to need real limiting; the loop stops as soon as
//! it is inside the deadband.
constexpr int CorrectionPasses = 5;
//! Inside this many LU of the target the chain stops correcting.
constexpr float LoudnessDeadbandLu = 0.10f;
//! Fraction of the measured residual a correction pass asks for: a gain applied
//! to a signal already sitting on the limiter's ceiling is partly absorbed.
constexpr float CorrectionDamping = 0.70f;
//! Total correction the loop may apply, either way. A target the ceiling cannot
//! carry must stop short and be reported as a warn; without this bound the loop
//! would keep pushing gain into the limiter trying to close a residual that
//! cannot close.
constexpr float MaxCorrectionDb = 12.0f;
//! A step smaller than this is not worth another limiter pass.
constexpr float MinCorrectionStepDb = 0.01f;

} // namespace

MasteringChain::MasteringChain(const MasteringChainSettings& settings) :
	m_settings(settings)
{
}

void MasteringChain::feedSignal(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	MasteringMetrics& out)
{
	LufsMeter meter(sampleRate, DEFAULT_CHANNELS);

	const auto step = static_cast<std::size_t>(std::max<sample_rate_t>(1, sampleRate / 10));
	for (std::size_t offset = 0; offset < frames.size(); offset += step)
	{
		const auto count = std::min(step, frames.size() - offset);
		meter.processBlock(frames.data() + offset, count);
		out.shortTermMaxLufs = std::max(out.shortTermMaxLufs, meter.shortTermLufs());
	}

	const LufsMeter::Reading reading = meter.read();
	out.integratedLufs = reading.integratedLufs;
	out.truePeakDbtp = reading.truePeakDbtp;
	if (std::isfinite(out.integratedLufs) && std::isfinite(out.truePeakDbtp))
	{
		out.crestFactorDb = out.truePeakDbtp - out.integratedLufs;
	}
}

MasteringMetrics MasteringChain::measure(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate)
{
	MasteringMetrics metrics;
	feedSignal(frames, sampleRate, metrics);
	return metrics;
}

float MasteringChain::loudnessGainDb(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	float targetLufs)
{
	const MasteringMetrics metrics = measure(frames, sampleRate);
	if (!std::isfinite(metrics.integratedLufs))
	{
		// No gated block: silence, or less than a 400 ms block of signal. A gain
		// derived from a non-measurement would be a guess, so the chain adds none.
		return 0.0f;
	}
	return targetLufs - metrics.integratedLufs;
}

double MasteringChain::staticReductionDb(double overDb, double ratio, double kneeDb)
{
	if (ratio <= 1.0)
	{
		return 0.0;
	}
	const double slope = 1.0 - 1.0 / ratio;
	if (kneeDb > 0.0 && overDb > -(kneeDb / 2.0) && overDb < kneeDb / 2.0)
	{
		// Quadratic soft knee: 0 dB of reduction at the bottom edge, the full
		// ratio's reduction at the top edge, continuous with both sides.
		const double x = overDb + kneeDb / 2.0;
		return slope * x * x / (2.0 * kneeDb);
	}
	return overDb > 0.0 ? slope * overDb : 0.0;
}

void MasteringChain::applyDynamics(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	const MasteringChainSettings& settings)
{
	const double attack = smoothingCoefficient(settings.dynamicsAttackMs, sampleRate);
	const double release = smoothingCoefficient(settings.dynamicsReleaseMs, sampleRate);
	double reductionDb = 0.0;

	for (auto& frame : frames)
	{
		const double peak = std::max(std::fabs(frame.left()), std::fabs(frame.right()));
		const double wanted = staticReductionDb(linearToDb(peak) - settings.dynamicsThresholdDb,
			settings.dynamicsRatio, settings.dynamicsKneeDb);
		// More reduction than before is the attack side, less is the release side.
		const double coefficient = wanted > reductionDb ? attack : release;
		reductionDb = coefficient * reductionDb + (1.0 - coefficient) * wanted;

		const auto gain = static_cast<float>(dbToLinear(static_cast<float>(-reductionDb)));
		frame.setLeft(frame.left() * gain);
		frame.setRight(frame.right() * gain);
	}
}

void MasteringChain::applyLimiter(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	float ceilingLinear)
{
	const double release = smoothingCoefficient(static_cast<float>(LimiterReleaseSeconds * 1000.0), sampleRate);
	double envelope = 1.0;

	for (auto& frame : frames)
	{
		const double peak = std::max(std::fabs(frame.left()), std::fabs(frame.right()));
		const double wanted = peak > ceilingLinear ? ceilingLinear / peak : 1.0;
		// Instant attack (never above the ceiling), 50 ms release back to unity.
		envelope = wanted < envelope ? wanted : release * envelope + (1.0 - release);

		const auto gain = static_cast<float>(envelope);
		frame.setLeft(frame.left() * gain);
		frame.setRight(frame.right() * gain);
	}
}

void MasteringChain::trimTruePeak(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	float ceilingDbtp)
{
	const float truePeak = measure(frames, sampleRate).truePeakDbtp;
	if (std::isfinite(truePeak) && truePeak > ceilingDbtp)
	{
		applyGainDb(frames, ceilingDbtp - truePeak);
	}
}

void MasteringChain::limitAndTrim(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
	float ceilingLinear)
{
	applyLimiter(frames, sampleRate, ceilingLinear);
	trimTruePeak(frames, sampleRate, linearToDb(ceilingLinear));
}

void MasteringChain::process(std::vector<SampleFrame>& frames, sample_rate_t sampleRate) const
{
	if (frames.empty())
	{
		return;
	}

	applyGainDb(frames, loudnessGainDb(frames, sampleRate, m_settings.targetLufs));

	if (m_settings.dynamicsEnabled)
	{
		applyDynamics(frames, sampleRate, m_settings);
		applyGainDb(frames, loudnessGainDb(frames, sampleRate, m_settings.targetLufs));
	}

	// Bounded measured correction. Every pass ends with the limiter and the
	// true-peak trim, so the ceiling holds whatever the last gain did, and the
	// gain applied is only part of the residual it saw (CorrectionDamping):
	// a signal already on the ceiling absorbs part of any gain.
	float correctionDb = 0.0f;
	const float ceiling = static_cast<float>(dbToLinear(m_settings.ceilingDbtp));
	for (int pass = 0; pass <= CorrectionPasses; ++pass)
	{
		limitAndTrim(frames, sampleRate, ceiling);

		const float residual = loudnessGainDb(frames, sampleRate, m_settings.targetLufs);
		const bool lastPass = pass == CorrectionPasses;
		if (lastPass || !std::isfinite(residual) || std::fabs(residual) <= LoudnessDeadbandLu)
		{
			break;
		}
		const float step = std::clamp(residual * CorrectionDamping,
			-MaxCorrectionDb - correctionDb, MaxCorrectionDb - correctionDb);
		if (std::fabs(step) < MinCorrectionStepDb)
		{
			break;
		}
		correctionDb += step;
		applyGainDb(frames, step);
	}
}

} // namespace lmms
