/*
 * PitchTrack.cpp - a monophonic line's notes, found in audio (YIN)
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
#include "PitchTrack.h"

#include <algorithm>
#include <cmath>

namespace lmms::pitchtrack
{

namespace
{

// At the analysis rate (about 11 kHz): a 93 ms window reaches down to the low E of a bass.
constexpr int AnalysisRate = 11025;
constexpr int Window = 1024;
constexpr int Hop = 256;
constexpr float Threshold = 0.15f;  // YIN's absolute threshold on the normalised difference

float rms(const float* mono, std::size_t count, std::size_t frame, int size)
{
	double sum = 0.0;
	const std::size_t end = std::min(count, frame + static_cast<std::size_t>(size));
	for (std::size_t i = frame; i < end; ++i) { sum += static_cast<double>(mono[i]) * mono[i]; }
	return end > frame ? static_cast<float>(std::sqrt(sum / static_cast<double>(end - frame))) : 0.f;
}

int keyOf(float hz)
{
	return static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
}

//! The first lag whose cumulative-mean-normalised difference dips under the threshold, refined; 0 for none.
float bestLag(const std::vector<float>& d, int maxLag)
{
	double running = 0.0;
	std::vector<float> cmnd(static_cast<std::size_t>(maxLag), 1.f);
	for (int tau = 1; tau < maxLag; ++tau)
	{
		running += d[static_cast<std::size_t>(tau)];
		cmnd[static_cast<std::size_t>(tau)] = running > 0.0 ? static_cast<float>(d[static_cast<std::size_t>(tau)] * tau / running) : 1.f;
	}
	for (int tau = 2; tau + 1 < maxLag; ++tau)
	{
		if (cmnd[static_cast<std::size_t>(tau)] >= Threshold) { continue; }
		while (tau + 1 < maxLag && cmnd[static_cast<std::size_t>(tau) + 1] < cmnd[static_cast<std::size_t>(tau)]) { ++tau; }
		const float a = cmnd[static_cast<std::size_t>(tau) - 1];
		const float b = cmnd[static_cast<std::size_t>(tau)];
		const float c = cmnd[static_cast<std::size_t>(tau) + 1];
		const float denominator = a - 2.f * b + c;
		return static_cast<float>(tau) + (std::fabs(denominator) > 1e-9f ? 0.5f * (a - c) / denominator : 0.f);
	}
	return 0.f;
}

} // namespace

float pitchAt(const float* mono, std::size_t count, std::size_t frame, int size, int rate)
{
	const int maxLag = size / 2;
	if (frame + static_cast<std::size_t>(size) > count || maxLag < 4) { return 0.f; }
	std::vector<float> d(static_cast<std::size_t>(maxLag), 0.f);
	for (int tau = 1; tau < maxLag; ++tau)
	{
		double sum = 0.0;
		for (int i = 0; i < maxLag; ++i)
		{
			const float delta = mono[frame + static_cast<std::size_t>(i)] - mono[frame + static_cast<std::size_t>(i + tau)];
			sum += static_cast<double>(delta) * delta;
		}
		d[static_cast<std::size_t>(tau)] = static_cast<float>(sum);
	}
	const float lag = bestLag(d, maxLag);
	return lag > 0.f ? static_cast<float>(rate) / lag : 0.f;
}

namespace
{

//! @a count frames averaged in groups of @a factor (a box low-pass, enough an octave below Nyquist).
std::vector<float> decimate(const float* mono, std::size_t count, int factor)
{
	std::vector<float> low(count / static_cast<std::size_t>(factor));
	for (std::size_t i = 0; i < low.size(); ++i)
	{
		float sum = 0.f;
		for (int k = 0; k < factor; ++k) { sum += mono[i * static_cast<std::size_t>(factor) + static_cast<std::size_t>(k)]; }
		low[i] = sum / static_cast<float>(factor);
	}
	return low;
}

//! Hop keys into notes: a note holds while its key stays, and is kept when it lasts long enough.
struct Segmenter
{
	f_cnt_t minFrames;
	std::vector<PitchNote> notes;
	PitchNote current;
	bool open = false;

	void close(f_cnt_t at)
	{
		if (open && at - current.start >= minFrames) { current.end = at; notes.push_back(current); }
		open = false;
	}

	void feed(int key, float level, f_cnt_t at)
	{
		if (open && key == current.key) { current.level = std::max(current.level, level); return; }
		close(at);
		if (key >= 0) { current = PitchNote{at, at, key, level}; open = true; }
	}
};

} // namespace

std::vector<PitchNote> detectNotes(const float* mono, std::size_t count, int rate, float gate, f_cnt_t minFrames)
{
	// Decimated first: the difference function is quadratic in the window, so this keeps a long clip fast.
	const int factor = std::max(1, rate / AnalysisRate);
	const std::vector<float> low = decimate(mono, count, factor);
	Segmenter segmenter{minFrames};
	for (std::size_t frame = 0; frame + Window <= low.size(); frame += Hop)
	{
		const float level = rms(low.data(), low.size(), frame, Window);
		const float hz = level > gate ? pitchAt(low.data(), low.size(), frame, Window, rate / factor) : 0.f;
		segmenter.feed(hz > 30.f && hz < 2000.f ? keyOf(hz) : -1, level, static_cast<f_cnt_t>(frame) * factor);
	}
	segmenter.close(static_cast<f_cnt_t>(count));
	return segmenter.notes;
}

} // namespace lmms::pitchtrack
