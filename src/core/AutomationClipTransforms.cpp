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
#include <limits>
#include <random>
#include <set>
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
		it.value().m_inValue = min + inValDist;
		it.value().m_outValue = min + outValDist;

		changedTimeMap = true;
	}

	if (changedTimeMap)
	{
		generateTangents();
		publishReadSnapshot();
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
void mirrorRange(const TimeMap& map, int start, int end, TimeMap& out)
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
				out[end - (POS(it) - start)] = AutomationNode(OUTVAL(it), 0);
			}
			else
			{
				out[end - (POS(it) - start)] = AutomationNode(OUTVAL(it), INVAL(it));
			}
			// The curve of the mirrored point is settled by mirrorSegmentCurves() below, once the
			// whole map is known: it belongs to the segment the mirror STARTS, not to this one.
			out[end - (POS(it) - start)].setCurve(it->curve(), it->bend());
		}
	}
}

/*! Each mirrored segment keeps the curve of the ORIGINAL segment it came from. A curve belongs to
 *  the segment that STARTS at its point (include/AutomationNode.h:156), and mirroring [start, end]
 *  reverses time, so the segment A -> B inside the range comes back as B' -> A': it now starts at
 *  the mirror of B, which is the point that must take A's curve - B's predecessor inside the range.
 *  Taking the curve of the point's own mirror instead is one segment off and puts every shape onto
 *  the segment that follows the one it was drawn on.
 *
 *  A Bend's amount is negated with its segment: bendValueAt() below shapes a segment with
 *  pow(t, 1+4*bend) (or 1-pow(1-t, 1-4*bend)) from the point that starts it, and reading that
 *  segment backwards is exactly t -> 1-t, which is what -bend is.
 *
 *  A point whose predecessor lies outside the range keeps its own curve: the segment it now starts
 *  came from one only half inside the flip, and there is no original segment to take it from. */
void mirrorSegmentCurves(const TimeMap& map, int start, int end, TimeMap& out)
{
	auto previous = map.lowerBound(0);
	for (auto it = map.begin(); it != map.end(); ++it)
	{
		if (POS(it) < start) { previous = it; continue; }
		if (POS(it) > end) { break; }
		const auto mirrored = out.find(end - (POS(it) - start));
		const bool reversed = POS(previous) >= start && POS(previous) < POS(it);
		if (mirrored != out.end())
		{
			const float bend = reversed ? -previous->bend() : it->bend();
			mirrored->setCurve(reversed ? previous->curve() : it->curve(), bend);
		}
		previous = it;
	}
}

