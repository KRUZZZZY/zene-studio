/*
 * SampleOperators.cpp - the in-tree DSP behind the sample.* group (board #706)
 *
 * Copyright (2026) Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "SampleOperators.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lmms
{
namespace sampleops
{

namespace
{

constexpr double TwoPi = 6.283185307179586476925286766559;

//! xorshift32: one fixed, published integer generator per channel, so a tone
//! and two noise takes differ deterministically and a re-run reproduces the
//! same take (the seed is a constant, not a clock).
struct WhiteSource
{
	uint32_t state;

	float next()
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		// Map the high 24 bits around their midpoint onto [-1, 1].
		const auto word = static_cast<float>(state >> 8) - 8388608.0f;
		return word / 8388608.0f;
	}
};

//! Paul Kellet's published pink-noise filter (the widely reproduced economy
//! form): seven one-pole sections over white noise, re-issued here as our own
//! arithmetic rather than lifted from any GPLv3 tree.
struct PinkChannel
{
	WhiteSource white;
	float b0 = 0.f, b1 = 0.f, b2 = 0.f, b3 = 0.f, b4 = 0.f, b5 = 0.f, b6 = 0.f;

	float next()
	{
		const float w = white.next();
		b0 = 0.99886f * b0 + w * 0.0555179f;
		b1 = 0.99332f * b1 + w * 0.0750759f;
		b2 = 0.96900f * b2 + w * 0.1538520f;
		b3 = 0.86650f * b3 + w * 0.3104856f;
		b4 = 0.55000f * b4 + w * 0.5329522f;
		b5 = -0.7616f * b5 - w * 0.0168980f;
		const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
		b6 = w * 0.115926f;
		return pink * 0.11f;
	}
};

std::vector<SampleFrame> silence(f_cnt_t frames)
{
	return std::vector<SampleFrame>(static_cast<size_t>(frames), SampleFrame(0.f, 0.f));
}

std::vector<SampleFrame> tone(double frequencyHz, float amplitude, f_cnt_t frames, int sampleRate)
{
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	const double step = frequencyHz * TwoPi / static_cast<double>(sampleRate);
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		const auto value = static_cast<float>(amplitude * std::sin(step * static_cast<double>(i)));
		out.emplace_back(value, value);
	}
	return out;
}

std::vector<SampleFrame> white(float amplitude, f_cnt_t frames)
{
	WhiteSource left{0x2545F491u};
	WhiteSource right{0x9E3779B9u};
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		out.emplace_back(amplitude * left.next(), amplitude * right.next());
	}
	return out;
}

std::vector<SampleFrame> pink(float amplitude, f_cnt_t frames)
{
	PinkChannel left{{0x2545F491u}};
	PinkChannel right{{0x9E3779B9u}};
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		out.emplace_back(amplitude * left.next(), amplitude * right.next());
	}
	return out;
}

} // namespace

std::vector<SampleFrame> generate(Kind kind, double frequencyHz, float amplitude,
	f_cnt_t frames, int sampleRate)
{
	switch (kind)
	{
	case Kind::Tone:
		return tone(frequencyHz, amplitude, frames, sampleRate);
	case Kind::White:
		return white(amplitude, frames);
	case Kind::Pink:
		return pink(amplitude, frames);
	case Kind::Silence:
		break;
	}
	return silence(frames);
}

std::vector<SampleFrame> amplify(const SampleFrame* in, f_cnt_t frames, float gain)
{
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		out.emplace_back(in[i].left() * gain, in[i].right() * gain);
	}
	return out;
}

std::vector<SampleFrame> reverse(const SampleFrame* in, f_cnt_t frames)
{
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		out.push_back(in[frames - 1 - i]);
	}
	return out;
}

std::vector<SampleFrame> fade(const SampleFrame* in, f_cnt_t frames, bool fadeIn)
{
	std::vector<SampleFrame> out;
	out.reserve(static_cast<size_t>(frames));
	if (frames < 2)
	{
		out.assign(in, in + frames);
		return out;
	}
	const auto span = static_cast<float>(frames - 1);
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		const float pos = static_cast<float>(i) / span;
		const float gain = fadeIn ? pos : 1.0f - pos;
		out.emplace_back(in[i].left() * gain, in[i].right() * gain);
	}
	return out;
}

float peakOf(const SampleFrame* in, f_cnt_t frames)
{
	float peak = 0.0f;
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		peak = std::max(peak, std::abs(in[i].left()));
		peak = std::max(peak, std::abs(in[i].right()));
	}
	return peak;
}

} // namespace sampleops
} // namespace lmms
