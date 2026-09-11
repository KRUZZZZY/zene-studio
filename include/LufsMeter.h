/*
 * LufsMeter.h - ITU-R BS.1770-4 / EBU R128 loudness and true-peak measurement
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

#ifndef LMMS_LUFS_METER_H
#define LMMS_LUFS_METER_H

#include <array>
#include <cstdint>
#include <limits>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * Loudness (LUFS) and true-peak (dBTP) meter: ITU-R BS.1770-4 / EBU R128.
 *
 * What is measured, and where each piece comes from:
 *  - K-weighting: the two published biquads of Recommendation ITU-R BS.1770-4
 *    Annex 1 - a high-shelf pre-filter and the RLB high-pass - applied to every
 *    channel. The recommendation publishes the coefficient rows for 48 kHz only
 *    and asks implementations at other rates for the same frequency response, so
 *    designBiquads() derives the two sections from the equivalent analogue
 *    prototype parameters (see LufsMeter.cpp): at 48 kHz that derivation
 *    reproduces the published rows, which the unit test asserts.
 *  - 400 ms loudness blocks with a 100 ms hop (75 % overlap). A block's
 *    loudness is -0.691 + 10*log10(sum_i G_i * z_i), where z_i is the mean
 *    square of the K-weighted channel i over the block and G_i the channel
 *    weight of Annex 1 Table 3 (1.0 for L/R/C, 1.41 for Ls/Rs; the LFE channel
 *    of a 5.1 layout is not measured).
 *  - Gated integrated loudness (LUFS-I): blocks below the -70 LUFS absolute
 *    gate are dropped; the surviving blocks are gated again 10 LU below their
 *    own mean; the mean square of what is left gives LUFS-I.
 *  - Momentary LUFS-M (400 ms window) and short-term LUFS-S (3 s window),
 *    ungated, as EBU Tech 3341 defines them.
 *  - True peak in dBTP: 4 x over-sampling with the published order-48, 4-phase
 *    FIR interpolation filter of BS.1770-4 Annex 2. The maximum is taken over
 *    the four interpolated phases and the un-interpolated samples, so the meter
 *    can never read below the peak-sample value.
 *
 * Realtime safety (program rule, AGENTS.md): every buffer is fixed-size and
 * owned by the object. processBlock()/processPlanar() allocate nothing, lock
 * nothing and grow nothing; they are safe to call from an audio thread. The
 * getters only read, so any thread may poll them while blocks are being fed -
 * a poll can land between two blocks, which is exactly what a meter wants.
 *
 * The meter is opt-in: nothing in the default mixer or render path constructs
 * or feeds one, so adding it changes no existing render.
 *
 * Usage (per block, from whatever produces audio; then poll from the UI):
 * @code
 *   LufsMeter meter(engine->baseSampleRate(), DEFAULT_CHANNELS);
 *   // audio thread:
 *   meter.processBlock(buffer.data(), buffer.size());
 *   // anywhere:
 *   const auto reading = meter.read();
 * @endcode
 */
class LMMS_EXPORT LufsMeter
{
public:
	//! BS.1770-4 Table 3 channel layouts: mono, stereo, and 5.1 (L R C LFE Ls Rs).
	static constexpr int MaxChannels = 6;

	//! Returned by every loudness/peak getter while it has no value yet.
	static constexpr float MinusInfinity = -std::numeric_limits<float>::infinity();

	//! Normalised biquad section: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2].
	struct Biquad
	{
		double b0 = 1.0;
		double b1 = 0.0;
		double b2 = 0.0;
		double a1 = 0.0;
		double a2 = 0.0;
	};

	//! A consistent snapshot of everything the meter publishes.
	struct Reading
	{
		//! Gated integrated loudness of everything processed since reset().
		float integratedLufs = MinusInfinity;
		//! Loudness of the last 400 ms.
		float momentaryLufs = MinusInfinity;
		//! Loudness of the last 3 s.
		float shortTermLufs = MinusInfinity;
		//! Maximum true peak since reset(), in dB relative to full scale.
		float truePeakDbtp = MinusInfinity;
	};

	/**
	 * @param sampleRate   Rate of the blocks that will be fed, in Hz.
	 * @param channelCount Channels per frame: 1 (mono), 2 (stereo) or 6 (5.1).
	 *                     Clamped to [1, MaxChannels]; the weight table only
	 *                     distinguishes those layouts, anything between them is
	 *                     measured with every channel weighted 1.0.
	 */
	explicit LufsMeter(sample_rate_t sampleRate = 48000, ch_cnt_t channelCount = DEFAULT_CHANNELS);

	//! Drops every measurement and filter state; the meter is as if just built.
	void reset();

	sample_rate_t sampleRate() const { return m_sampleRate; }
	ch_cnt_t channelCount() const { return m_channelCount; }

	/**
	 * Feeds one block of interleaved stereo frames - the shape a SampleBuffer or
	 * a mixer channel hands out. The meter measures the first two channels.
	 * Allocates nothing; safe on an audio thread.
	 */
	void processBlock(const SampleFrame* frames, f_cnt_t frameCount);

