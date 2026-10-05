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

#include <QStringList>

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
	case FadeShape::Logarithmic: return QStringLiteral("logarithmic");
	case FadeShape::SCurve: return QStringLiteral("s_curve");
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
	if (name == QLatin1String("logarithmic")) { *shape = FadeShape::Logarithmic; return true; }
	if (name == QLatin1String("s_curve")) { *shape = FadeShape::SCurve; return true; }
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
	case FadeShape::Logarithmic: return static_cast<float>(1.0 - (1.0 - t) * (1.0 - t));
	case FadeShape::SCurve: return static_cast<float>(t * t * (3.0 - 2.0 * t));
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

float envelopeGainAt(const ClipEdits& edits, double tick)
{
	const int count = edits.envelopeCount;
	if (count <= 0) { return 1.0f; }
	const auto& points = edits.envelope;
	if (tick <= points[0].tick) { return gainDbToLinear(points[0].gainDb); }
	for (int i = 1; i < count; ++i)
	{
		if (tick > points[i].tick) { continue; }
		const GainPoint& a = points[i - 1];
		const GainPoint& b = points[i];
		const double span = std::max(1, b.tick - a.tick);
		const double t = (tick - a.tick) / span;
		return gainDbToLinear(static_cast<float>(a.gainDb + (b.gainDb - a.gainDb) * t));
	}
	return gainDbToLinear(points[count - 1].gainDb);
}

QString envelopeToText(const ClipEdits& edits)
{
	QStringList parts;
	for (int i = 0; i < edits.envelopeCount; ++i)
	{
		parts.append(QStringLiteral("%1:%2").arg(edits.envelope[i].tick).arg(static_cast<double>(edits.envelope[i].gainDb), 0, 'f', 3));
	}
	return parts.join(QLatin1Char(';'));
}

void envelopeFromText(const QString& text, ClipEdits* edits)
{
	edits->envelopeCount = 0;
	for (const QString& part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts))
	{
		if (edits->envelopeCount >= MaxGainPoints) { break; }
		const int colon = part.indexOf(QLatin1Char(':'));
		bool tickOk = false;
		const int tick = part.left(colon).toInt(&tickOk);
		const float db = part.mid(colon + 1).toFloat();
		// A tick that is not a tick used to convert to 0 and put the point at the clip's start.
		if (colon <= 0 || !tickOk || !std::isfinite(db)) { continue; }  // a damaged file never puts an infinite gain on the audio path
		edits->envelope[edits->envelopeCount++] = {std::max(0, tick), std::clamp(db, MinGainPointDb, MaxGainPointDb)};
	}
	std::sort(edits->envelope.begin(), edits->envelope.begin() + edits->envelopeCount,
		[](const GainPoint& a, const GainPoint& b) { return a.tick < b.tick; });
}

float clipSpeedAt(const ClipEdits& edits, f_cnt_t frame, f_cnt_t clipFrames, f_cnt_t speedInFrames, f_cnt_t speedOutFrames)
{
	const double along = clipFrames > 0 ? std::clamp(static_cast<double>(frame) / static_cast<double>(clipFrames), 0.0, 1.0) : 0.0;
	double speed = (edits.speedFrom + (edits.speedTo - edits.speedFrom) * along) / 100.0;
	if (speedInFrames > 0 && frame < speedInFrames) { speed *= static_cast<double>(std::max<f_cnt_t>(frame, 0)) / static_cast<double>(speedInFrames); }
	const f_cnt_t left = clipFrames - frame;
	if (speedOutFrames > 0 && left < speedOutFrames) { speed *= static_cast<double>(std::max<f_cnt_t>(left, 0)) / static_cast<double>(speedOutFrames); }
	return static_cast<float>(std::max(speed, 1.0 / 32.0));
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

float preFadedGainAt(const PreFadeSpan& span, std::int64_t clipPos, FadeShape fadeInShape, FadeShape fadeOutShape)
{
	double value = 1.0;
	const std::int64_t inLength = span.preInFrames + span.fadeInFrames;
	const std::int64_t intoIn = clipPos + span.preInFrames;  // frames since the fade-in began
	if (inLength > 0 && intoIn < inLength)
	{
		value *= intoIn <= 0 ? 0.0 : fadeShapeGain(fadeInShape, static_cast<double>(intoIn) / static_cast<double>(inLength));
	}
	const std::int64_t outLength = span.fadeOutFrames + span.preOutFrames;
	const std::int64_t left = span.clipFrames + span.preOutFrames - clipPos;  // frames until the fade-out ends
	if (outLength > 0 && left < outLength)
	{
		value *= left <= 0 ? 0.0 : fadeShapeGain(fadeOutShape, static_cast<double>(left) / static_cast<double>(outLength));
	}
	return static_cast<float>(value);
}

} // namespace lmms
