/*
 * MasteringChain.h - offline mastering chain and BS.1770-4 measurement
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

#ifndef LMMS_MASTERING_CHAIN_H
#define LMMS_MASTERING_CHAIN_H

#include <limits>
#include <vector>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * Objective readings of one programme signal, all from the BS.1770-4 meter
 * (include/LufsMeter.h). No perceptual claim is attached to any of them.
 */
struct MasteringMetrics
{
	//! Gated integrated loudness (LUFS-I).
	float integratedLufs = -std::numeric_limits<float>::infinity();
	//! The loudest 3 s short-term window in the signal (LUFS-S), as EBU Tech 3341 defines it.
	float shortTermMaxLufs = -std::numeric_limits<float>::infinity();
	//! Maximum true peak, 4x over-sampled (dBTP).
	float truePeakDbtp = -std::numeric_limits<float>::infinity();
	//! truePeakDbtp - integratedLufs; a derived diagnostic, not a standard measure.
	float crestFactorDb = -std::numeric_limits<float>::infinity();
};

/**
 * Settings of the wave-1 mastering chain. Every field is explicit: nothing here
 * is inferred from the source material, so two settings that differ do so on
 * purpose and the measurement can be checked against them.
 */
struct MasteringChainSettings
{
	//! Integrated loudness the chain aims for (LUFS-I). -14.0 streaming, -23.0 EBU R128.
	float targetLufs = -14.0f;
	//! True-peak ceiling in dBTP. -1.0 is what EBU R128 and the streaming
	//! services' delivery guidance both publish.
	float ceilingDbtp = -1.0f;

	//! Insert the glue-compressor stage between the two loudness gains.
	bool dynamicsEnabled = false;
	//! Compressor static curve, in dBFS of the level-normalised signal: the
	//! makeup gain runs first, so the threshold is relative to the programme's
	//! own loudness rather than to an absolute level the material may never reach.
	float dynamicsThresholdDb = -20.0f;
	float dynamicsRatio = 2.0f;
	float dynamicsAttackMs = 10.0f;
	float dynamicsReleaseMs = 150.0f;
	float dynamicsKneeDb = 6.0f;
};

/**
 * The wave-1 mastering chain: measured loudness gain, an optional glue
 * compressor, a peak limiter and a measured true-peak trim.
 *
 * It is an OFFLINE processor. process() allocates (it takes a signal-sized
 * copy through the meter) and does floating-point work proportional to the
 * signal, so it runs on the CLI/test thread that drives the job, never on the
 * audio callback and never inside a render.
 *
 * Order of operations, and why:
 *  1. gain to target, measured on the signal itself (not predicted from its peak);
 *  2. optional dynamics, then a second measured gain to the same target, so a
 *     dynamics candidate differs from its sibling in how it reaches the target,
 *     not in where it lands;
 *  3. a sample-domain peak limiter at the ceiling, then a true-peak trim:
 *     inter-sample peaks survive the limiter, so the ceiling is enforced against
 *     the *measured* true peak. The 4x interpolator is linear, so one measured
 *     trim is exact;
 *  4. a bounded measured-correction loop: limiting removes energy the first gain
 *     cannot predict, so the chain re-measures and adds the residual it still
 *     sees (damped, a fixed number of passes), always re-limiting afterwards so
 *     the ceiling holds after the last operation. Where the target is not
 *     reachable at that ceiling on that material, the loop stops short and the
 *     residual is what the report shows.
 */
class LMMS_EXPORT MasteringChain
{
public:
	explicit MasteringChain(const MasteringChainSettings& settings);

	//! Runs the chain over interleaved stereo frames, in place. Offline only.
	void process(std::vector<SampleFrame>& frames, sample_rate_t sampleRate) const;

	//! BS.1770-4 readings for one signal: LUFS-I, loudest LUFS-S, dBTP, crest.
	static MasteringMetrics measure(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate);

	//! Gain in dB that moves the signal's measured LUFS-I to \p targetLufs;
	//! 0 dB when the signal has no gated block (silence, or shorter than 400 ms).
	static float loudnessGainDb(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		float targetLufs);

	const MasteringChainSettings& settings() const { return m_settings; }

private:
	//! Feed the whole signal through one meter; fills every field it can report.
	static void feedSignal(const std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		MasteringMetrics& out);
	//! Static compressor curve: gain reduction in dB for \p overDb above threshold.
	static double staticReductionDb(double overDb, double ratio, double kneeDb);
	//! Stereo-linked feed-forward glue compressor, instantiated by settings 4.
	static void applyDynamics(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		const MasteringChainSettings& settings);
	//! Peak limiter: instant attack, 50 ms release, sample-domain ceiling.
	static void applyLimiter(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		float ceilingLinear);
	//! Limiter followed by the measured true-peak trim - the pair that leaves the
	//! signal both sample-limited and below the ceiling it is measured at.
	static void limitAndTrim(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		float ceilingLinear);
	//! Measured gain that brings the true peak to \p ceilingDbtp; no-op below it.
	static void trimTruePeak(std::vector<SampleFrame>& frames, sample_rate_t sampleRate,
		float ceilingDbtp);

	MasteringChainSettings m_settings;
};

} // namespace lmms

#endif // LMMS_MASTERING_CHAIN_H