//! The two end points of a flipped range: each keeps the other end's values (or the value there).
void flipEnds(AutomationClip* clip, const TimeMap& map, int start, int end, TimeMap& out)
{
	if (map.contains(start) && map.contains(end))
	{
		out[start] = AutomationNode(map[start].getInValue(), map[end].getInValue());
		out[end] = AutomationNode(map[start].getOutValue(), map[end].getOutValue());
	}
	else if (map.contains(start))
	{
		out[start] = AutomationNode(map[start].getInValue(), clip->valueAt(end));
		out[end] = AutomationNode(map[start].getOutValue(), clip->valueAt(end));
	}
	else if (map.contains(end))
	{
		out[start] = AutomationNode(clip->valueAt(start), map[end].getInValue());
		out[end] = AutomationNode(clip->valueAt(start), map[end].getOutValue());
	}
	else
	{
		out[start] = AutomationNode(clip->valueAt(start), clip->valueAt(end));
		out[end] = AutomationNode(clip->valueAt(start), clip->valueAt(end));
	}

	// The curves are not settled here: these two points are rebuilt, not mirrored, and the
	// curve each of them owns is the curve of the segment that STARTS at it. mirrorSegmentCurves()
	// works that out over the whole finished map, which is the only place it can be known.
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
	mirrorRange(m_timeMap, start, end, tempMap);
	flipEnds(this, m_timeMap, start, end, tempMap);
	// Last: a point's curve decides the segment after it, so which curve a mirrored point takes
	// can only be read off the finished map.
	mirrorSegmentCurves(m_timeMap, start, end, tempMap);

	m_timeMap.clear();

	m_timeMap = tempMap;

	cleanObjects();

	generateTangents();
	publishReadSnapshot();
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

/*! The most points one drawShape may name. A draw over more is refused whole, before the
 *  range is cleared: the request is refused, not quietly thinned, because a caller that
 *  asked for a point every tick over the whole song has asked for something no clip can
 *  hold. 1<<16 is far above any draw a person makes and far below anything that hurts. */
constexpr long long DrawShapeMaxPoints = 1 << 16;

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
	// The point count is known before anything is edited: a draw inserts one point every
	// @a step ticks, so a wide range at a fine step names an impractical number of them
	// (0..INT_MAX by 1 is two billion insertions, which exhausts memory and stalls the
	// application). A draw over more than DrawShapeMaxPoints is refused whole - before
	// the range is cleared - rather than half-drawn.
	const long long wanted = (static_cast<long long>(end) - start) / step + 2;
	if (wanted > DrawShapeMaxPoints) { return; }
	QMutexLocker m(&m_clipMutex);
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> uniform(0.f, 1.f);
	const double span = static_cast<double>(end - start);
	for (auto it = m_timeMap.lowerBound(start); it != m_timeMap.end() && it.key() <= end;) { it = m_timeMap.erase(it); }
	for (long long at = start;; at += step)  // wide: a step past INT_MAX must end the loop
	{
		const int tick = static_cast<int>(std::min<long long>(at, end));  // a span the step does not divide still ends on a point
		const double x = (tick - start) / span;
		const double phase = std::fmod(x * std::max(1, cycles), 1.0);
		const float level = shape == Shape::Adsr ? adsrAt(x) : shapeAt(shape, phase, uniform(rng));
		const float value = low + (high - low) * level;
			m_timeMap[tick] = AutomationNode(value);
		if (tick == end) { break; }
	}
	generateTangents();
	publishReadSnapshot();
	emit dataChanged();
}

namespace
{

//! Whether every scale and offset transformRange was handed can take part in arithmetic:
//! a NaN or an infinity has no tick, so a transform carrying one is refused whole rather
//! than half-written (and rather than casting a non-finite double to an int).
bool transformInputsAreFinite(double timeScale, int timeOffset, double valueScale, double valueOffset)
{
	return std::isfinite(timeScale) && std::isfinite(valueScale) && std::isfinite(valueOffset)
		&& timeOffset >= std::numeric_limits<int>::min() / 2 && timeOffset <= std::numeric_limits<int>::max() / 2;
}

/*! @a pos rescaled about @a start and shifted by @a timeOffset, as a tick clamped into
 *  [0, INT_MAX]. The scaling is done in long double and the round is made only after the
 *  result is clamped, so a scale far outside the representable range cannot overflow the
 *  conversion or the following integer addition. */
int transformedTick(int start, int pos, double timeScale, int timeOffset)
{
	const long double scaled = static_cast<long double>(start) + (pos - start) * timeScale;
	const long double limit = static_cast<long double>(std::numeric_limits<int>::max());
	const long double clamped = std::clamp(scaled + timeOffset, 0.0L, limit);
	return static_cast<int>(std::llround(clamped));
}

/*! False when two of @a destinations are the same tick, or when one of them is a tick
 *  @a kept already holds. A time map holds ONE node per tick, so either case means a
 *  point the caller owns would be silently replaced - while the count the transform
 *  returns went on reporting every source point. A transform that would lose a point is
 *  refused whole instead: the clip is left as it was and nothing is reported as moved. */
bool destinationsAreDistinct(const AutomationClip::timeMap& kept, const std::vector<int>& destinations)
{
	std::set<int> seen;
	for (const int tick : destinations)
	{
		if (kept.contains(tick) || !seen.insert(tick).second) { return false; }
	}
	return true;
}

/*! @a source's tangents, rescaled onto @a point. A tangent is a slope - a value per tick, since
 *  generateTangents() divides a value difference by a tick difference and valueAt()'s cubic branch
 *  multiplies it back by the segment's tick span - so it does not survive a rescaling of either
 *  axis unchanged: the new slope is the old one times valueScale over timeScale (review of
 *  b76118bc8). A negative timeScale also REVERSES time, so the segment that arrived at the point
 *  now leaves it and the two tangents trade sides; the negative divisor is what turns them around,
 *  so a reversal needs no extra sign of its own. A positive scale keeps each tangent on the side it
 *  described. A timeScale of 0 collapses every point in the range onto one tick, where there is no
 *  span to be a slope over: two such points are refused as a collision (see
 *  destinationsAreDistinct), and a lone point keeps the tangents it had rather than the infinity
 *  the quotient would give. */
void carryTangents(AutomationNode& point, const AutomationNode& source, double timeScale, double valueScale)
{
	const bool reversed = timeScale < 0.0;
	const float tangentScale = timeScale == 0.0 ? 1.f : static_cast<float>(valueScale / timeScale);
	point.setInTangent((reversed ? source.getOutTangent() : source.getInTangent()) * tangentScale);
	point.setOutTangent((reversed ? source.getInTangent() : source.getOutTangent()) * tangentScale);
}

} // namespace

