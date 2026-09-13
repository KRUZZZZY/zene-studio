/*
 * ClipEdits.cpp - the per-clip fade and clip-gain envelope.
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

#include "ClipEdits.h"

#include <algorithm>
#include <cmath>

namespace lmms
{

namespace
{

//! pi/2, the quarter-turn the equal-power ramp is a sine of.
constexpr double kHalfPi = 1.57079632679489661923;

QString shapeName(FadeShape shape)
{
	switch (shape)
	{
	case FadeShape::Exponential: return QStringLiteral("exponential");
	case FadeShape::EqualPower: return QStringLiteral("equal_power");
	case FadeShape::Linear: break;
	}
	return QStringLiteral("linear");
}

} // namespace

QString fadeShapeName(FadeShape shape)
{
	return shapeName(shape);
}

bool fadeShapeFromName(const QString& name, FadeShape* shape)
{
	if (shape == nullptr) { return false; }
	if (name == QLatin1String("linear")) { *shape = FadeShape::Linear; return true; }
	if (name == QLatin1String("exponential")) { *shape = FadeShape::Exponential; return true; }
	if (name == QLatin1String("equal_power")) { *shape = FadeShape::EqualPower; return true; }
	return false;
}

float fadeShapeGain(FadeShape shape, double x)
{
	const double t = std::clamp(x, 0.0, 1.0);
	switch (shape)
	{
	// The DAW convention for a "logarithmic" ramp: the amplitude rises as the
	// square of the progress, which is what a fader move of the same shape does.
	case FadeShape::Exponential: return static_cast<float>(t * t);
	// sin(t * pi/2): at t = 0.5 this is sqrt(2)/2, so two of these meeting over
	// one range are sin^2 + cos^2 = 1 in power. The crossfade contract.
	case FadeShape::EqualPower: return static_cast<float>(std::sin(t * kHalfPi));
	case FadeShape::Linear: break;
	}
	return static_cast<float>(t);
}

float gainDbToLinear(float db)
{
	return static_cast<float>(std::pow(10.0, static_cast<double>(db) / 20.0));
}

float gainLinearToDb(float linear)
{
	if (!(linear > 0.0f)) { return -1000.0f; } // -inf on the wire is not JSON
	return static_cast<float>(20.0 * std::log10(static_cast<double>(linear)));
}

float clipFadeGainAt(f_cnt_t frame, f_cnt_t clipFrames,
	f_cnt_t fadeInFrames, f_cnt_t fadeOutFrames,
	FadeShape fadeInShape, FadeShape fadeOutShape)
{
	if (clipFrames == 0) { return 1.0f; }

	double value = 1.0;

	// The ramp up: the region is the clip's first `fadeInFrames` output frames,
	// so frame 0 is exactly silent and the frame after the region is exactly 1.
	if (fadeInFrames > 0 && frame < fadeInFrames)
	{
		value *= fadeShapeGain(fadeInShape,
			static_cast<double>(frame) / static_cast<double>(fadeInFrames));
	}

	// The ramp down: measured inward from the clip's last output frame, so the
	// region is exactly `fadeOutFrames` long and the ramp is monotonic across it.
	if (fadeOutFrames > 0)
	{
		const f_cnt_t fadeOutStart = clipFrames > fadeOutFrames ? clipFrames - fadeOutFrames : 0;
		if (frame >= fadeOutStart)
		{
			const auto remaining = clipFrames > frame ? clipFrames - frame : 0;
			value *= fadeShapeGain(fadeOutShape,
				static_cast<double>(remaining) / static_cast<double>(fadeOutFrames));
		}
	}

	return static_cast<float>(value);
}

} // namespace lmms