	/**
	 * Feeds one block of planar channel buffers (channels[ch][frame]).
	 * channelCount must equal channelCount(); a mismatch is ignored so a caller
	 * cannot make the meter read past the end of its own storage.
	 * Allocates nothing; safe on an audio thread.
	 */
	void processPlanar(const sample_t* const* channels, ch_cnt_t channelCount, f_cnt_t frameCount);

	//! Snapshot of all four values.
	Reading read() const;
	//! Gated integrated loudness (LUFS-I); MinusInfinity until a block passes the absolute gate.
	float integratedLufs() const;
	//! Momentary loudness (LUFS-M, last 400 ms); MinusInfinity until 400 ms have been fed.
	float momentaryLufs() const;
	//! Short-term loudness (LUFS-S, last 3 s); MinusInfinity until 3 s have been fed.
	float shortTermLufs() const;
	//! Maximum true peak (dBTP) since reset(); MinusInfinity until a block was fed.
	float truePeakDbtp() const;

	/**
	 * K-weighting filter coefficients for \p sampleRate: the pre-filter (stage 1
	 * high shelf) and the RLB high-pass (stage 2). At 48000 Hz the two rows are
	 * the published BS.1770-4 Table 1 values (15 digits, asserted by the test);
	 * other rates are derived from the same equivalent analogue prototype
	 * parameters by bilinear transform, which is what the recommendation asks
	 * for when it says other rates must reproduce the same frequency response.
	 * Exposed for tests and for components that want to reproduce the weighting.
	 */
	static void kWeightingCoefficients(sample_rate_t sampleRate, Biquad& preFilter, Biquad& rlbHighPass);

private:
	//! 100 ms sub-blocks: a 400 ms block is 4 of them, the short-term window 30.
	static constexpr int SubBlocksPerSecond = 10;
	static constexpr int MomentarySubBlocks = 4;
	static constexpr int ShortTermSubBlocks = 30;
	//! BS.1770-4 Annex 1: absolute gate, relative gate and the log offset.
	static constexpr float AbsoluteGateLufs = -70.0f;
	static constexpr float RelativeGateLu = -10.0f;
	static constexpr float LoudnessOffset = -0.691f;
	//! Gating-block histogram: 0.05 LU bins from the absolute gate up to +5 LUFS.
	static constexpr float BinWidthLu = 0.05f;
	static constexpr int BinCount = 1501;
	//! BS.1770-4 Annex 2 example true-peak interpolator: 4 phases x 12 taps.
	static constexpr int TruePeakPhases = 4;
	static constexpr int TruePeakTaps = 12;

	struct FilterState
	{
		double preX1 = 0.0, preX2 = 0.0, preY1 = 0.0, preY2 = 0.0;
		double rlbX1 = 0.0, rlbX2 = 0.0, rlbY1 = 0.0, rlbY2 = 0.0;
	};

	struct TruePeakState
	{
		std::array<float, TruePeakTaps> line{};
		int writeIndex = 0;
		double peak = 0.0;
	};

	struct GatingBin
	{
		double energy = 0.0;
		std::uint32_t count = 0;
	};

	void setSampleRate(sample_rate_t sampleRate);
	void setChannelCount(ch_cnt_t channelCount);
	void designBiquads(sample_rate_t sampleRate);

	//! K-weights one sample, accumulates its energy and updates the true peak.
	void accumulateSample(ch_cnt_t channel, sample_t value);
	//! Counts a finished frame; closes the 100 ms sub-block when it is due.
	void endOfFrame();
	void updateTruePeak(ch_cnt_t channel, sample_t value);
	//! Closes a 100 ms sub-block: shifts the ring and adds a gating block.
	void closeSubBlock();
	//! Mean square of the window of the last \p blocks sub-blocks; negative when
	//! the window is not full yet.
	double windowMeanSquare(int blocks) const;
	float windowLoudness(int blocks) const;
	void addGatingBlock();
	static float loudnessFromEnergy(double energy);
	static double channelWeight(int channel, int channelCount);
	int binIndex(float lufs) const;
	float binLoudness(int index) const;
	float gatedLoudness() const;

	//! +6 dB is 1.41^2; the weight is applied squared on the energy domain.
	static constexpr double SurroundWeight = 1.41;

	sample_rate_t m_sampleRate = 48000;
	ch_cnt_t m_channelCount = 2;
	int m_subBlockFrames = 4800;
	int m_framesInSubBlock = 0;

	Biquad m_preFilter;
	Biquad m_rlbHighPass;
	std::array<FilterState, MaxChannels> m_filterState{};
	std::array<TruePeakState, MaxChannels> m_truePeak{};

	std::array<double, MaxChannels> m_pendingSum{};
	//! Ring of the last 30 sub-block sums, one channel row short of a full matrix.
	std::array<double, ShortTermSubBlocks * MaxChannels> m_ring{};
	int m_ringWrite = 0;
	int m_subBlocksClosed = 0;

	std::array<GatingBin, BinCount> m_gating;
};

} // namespace lmms

#endif // LMMS_LUFS_METER_H