int AutomationClip::transformRange(int start, int end, double timeScale, int timeOffset, double valueScale, double valueOffset)
{
	if (!transformInputsAreFinite(timeScale, timeOffset, valueScale, valueOffset)) { return 0; }
	QMutexLocker m(&m_clipMutex);
	timeMap kept;
	std::vector<std::pair<int, AutomationNode>> moved;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it)
	{
		if (POS(it) < start || POS(it) > end) { kept[POS(it)] = *it; continue; }
		moved.emplace_back(POS(it), *it);
	}
	std::vector<int> destinations;
	destinations.reserve(moved.size());
	for (const auto& source : moved) { destinations.push_back(transformedTick(start, source.first, timeScale, timeOffset)); }
	// Before anything is committed, so a collision cannot cost the clip a point on the way out.
	if (!destinationsAreDistinct(kept, destinations)) { return 0; }
	const float pivot = (getMin() + getMax()) / 2.f;
	const auto value = [&](float v) {
		return std::clamp(static_cast<float>((v - pivot) * valueScale + pivot + valueOffset), getMin(), getMax());
	};
	for (std::size_t i = 0; i < moved.size(); ++i)
	{
		const int tick = destinations[i];
		// The point keeps its own curve and bend (review L06-L07a #1), and its tangent
		// state: a node whose tangents were locked by hand keeps them, so the identity
		// transform does not silently regenerate a hand-drawn slope. The generated
		// tangents of an unlocked point are left at their defaults - generateTangents()
		// below derives them from the values and the neighbours anyway.
		AutomationNode point(value(moved[i].second.getInValue()), value(moved[i].second.getOutValue()));
		point.setCurve(moved[i].second.curve(), moved[i].second.bend());
		point.setLockedTangents(moved[i].second.lockedTangents());
		if (moved[i].second.lockedTangents())
		{
			carryTangents(point, moved[i].second, timeScale, valueScale);
		}
		kept[tick] = point;
	}
	m_timeMap = kept;
	generateTangents();
	publishReadSnapshot();
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

int AutomationClip::thin(float tolerance)
{
	QMutexLocker m(&m_clipMutex);
	if (m_timeMap.size() < 3) { return 0; }
	std::vector<std::pair<int, float>> points;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it) { points.emplace_back(POS(it), OUTVAL(it)); }
	std::vector<bool> keep(points.size(), false);
	keep.front() = keep.back() = true;
	// A step (in != out) or a point with its own curve is not on any straight line: kept (review L06-L07a #7).
	// A point whose tangents were locked by hand is kept too: its shape is the point's own and the
	// measurement below only looks at OUT values against a straight line, so it cannot see that shape.
	std::size_t i = 0;
	for (auto it = m_timeMap.begin(); it != m_timeMap.end(); ++it, ++i)
	{
		if (INVAL(it) != OUTVAL(it) || it.value().curve() != AutomationNode::Curve::Clip
		    || it.value().lockedTangents())
		{
			keep[i] = true;
		}
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
	publishReadSnapshot();
	emit dataChanged();
	return removed;
}

} // namespace lmms
