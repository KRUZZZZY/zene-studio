/*
 * AutomationClipTransforms.cpp - edits over an automation clip's points: flip, shapes, transform, thin
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

/*! Split out of AutomationClip.cpp (0.5.0, for the file-length ratchet): the flips, moved here
 *  unchanged, and the automation editing of 0.5.0 L06 - ED-184 shapes drawn over a range,
 *  ED-186 a range of points rescaled in time and value, ED-187 thinning (Douglas-Peucker). Each
 *  edits the time map under the clip's mutex, regenerates the tangents and announces the change;
 *  the commands that call them take the clip's journal checkpoint first. */

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "AutomationClip.h"
#include "AutomationNode.h"

namespace lmms
{

void AutomationClip::flipY(int min, int max)
{
	QMutexLocker m(&m_clipMutex);

	bool changedTimeMap = false;

	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it)
	{
		// Get distance from IN/OUT values to max value
		float inValDist = max - INVAL(it);
		float outValDist = max - OUTVAL(it);

		// To flip, that will be the new distance between
		// the IN/OUT values and the min value
		it.value().setInValue(min + inValDist);
		it.value().setOutValue(min + outValDist);

		changedTimeMap = true;
	}

	if (changedTimeMap)
	{
		generateTangents();
		emit dataChanged();
	}
}




void AutomationClip::flipY()
{
	flipY(getMin(), getMax());
}




namespace
{

using TimeMap = AutomationClip::timeMap;

//! flipX's body, split out unchanged for the CCN target (tests/complexity-gate.sh): the points in
//! [start, end] mirrored in time into @a out, the others copied.
void mirrorRange(AutomationClip* clip, const TimeMap& map, int start, int end, TimeMap& out)
{
	const TimeMap::const_iterator firstIterator = map.lowerBound(0);
	for (auto it = map.begin(); it != map.end(); ++it)
	{
		if (POS(it) < start || POS(it) > end)
		{
			out[POS(it)] = *it;
		}
		else
		{
			// If the first node in the clip is not at the start, it can break things when clipping, so
			// we have to set its in value to 0.
			if (it == firstIterator && POS(firstIterator) > 0)
			{
				out[end - (POS(it) - start)] = AutomationNode(clip, OUTVAL(it), 0, end - (POS(it) - start));
			}
			else
			{
				out[end - (POS(it) - start)] = AutomationNode(clip, OUTVAL(it), INVAL(it), end - (POS(it) - start));
			}
		}
	}
}

//! The two end points of a flipped range: each keeps the other end's values (or the value there).
void flipEnds(AutomationClip* clip, const TimeMap& map, int start, int end, TimeMap& out)
{
	if (map.contains(start) && map.contains(end))
	{
		out[start] = AutomationNode(clip, map[start].getInValue(), map[end].getInValue(), start);
		out[end] = AutomationNode(clip, map[start].getOutValue(), map[end].getOutValue(), end);
	}
	else if (map.contains(start))
	{
		out[start] = AutomationNode(clip, map[start].getInValue(), clip->valueAt(end), start);
		out[end] = AutomationNode(clip, map[start].getOutValue(), clip->valueAt(end), end);
	}
	else if (map.contains(end))
	{
		out[start] = AutomationNode(clip, clip->valueAt(start), map[end].getInValue(), start);
		out[end] = AutomationNode(clip, clip->valueAt(start), map[end].getOutValue(), end);
	}
	else
	{
		out[start] = AutomationNode(clip, clip->valueAt(start), clip->valueAt(end), start);
		out[end] = AutomationNode(clip, clip->valueAt(start), clip->valueAt(end), end);
	}
}

} // namespace

void AutomationClip::flipX(int start, int end)
{
	QMutexLocker m(&m_clipMutex);

	timeMap::const_iterator firstIterator = m_timeMap.lowerBound(0);

	if (firstIterator == m_timeMap.end()) { return; }

	if (start == -1 && end == -1) { start = 0; end = length() - startTimeOffset(); }
	else if (!(end >= 0 && start >= 0 && end > start)) { return; }

	// Temporary map where we will store the flipped version
	// of our clip
	timeMap tempMap;
	mirrorRange(this, m_timeMap, start, end, tempMap);
	flipEnds(this, m_timeMap, start, end, tempMap);

	m_timeMap.clear();

	m_timeMap = tempMap;

	cleanObjects();

	generateTangents();
	emit dataChanged();
}




namespace
{

//! One period of @a shape at phase @a x (0..1), in 0..1; @a random is the step's own draw.
float shapeAt(AutomationClip::Shape shape, double x, float random)
{
	constexpr double Tau = 6.283185307179586;
	switch (shape)
	{
	case AutomationClip::Shape::Sine: return static_cast<float>(0.5 + 0.5 * std::sin(Tau * x));
	case AutomationClip::Shape::Triangle: return static_cast<float>(x < 0.5 ? 2.0 * x : 2.0 - 2.0 * x);
	case AutomationClip::Shape::Square: return x < 0.5 ? 1.f : 0.f;
	case AutomationClip::Shape::Saw: return static_cast<float>(x);
	case AutomationClip::Shape::Random: return random;
	case AutomationClip::Shape::Adsr: break;
	}
	return 0.f;
}

//! A whole-range ADSR envelope at @a x (0..1): attack 10%, decay to 60% by 30%, release the last 20%.
float adsrAt(double x)
{
	if (x < 0.1) { return static_cast<float>(x / 0.1); }
	if (x < 0.3) { return static_cast<float>(1.0 - 0.4 * (x - 0.1) / 0.2); }
	if (x < 0.8) { return 0.6f; }
	return static_cast<float>(0.6 * (1.0 - (x - 0.8) / 0.2));
}

} // namespace

