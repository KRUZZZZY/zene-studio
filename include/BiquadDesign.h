/*
 * BiquadDesign.h - the cookbook second-order sections the 0.5.0 devices share
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

#ifndef LMMS_BIQUAD_DESIGN_H
#define LMMS_BIQUAD_DESIGN_H

#include <algorithm>
#include <cmath>

namespace lmms
{

//! The shapes of the audio EQ cookbook (R. Bristow-Johnson) a 0.5.0 device or the rack split uses.
enum class BiquadShape
{
	LowPass,
	HighPass,
	BandPass,  //!< constant 0 dB peak gain
	AllPass,
	Peak,
	LowShelf,
	HighShelf
};

//! One section's coefficients, normalised by a0, for y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct BiquadCoefficients
{
	float b0 = 1.f;
	float b1 = 0.f;
	float b2 = 0.f;
	float a1 = 0.f;
	float a2 = 0.f;
};

/*! The cookbook section of @a shape at @a hz (kept below 0.45 of @a sampleRate), quality @a q
 *  (shelves use the cookbook's Q form too: 0.7071 is slope S = 1, the steepest without
 *  overshoot) and, for a peak or shelf, @a gainDb.
 *  Plain arithmetic: allocation-free, so an audio thread may call it when a parameter moves. */
inline BiquadCoefficients designBiquad(BiquadShape shape, double hz, double q, double sampleRate, double gainDb = 0.0)
{
	constexpr double pi = 3.14159265358979323846;
	const double w0 = 2.0 * pi * std::clamp(hz, 1.0, 0.45 * sampleRate) / sampleRate;
	const double cosw = std::cos(w0);
	const double alpha = std::sin(w0) / (2.0 * std::max(q, 1e-3));
	const double a = std::pow(10.0, gainDb / 40.0);
	const double shelf = 2.0 * std::sqrt(a) * alpha;
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
	switch (shape)
	{
	case BiquadShape::LowPass: b0 = b2 = (1.0 - cosw) / 2.0; b1 = 1.0 - cosw; a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;
	case BiquadShape::HighPass: b0 = b2 = (1.0 + cosw) / 2.0; b1 = -(1.0 + cosw); a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;
	case BiquadShape::BandPass: b0 = alpha; b1 = 0.0; b2 = -alpha; a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;
	case BiquadShape::AllPass: b0 = 1.0 - alpha; b1 = -2.0 * cosw; b2 = 1.0 + alpha; a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha; break;
	case BiquadShape::Peak:
		b0 = 1.0 + alpha * a; b1 = -2.0 * cosw; b2 = 1.0 - alpha * a; a0 = 1.0 + alpha / a; a1 = -2.0 * cosw; a2 = 1.0 - alpha / a;
		break;
	case BiquadShape::LowShelf:
		b0 = a * ((a + 1.0) - (a - 1.0) * cosw + shelf); b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw);
		b2 = a * ((a + 1.0) - (a - 1.0) * cosw - shelf); a0 = (a + 1.0) + (a - 1.0) * cosw + shelf;
		a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosw); a2 = (a + 1.0) + (a - 1.0) * cosw - shelf;
		break;
	case BiquadShape::HighShelf:
		b0 = a * ((a + 1.0) + (a - 1.0) * cosw + shelf); b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw);
		b2 = a * ((a + 1.0) + (a - 1.0) * cosw - shelf); a0 = (a + 1.0) - (a - 1.0) * cosw + shelf;
		a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw); a2 = (a + 1.0) - (a - 1.0) * cosw - shelf;
		break;
	}
	return {static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0), static_cast<float>(a1 / a0),
		static_cast<float>(a2 / a0)};
}

/*! One section with its state for @a Channels channels (transposed direct form II), for a device's
 *  audio path: plain data, fixed size, no allocation. */
template<int Channels>
struct BiquadSection
{
	BiquadCoefficients c;
	float z1[Channels] = {};
	float z2[Channels] = {};

	void set(const BiquadCoefficients& coefficients) { c = coefficients; }
	void reset()
	{
		std::fill(z1, z1 + Channels, 0.f);
		std::fill(z2, z2 + Channels, 0.f);
	}
	float run(float x, int channel) noexcept
	{
		// A channel outside the section's state is a caller error: pass the input through and touch
		// nothing, rather than read or write past the fixed-size arrays.
		if (channel < 0 || channel >= Channels) { return x; }
		const float y = c.b0 * x + z1[channel];
		z1[channel] = c.b1 * x - c.a1 * y + z2[channel];
		z2[channel] = c.b2 * x - c.a2 * y;
		return y;
	}
};

//! The one-pole smoothing coefficient that settles in @a ms at @a sampleRate (an envelope's
//! attack or release): y += (1 - k) (x - y), written y = x + k (y - x).
inline float settleCoefficient(float ms, float sampleRate)
{
	return static_cast<float>(std::exp(-1.0 / std::max(1.0, static_cast<double>(ms) * 0.001 * static_cast<double>(sampleRate))));
}

} // namespace lmms

#endif // LMMS_BIQUAD_DESIGN_H
