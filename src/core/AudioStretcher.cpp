/*
 * AudioStretcher.cpp - pitch-preserving time stretch (WSOLA).
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
 */

#include "AudioStretcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lmms
{

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;

/*! One source frame at a fractional position, linearly interpolated.
 *
 *  Fractional positions are the normal case, not an edge case: the analysis
 *  hop is `Hs * speed` and any speed that is not an integer ratio (or any
 *  source whose rate differs from the project's) lands between frames. The
 *  interpolation is the engine's default converter class - `Mode::Linear`,
 *  which is `AudioResampler`'s default and the converter every render used
 *  before this feature existed - so this path is never worse than the path it
 *  replaces, and never better than the sinc modes.
 *
 *  Outside the source it is silence, not a clamp: clamping would hold the last
 *  frame's DC for a whole grain at the end of a clip.
 */
SampleFrame readAt(const SampleFrame* src, f_cnt_t srcFrames, double position)
{
	if (position < 0.0) { return SampleFrame{}; }
	const auto index = static_cast<std::int64_t>(position);
	if (index >= static_cast<std::int64_t>(srcFrames)) { return SampleFrame{}; }

	const SampleFrame first = src[index];
	// The last frame has no successor to interpolate towards.
	if (index + 1 >= static_cast<std::int64_t>(srcFrames)) { return first; }

	const auto fraction = static_cast<sample_t>(position - static_cast<double>(index));
	const SampleFrame second = src[index + 1];
	return SampleFrame(first[0] + (second[0] - first[0]) * fraction,
		first[1] + (second[1] - first[1]) * fraction);
}

} // namespace

AudioStretcher::AudioStretcher()
{
	prepare();
}

void AudioStretcher::prepare(const Parameters& params)
{
	// Clamp into the fixed state rather than throwing: this can be called from
	// the audio thread (a play handle prepares its own stretcher), where an
	// exception has nowhere to go.
	int grain = std::clamp(params.grainFrames, 64, MaxGrainFrames);
	grain -= grain % 2;   // even, so the synthesis hop is an exact half
	const int radius = std::clamp(params.searchRadius, 0, MaxSearchRadius);

	// Already prepared for this shape: the window table is the expensive part
	// of prepare() and nothing about it changed.
	if (m_prepared && grain == m_grain && radius == m_params.searchRadius) { return; }

	m_params.grainFrames = grain;
	m_params.searchRadius = radius;
	m_grain = grain;
	m_hop = grain / 2;

	/*! Periodic Hann over the grain, `hs = grain / 2` hops: the window is
	 *  exactly constant-overlap-add at that hop (`w[i] + w[i + grain/2] = 1`
	 *  for the periodic form below), which is what makes a stretch of 1 a
	 *  rebuild of the waveform rather than a modulation of it. */
	for (int i = 0; i < m_grain; ++i)
	{
		m_window[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / m_grain));
	}

	m_prepared = true;
	reset();
}

void AudioStretcher::reset()
{
	m_accum.fill(SampleFrame{});
	m_hopOut.fill(SampleFrame{});
	m_hopReady = 0;
	m_hopRead = 0;
	m_sourcePos = 0.0;
	m_started = false;
}

void AudioStretcher::seek(double sourceFrame)
{
	m_sourcePos = std::isfinite(sourceFrame) ? sourceFrame : 0.0;
}

void AudioStretcher::produceHop(const SampleFrame* src, f_cnt_t srcFrames, double speed)
{
	const int carry = m_grain - m_hop;   // the region the next grain overlaps

	/*! ALIGNMENT. The nominal position is the cursor; the grain is allowed to
	 *  start up to ±searchRadius frames away from it, and the offset chosen is
	 *  the one whose first `carry` frames best continue what the previous
	 *  grains already wrote.
	 *
	 *  The score is the normalised cross-correlation
	 *  `sum(candidate * carry) / sqrt(sum(candidate^2))`. The carry region's
	 *  own energy is the same for every candidate, so it is a constant factor
	 *  and normalising by the candidate's energy alone is the same ranking
	 *  (and the reason a loud candidate cannot win on level alone).
	 *
	 *  The first hop has nothing written yet, so there is nothing to align to
	 *  and the grain starts exactly at the cursor.
	 */
	int bestOffset = 0;
	if (m_started && m_params.searchRadius > 0)
	{
		double bestScore = -std::numeric_limits<double>::infinity();
		for (int offset = -m_params.searchRadius; offset <= m_params.searchRadius; ++offset)
		{
			const double start = m_sourcePos + static_cast<double>(offset);
			double correlation = 0.0;
			double energy = 1e-12;   // never divide by zero on a silent candidate
			for (int i = 0; i < carry; ++i)
			{
				const SampleFrame candidate = readAt(src, srcFrames, start + i);
				const SampleFrame carryFrame = m_accum[i];
				correlation += static_cast<double>(candidate[0]) * carryFrame[0]
					+ static_cast<double>(candidate[1]) * carryFrame[1];
				energy += static_cast<double>(candidate[0]) * candidate[0]
					+ static_cast<double>(candidate[1]) * candidate[1];
			}
			const double score = correlation / std::sqrt(energy);
			if (score > bestScore)
			{
				bestScore = score;
				bestOffset = offset;
			}
		}
	}

	// Windowed overlap-add of the chosen grain onto the carry.
	const double grainStart = m_sourcePos + static_cast<double>(bestOffset);
	for (int i = 0; i < m_grain; ++i)
	{
		const SampleFrame frame = readAt(src, srcFrames, grainStart + i);
		const float window = m_window[i];
		m_accum[i][0] += frame[0] * window;
		m_accum[i][1] += frame[1] * window;
	}

	// The hop's frames are complete: they are the sum of the previous grain's
	// right half and this grain's left half.
	for (int i = 0; i < m_hop; ++i) { m_hopOut[i] = m_accum[i]; }
	m_hopReady = static_cast<f_cnt_t>(m_hop);
	m_hopRead = 0;

	// Carry the rest forward and clear the tail this grain contributed to.
	for (int i = 0; i < carry; ++i) { m_accum[i] = m_accum[i + m_hop]; }
	for (int i = carry; i < m_grain; ++i) { m_accum[i] = SampleFrame{}; }

	// The source advances by the synthesis hop scaled by the speed; only the
	// OUTPUT advances by the synthesis hop itself, and that is the whole trick.
	m_sourcePos += static_cast<double>(m_hop) * speed;
	m_started = true;
}

f_cnt_t AudioStretcher::process(const SampleFrame* src, f_cnt_t srcFrames, SampleFrame* dst,
	f_cnt_t dstFrames, double speed)
{
	if (!m_prepared || src == nullptr || dst == nullptr || dstFrames <= 0) { return 0; }
	if (!std::isfinite(speed) || speed <= 0.0) { speed = 1.0; }

	f_cnt_t written = 0;
	while (written < dstFrames)
	{
		if (m_hopRead >= m_hopReady) { produceHop(src, srcFrames, speed); }
		const f_cnt_t available = m_hopReady - m_hopRead;
		const f_cnt_t take = std::min(available, dstFrames - written);
		std::copy_n(m_hopOut.begin() + static_cast<std::ptrdiff_t>(m_hopRead),
			static_cast<std::size_t>(take), dst + written);
		m_hopRead += take;
		written += take;
	}
	return written;
}

} // namespace lmms
