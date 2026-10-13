/*
 * RackSplit.cpp - a rack's chains fed by frequency band, mid / side or left / right
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

#include "RackSplit.h"

#include <algorithm>
#include <cmath>

namespace lmms
{

namespace
{

constexpr float kLowestSpread = 120.f;
constexpr float kHighestSpread = 6000.f;

} // namespace

const QStringList& RackSplit::modeNames()
{
	static const QStringList names{QStringLiteral("none"), QStringLiteral("bands"), QStringLiteral("mid_side"),
		QStringLiteral("left_right")};
	return names;
}

QString RackSplit::modeName(RackSplitMode mode)
{
	return modeNames().value(static_cast<int>(mode));
}

bool RackSplit::modeFromName(const QString& name, RackSplitMode* mode)
{
	const int index = modeNames().indexOf(name);
	if (index < 0) { return false; }
	*mode = static_cast<RackSplitMode>(index);
	return true;
}

float RackSplit::crossover(int k, int bands) const
{
	if (k < crossoverCount) { return crossovers[static_cast<std::size_t>(k)]; }
	// Spread in octaves: k of the (bands - 1) edges, between the two spread limits.
	const int edges = std::max(1, bands - 1);
	const float t = edges == 1 ? 0.5f : static_cast<float>(k) / static_cast<float>(edges - 1);
	return kLowestSpread * std::pow(kHighestSpread / kLowestSpread, t);
}

void RackSplit::save(QDomElement& rack) const
{
	if (!active()) { return; }
	rack.setAttribute(QStringLiteral("split"), modeName(mode));
	const int count = std::clamp(crossoverCount, 0, MaxBands - 1);
	if (count == 0) { return; }
	QStringList values;
	for (int k = 0; k < count; ++k) { values << QString::number(crossovers[static_cast<std::size_t>(k)], 'g', 7); }
	rack.setAttribute(QStringLiteral("crossovers"), values.join(QLatin1Char(',')));
}

void RackSplit::load(const QDomElement& rack)
{
	*this = RackSplit{};
	if (!modeFromName(rack.attribute(QStringLiteral("split"), QStringLiteral("none")), &mode)) { mode = RackSplitMode::None; }
	const QStringList values = rack.attribute(QStringLiteral("crossovers")).split(QLatin1Char(','), Qt::SkipEmptyParts);
	for (const QString& value : values)
	{
		bool ok = false;
		const float hz = value.toFloat(&ok);
		if (!ok || crossoverCount >= MaxBands - 1) { continue; }
		crossovers[static_cast<std::size_t>(crossoverCount++)] = std::clamp(hz, MinCrossoverHz, MaxCrossoverHz);
	}
	std::sort(crossovers.begin(), crossovers.begin() + crossoverCount);
}

void RackSplitFilter::addStage(BiquadShape shape, float hz, float sampleRate)
{
	if (m_stageCount >= MaxStages) { return; }
	// Q = 1/sqrt(2): two Butterworth sections in a row make a Linkwitz-Riley 4th order, and the
	// all-pass is what such a low-pass plus high-pass pair sums to.
	BiquadSection<2>& stage = m_stages[static_cast<std::size_t>(m_stageCount++)];
	stage.set(designBiquad(shape, hz, 1.0 / std::sqrt(2.0), sampleRate));
	stage.reset();
}

void RackSplitFilter::configureBands(const RackSplit& split, int position, int count, float sampleRate)
{
	const int bands = std::min(count, RackSplit::MaxBands);
	if (position >= bands) { m_kind = Kind::Silence; return; }
	m_kind = Kind::Band;
	// Band k of the classic crossover tree, computed on its own: the high-passes of every crossover
	// below it, the low-pass of crossover k, then an all-pass at every crossover above that (the
	// phase the higher bands carry) - so the bands sum to an all-pass, flat in level.
	for (int k = 0; k < position; ++k)
	{
		addStage(BiquadShape::HighPass, split.crossover(k, bands), sampleRate);
		addStage(BiquadShape::HighPass, split.crossover(k, bands), sampleRate);
	}
	for (int pass = 0; position < bands - 1 && pass < 2; ++pass) { addStage(BiquadShape::LowPass, split.crossover(position, bands), sampleRate); }
	for (int k = position + 1; k < bands - 1; ++k) { addStage(BiquadShape::AllPass, split.crossover(k, bands), sampleRate); }
}

void RackSplitFilter::configure(const RackSplit& split, int position, int count, float sampleRate)
{
	m_kind = Kind::Pass;
	m_stageCount = 0;
	if (!split.active() || count < 2 || position < 0 || position >= count || sampleRate <= 0.f) { return; }
	if (split.mode == RackSplitMode::Bands) { configureBands(split, position, count, sampleRate); return; }
	// Mid / side and left / right use the first two chains; the rest get silence.
	const bool midSide = split.mode == RackSplitMode::MidSide;
	static constexpr Kind pairs[2][2] = {{Kind::Left, Kind::Right}, {Kind::Mid, Kind::Side}};
	m_kind = position < 2 ? pairs[midSide ? 1 : 0][position] : Kind::Silence;
}

void RackSplitFilter::processBand(float* samples, f_cnt_t frames) noexcept
{
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 2; ++c)
		{
			float x = samples[2 * f + c];
			for (int s = 0; s < m_stageCount; ++s) { x = m_stages[static_cast<std::size_t>(s)].run(x, c); }
			samples[2 * f + c] = x;
		}
	}
}

void RackSplitFilter::process(float* samples, f_cnt_t frames) noexcept
{
	if (m_kind == Kind::Pass) { return; }
	if (m_kind == Kind::Band) { processBand(samples, frames); return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		float& left = samples[2 * f];
		float& right = samples[2 * f + 1];
		const float mid = 0.5f * (left + right);
		const float side = 0.5f * (left - right);
		switch (m_kind)
		{
		case Kind::Mid: left = mid; right = mid; break;
		case Kind::Side: left = side; right = -side; break;
		case Kind::Left: right = 0.f; break;
		case Kind::Right: left = 0.f; break;
		default: left = 0.f; right = 0.f; break;
		}
	}
}

} // namespace lmms