void AutomationClip::drawShape(int start, int end, Shape shape, int cycles, float low, float high, int step, unsigned seed)
{
	if (end <= start || step <= 0) { return; }
	QMutexLocker m(&m_clipMutex);
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> uniform(0.f, 1.f);
	const double span = static_cast<double>(end - start);
	for (auto it = m_timeMap.lowerBound(start); it != m_timeMap.end() && it.key() <= end;) { it = m_timeMap.erase(it); }
	for (long long at = start; at <= end; at += step)  // wide: a step past INT_MAX must end the loop
	{
		const int tick = static_cast<int>(at);
		const double x = (tick - start) / span;
		const double phase = std::fmod(x * std::max(1, cycles), 1.0);
		const float level = shape == Shape::Adsr ? adsrAt(x) : shapeAt(shape, phase, uniform(rng));
		const float value = low + (high - low) * level;
		m_timeMap[tick] = AutomationNode(this, value, tick);
	}
	generateTangents();
	emit dataChanged();
}

int AutomationClip::transformRange(int start, int end, double timeScale, int timeOffset, double valueScale, double valueOffset)
{
	QMutexLocker m(&m_clipMutex);
	timeMap kept;
	std::vector<std::pair<int, AutomationNode>> moved;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it)
	{
		if (POS(it) < start || POS(it) > end) { kept[POS(it)] = *it; continue; }
		moved.emplace_back(POS(it), *it);
	}
	const float pivot = (getMin() + getMax()) / 2.f;
	const auto value = [&](float v) {
		return std::clamp(static_cast<float>((v - pivot) * valueScale + pivot + valueOffset), getMin(), getMax());
	};
	for (const auto& [pos, node] : moved)
	{
		const int tick = std::max(0, static_cast<int>(std::lround(start + (pos - start) * timeScale)) + timeOffset);
		// The point keeps its own curve and bend (review L06-L07a #1).
		AutomationNode point(this, value(node.getInValue()), value(node.getOutValue()), tick);
		point.setCurve(node.curve(), node.bend());
		kept[tick] = point;
	}
	m_timeMap = kept;
	generateTangents();
	emit dataChanged();
	return static_cast<int>(moved.size());
}

namespace
{

//! Douglas-Peucker over @a points[first..last]: marks in @a keep the points that matter.
void simplify(const std::vector<std::pair<int, float>>& points, std::size_t first, std::size_t last, float tolerance,
	std::vector<bool>& keep)
{
	if (last <= first + 1) { return; }
	const auto [x0, y0] = points[first];
	const auto [x1, y1] = points[last];
	float worst = -1.f;
	std::size_t at = first;
	for (std::size_t i = first + 1; i < last; ++i)
	{
		const double t = x1 == x0 ? 0.0 : static_cast<double>(points[i].first - x0) / (x1 - x0);
		const float distance = std::abs(points[i].second - static_cast<float>(y0 + (y1 - y0) * t));
		if (distance > worst) { worst = distance; at = i; }
	}
	if (worst <= tolerance) { return; }
	keep[at] = true;
	simplify(points, first, at, tolerance, keep);
	simplify(points, at, last, tolerance, keep);
}

} // namespace

float AutomationClip::bendValueAt(timeMap::const_iterator v, int offset) const
{
	const auto next = std::next(v);
	const double t = std::clamp(static_cast<double>(offset) / std::max(1, POS(next) - POS(v)), 0.0, 1.0);
	const double bend = v.value().bend();
	const double shaped = bend >= 0.0 ? std::pow(t, 1.0 + 4.0 * bend) : 1.0 - std::pow(1.0 - t, 1.0 - 4.0 * bend);
	return static_cast<float>(OUTVAL(v) + (INVAL(next) - OUTVAL(v)) * shaped);
}

int AutomationClip::thin(float tolerance)
{
	QMutexLocker m(&m_clipMutex);
	if (m_timeMap.size() < 3) { return 0; }
	std::vector<std::pair<int, float>> points;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it) { points.emplace_back(POS(it), OUTVAL(it)); }
	std::vector<bool> keep(points.size(), false);
	keep.front() = keep.back() = true;
	// A step (in != out) or a point with its own curve is not on any straight line: kept (review L06-L07a #7).
	std::size_t i = 0;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it, ++i)
	{
		if (INVAL(it) != OUTVAL(it) || it.value().curve() != AutomationNode::Curve::Clip) { keep[i] = true; }
	}
	simplify(points, 0, points.size() - 1, tolerance, keep);
	int removed = 0;
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		if (keep[i]) { continue; }
		m_timeMap.remove(points[i].first);
		++removed;
	}
	generateTangents();
	emit dataChanged();
	return removed;
}

} // namespace lmms
