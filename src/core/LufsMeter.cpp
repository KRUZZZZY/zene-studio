/*
 * LufsMeter.cpp - ITU-R BS.1770-4 / EBU R128 loudness and true-peak measurement
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

#include "LufsMeter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lmms
{

namespace
{

//! pi, so this file does not depend on a platform's M_PI macro.
constexpr double Pi = 3.14159265358979323846;

//! Recommendation ITU-R BS.1770-4, Annex 2 ("Guidelines for accurate
//! measurement of true-peak level"): the published order-48, 4-phase FIR
//! interpolation filter, 12 taps per phase, values exactly as printed there.
//! Its per-phase DC gains are 1.0016 (phases 0 and 3) and 0.9730 (phases 1 and
//! 2), i.e. the recommendation's example filter carries ~0.02 dB of passband
//! ripple - the true-peak readings below are within the 0.2 dB the EBU
//! tolerance allows.
constexpr double TruePeakCoefficients[12][4] = {
	{ 0.0017089843750, -0.0291748046875, -0.0189208984375, -0.0083007812500 },
	{ 0.0109863281250, 0.0292968750000, 0.0330810546875, 0.0148925781250 },
	{ -0.0196533203125, -0.0517578125000, -0.0582275390625, -0.0266113281250 },
	{ 0.0332031250000, 0.0891113281250, 0.1015625000000, 0.0476074218750 },
	{ -0.0594482421875, -0.1665039062500, -0.2003173828125, -0.1022949218750 },
	{ 0.1373291015625, 0.4650878906250, 0.7797851562500, 0.9721679687500 },
	{ 0.9721679687500, 0.7797851562500, 0.4650878906250, 0.1373291015625 },
	{ -0.1022949218750, -0.2003173828125, -0.1665039062500, -0.0594482421875 },
	{ 0.0476074218750, 0.1015625000000, 0.0891113281250, 0.0332031250000 },
	{ -0.0266113281250, -0.0582275390625, -0.0517578125000, -0.0196533203125 },
	{ 0.0148925781250, 0.0330810546875, 0.0292968750000, 0.0109863281250 },
	{ -0.0083007812500, -0.0189208984375, -0.0291748046875, 0.0017089843750 },
};

//! BS.1770-4 Annex 1, stage 1 (high-shelf pre-filter) analogue prototype.
constexpr double PreFilterFrequencyHz = 1681.974450955533;
constexpr double PreFilterGainDb = 3.999843853973347;
constexpr double PreFilterQ = 0.7071752369554196;
//! BS.1770-4 Annex 1, stage 2 (RLB high-pass) analogue prototype.
constexpr double RlbFrequencyHz = 38.13547087602444;
constexpr double RlbQ = 0.5003270373238773;
//! The exponent in the pre-filter's Vb term, as published in Annex 1.
constexpr double PreFilterVbExponent = 0.4996667741545416;

//! Bin 0 of the gating histogram collects everything at or below the absolute
//! gate; it is never counted by the gated means.
constexpr int BelowGateBin = 0;

} // namespace

LufsMeter::LufsMeter(sample_rate_t sampleRate, ch_cnt_t channelCount)
{
	setChannelCount(channelCount);
	setSampleRate(sampleRate);
}

void LufsMeter::setChannelCount(ch_cnt_t channelCount)
{
	m_channelCount = static_cast<ch_cnt_t>(std::clamp<int>(channelCount, 1, MaxChannels));
}

void LufsMeter::setSampleRate(sample_rate_t sampleRate)
{
	m_sampleRate = sampleRate;
	// 100 ms sub-blocks; a sample rate below 10 Hz would give 0 frames.
	m_subBlockFrames = std::max<int>(static_cast<int>(sampleRate) / SubBlocksPerSecond, 1);
	m_framesInSubBlock = 0;
	kWeightingCoefficients(sampleRate, m_preFilter, m_rlbHighPass);
}

void LufsMeter::reset()
{
	m_filterState = {};
	m_truePeak = {};
	m_pendingSum = {};
	m_ring = {};
	m_ringWrite = 0;
	m_subBlocksClosed = 0;
	m_framesInSubBlock = 0;
	m_gating = {};
}

void LufsMeter::kWeightingCoefficients(sample_rate_t sampleRate, Biquad& preFilter, Biquad& rlbHighPass)
{
	const double fs = std::max<double>(sampleRate, 1.0);

	// Stage 1: high-shelf pre-filter. The Annex 1 prototype is the analogue
	// shelf whose bilinear transform at 48 kHz reproduces the published table
	// [1.53512485958697, -2.69169618940638, 1.19839281085285] /
	// [1, -1.69065929318241, 0.73248077421585] (asserted by LufsMeterTest).
	const double shelfK = std::tan(Pi * PreFilterFrequencyHz / fs);
	const double vh = std::pow(10.0, PreFilterGainDb / 20.0);
	const double vb = std::pow(vh, PreFilterVbExponent);
	const double shelfA0 = 1.0 + shelfK / PreFilterQ + shelfK * shelfK;
	preFilter.b0 = (vh + vb * shelfK / PreFilterQ + shelfK * shelfK) / shelfA0;
	preFilter.b1 = 2.0 * (shelfK * shelfK - vh) / shelfA0;
	preFilter.b2 = (vh - vb * shelfK / PreFilterQ + shelfK * shelfK) / shelfA0;
	preFilter.a1 = 2.0 * (shelfK * shelfK - 1.0) / shelfA0;
	preFilter.a2 = (1.0 - shelfK / PreFilterQ + shelfK * shelfK) / shelfA0;

	// Stage 2: RLB high-pass. At 48 kHz this reproduces the published table
	// [1, -2, 1] / [1, -1.99004745483398, 0.99007225036621].
	const double highPassK = std::tan(Pi * RlbFrequencyHz / fs);
	const double highPassA0 = 1.0 + highPassK / RlbQ + highPassK * highPassK;
	rlbHighPass.b0 = 1.0;
	rlbHighPass.b1 = -2.0;
	rlbHighPass.b2 = 1.0;
	rlbHighPass.a1 = 2.0 * (highPassK * highPassK - 1.0) / highPassA0;
	rlbHighPass.a2 = (1.0 - highPassK / RlbQ + highPassK * highPassK) / highPassA0;
}

void LufsMeter::processBlock(const SampleFrame* frames, f_cnt_t frameCount)
{
	// SampleFrame is stereo, so this entry point only serves mono and stereo
	// meters; a 5.1 meter has to be fed through processPlanar().
	if (frames == nullptr || m_channelCount > 2) { return; }

	for (f_cnt_t frame = 0; frame < frameCount; ++frame)
	{
		const sample_t* data = frames[frame].data();
		accumulateSample(0, data[0]);
		if (m_channelCount > 1) { accumulateSample(1, data[1]); }
		endOfFrame();
	}
}

void LufsMeter::processPlanar(const sample_t* const* channels, ch_cnt_t channelCount, f_cnt_t frameCount)
{
	if (channels == nullptr || channelCount != m_channelCount) { return; }
	for (ch_cnt_t channel = 0; channel < channelCount; ++channel)
	{
		if (channels[channel] == nullptr) { return; }
	}

	for (f_cnt_t frame = 0; frame < frameCount; ++frame)
	{
		for (ch_cnt_t channel = 0; channel < channelCount; ++channel)
		{
			accumulateSample(channel, channels[channel][frame]);
		}
		endOfFrame();
	}
}

void LufsMeter::accumulateSample(ch_cnt_t channel, sample_t value)
{
	FilterState& state = m_filterState[channel];
	const double x = static_cast<double>(value);

	// Stage 1 (Direct Form I), then stage 2. Double precision state: the
	// published coefficients carry 15 significant digits.
	const double stage1 = m_preFilter.b0 * x + m_preFilter.b1 * state.preX1 + m_preFilter.b2 * state.preX2
		- m_preFilter.a1 * state.preY1 - m_preFilter.a2 * state.preY2;
	state.preX2 = state.preX1;
	state.preX1 = x;
	state.preY2 = state.preY1;
	state.preY1 = stage1;

	const double weighted = m_rlbHighPass.b0 * stage1 + m_rlbHighPass.b1 * state.rlbX1 + m_rlbHighPass.b2 * state.rlbX2
		- m_rlbHighPass.a1 * state.rlbY1 - m_rlbHighPass.a2 * state.rlbY2;
	state.rlbX2 = state.rlbX1;
	state.rlbX1 = stage1;
	state.rlbY2 = state.rlbY1;
	state.rlbY1 = weighted;

	m_pendingSum[channel] += weighted * weighted;
	updateTruePeak(channel, value);
}

void LufsMeter::endOfFrame()
{
	if (++m_framesInSubBlock < m_subBlockFrames) { return; }
	closeSubBlock();
	m_framesInSubBlock = 0;
}

void LufsMeter::closeSubBlock()
{
	for (ch_cnt_t channel = 0; channel < m_channelCount; ++channel)
	{
		m_ring[m_ringWrite * MaxChannels + channel] = m_pendingSum[channel];
		m_pendingSum[channel] = 0.0;
	}
	m_ringWrite = (m_ringWrite + 1) % ShortTermSubBlocks;
	m_subBlocksClosed = std::min(m_subBlocksClosed + 1, ShortTermSubBlocks);
	addGatingBlock();
}

void LufsMeter::addGatingBlock()
{
	const double meanSquare = windowMeanSquare(MomentarySubBlocks);
	if (meanSquare < 0.0) { return; }

	const int index = binIndex(loudnessFromEnergy(meanSquare));
	m_gating[index].energy += meanSquare;
	++m_gating[index].count;
}

double LufsMeter::windowMeanSquare(int blocks) const
{
	if (m_subBlocksClosed < blocks) { return -1.0; }

	double total = 0.0;
	for (int back = 0; back < blocks; ++back)
	{
		const int slot = (m_ringWrite - 1 - back + 2 * ShortTermSubBlocks) % ShortTermSubBlocks;
		for (ch_cnt_t channel = 0; channel < m_channelCount; ++channel)
		{
			total += channelWeight(channel, m_channelCount) * m_ring[slot * MaxChannels + channel];
		}
	}
	return total / (static_cast<double>(blocks) * static_cast<double>(m_subBlockFrames));
}

float LufsMeter::windowLoudness(int blocks) const
{
	const double meanSquare = windowMeanSquare(blocks);
	return meanSquare < 0.0 ? MinusInfinity : loudnessFromEnergy(meanSquare);
}

float LufsMeter::loudnessFromEnergy(double energy)
{
	// NaN and the all-zero (digital silence) case both land on the sentinel.
	if (!(energy > 0.0)) { return MinusInfinity; }
	return static_cast<float>(LoudnessOffset + 10.0 * std::log10(energy));
}

double LufsMeter::channelWeight(int channel, int channelCount)
{
	// Annex 1 Table 3, for the layouts the recommendation defines: L, R and C
	// are weighted 1.0 and the surrounds 1.41; the LFE channel of a 5.1 layout
	// is not part of the measurement at all. Any other channel count is
	// measured with every channel at 1.0 (mono and stereo are exact).
	if (channelCount != 6) { return 1.0; }
	if (channel == 3) { return 0.0; }
	return channel >= 4 ? SurroundWeight : 1.0;
}

int LufsMeter::binIndex(float lufs) const
{
	// The absolute gate is applied exactly here; what is left is quantised into
	// bins of BinWidthLu. Bin 0 holds everything at or below the gate (and -inf).
	if (!(lufs > AbsoluteGateLufs)) { return BelowGateBin; }
	const int index = 1 + static_cast<int>((lufs - AbsoluteGateLufs) / BinWidthLu);
	return std::min(index, BinCount - 1);
}

float LufsMeter::binLoudness(int index) const
{
	// Bin 0 is not on the scale; every other bin stands for the blocks whose
	// loudness is within half a bin of its centre.
	return AbsoluteGateLufs + (static_cast<float>(index) - 0.5f) * BinWidthLu;
}

float LufsMeter::gatedLoudness() const
{
	// Absolute gate: every bin above bin 0 (binIndex() already applied the
	// gate exactly when the block was added).
	double energy = 0.0;
	std::uint64_t count = 0;
	for (int index = 1; index < BinCount; ++index)
	{
		energy += m_gating[index].energy;
		count += m_gating[index].count;
	}
	if (count == 0) { return MinusInfinity; }

	// Relative gate: 10 LU below the ungated mean of the surviving blocks.
	const float threshold = loudnessFromEnergy(energy / static_cast<double>(count)) + RelativeGateLu;
	double gatedEnergy = 0.0;
	std::uint64_t gatedCount = 0;
	for (int index = 1; index < BinCount; ++index)
	{
		if (binLoudness(index) <= threshold) { continue; }
		gatedEnergy += m_gating[index].energy;
		gatedCount += m_gating[index].count;
	}
	if (gatedCount == 0) { return MinusInfinity; }
	return loudnessFromEnergy(gatedEnergy / static_cast<double>(gatedCount));
}

float LufsMeter::integratedLufs() const
{
	return gatedLoudness();
}

float LufsMeter::momentaryLufs() const
{
	return windowLoudness(MomentarySubBlocks);
}

float LufsMeter::shortTermLufs() const
{
	return windowLoudness(ShortTermSubBlocks);
}

float LufsMeter::truePeakDbtp() const
{
	double peak = 0.0;
	for (ch_cnt_t channel = 0; channel < m_channelCount; ++channel)
	{
		peak = std::max(peak, m_truePeak[channel].peak);
	}
	return peak > 0.0 ? static_cast<float>(20.0 * std::log10(peak)) : MinusInfinity;
}

LufsMeter::Reading LufsMeter::read() const
{
	Reading reading;
	reading.integratedLufs = integratedLufs();
	reading.momentaryLufs = momentaryLufs();
	reading.shortTermLufs = shortTermLufs();
	reading.truePeakDbtp = truePeakDbtp();
	return reading;
}

void LufsMeter::updateTruePeak(ch_cnt_t channel, sample_t value)
{
	static_assert(std::size(TruePeakCoefficients) == TruePeakTaps, "true-peak table is not 12 taps per phase");

	TruePeakState& state = m_truePeak[channel];
	state.line[state.writeIndex] = value;

	// The un-interpolated sample is part of the maximum, so the meter can never
	// under-read a peak-sample meter.
	double peak = std::fabs(static_cast<double>(value));
	for (int phase = 0; phase < TruePeakPhases; ++phase)
	{
		double interpolated = 0.0;
		for (int tap = 0; tap < TruePeakTaps; ++tap)
		{
			// state.writeIndex holds x[n]; tap k wants x[n-k].
			interpolated += TruePeakCoefficients[tap][phase]
				* state.line[(state.writeIndex + TruePeakTaps - tap) % TruePeakTaps];
		}
		peak = std::max(peak, std::fabs(interpolated));
	}
	state.peak = std::max(state.peak, peak);
	state.writeIndex = (state.writeIndex + 1) % TruePeakTaps;
}

} // namespace lmms
