/*
 * TakeLane.cpp - the take-lane model and the composite view (task #600).
 *
 * The whole type is integers and lane numbers. It never opens a take, never
 * writes a sample and never copies a buffer: "the source takes are never
 * destructively edited" is a property of this file's contents, not a promise
 * made elsewhere. docs/COMPING.md states the element shape and the reasons.
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

#include "TakeLane.h"

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QLatin1String>

#include "Clip.h"
#include "TimePos.h"

namespace lmms
{

namespace
{

//! Lane elements are kept sorted by index, so the index alone is the ordering
//! every lookup uses (lower_bound with this comparator).
bool laneLessThan(const TakeLane& lane, int index) { return lane.index < index; }
bool segmentBefore(const TakeLaneSegment& seg, const TakeLaneSegment& other)
{
	return seg.beginTick < other.beginTick;
}

/*! True when \p seg continues \p previous: it starts where the previous one
 *  ended, takes the same lane, and reads that lane's take at the offset the
 *  previous one's own arithmetic would have reached (so "the take restarts
 *  here" is expressible and is NOT merged away). */
bool continuesTake(const TakeLaneSegment& previous, const TakeLaneSegment& seg)
{
	if (previous.endTick != seg.beginTick) { return false; }
	if (previous.laneIndex != seg.laneIndex) { return false; }
	return seg.sourceOffset == previous.sourceOffset + (seg.beginTick - previous.beginTick);
}

TakeLane laneFromElement(const QDomElement& element)
{
	TakeLane lane;
	lane.index = std::max(0, element.attribute(QStringLiteral("index"), QStringLiteral("0")).toInt());
	lane.name = element.attribute(QStringLiteral("name"));
	return lane;
}

TakeLaneSegment segmentFromElement(const QDomElement& element)
{
	TakeLaneSegment seg;
	seg.beginTick = std::max(0, element.attribute(QStringLiteral("begin"), QStringLiteral("0")).toInt());
	seg.endTick = std::max(0, element.attribute(QStringLiteral("end"), QStringLiteral("0")).toInt());
	seg.laneIndex = std::max(0, element.attribute(QStringLiteral("lane"), QStringLiteral("0")).toInt());
	seg.sourceOffset = std::max(0, element.attribute(QStringLiteral("srcpos"), QStringLiteral("0")).toInt());
	return seg;
}

} // namespace


int TakeLaneModel::addLane(const QString& name)
{
	// The lowest index the track does not use. Reuse after a removal is
	// deliberate: every reference to the removed lane was already re-pointed by
	// removeLane(), so a recycled index cannot alias a stale segment.
	int index = 0;
	for (const TakeLane& lane : m_lanes)
	{
		if (lane.index != index) { break; }
		++index;
	}
	TakeLane added;
	added.index = index;
	added.name = name;
	const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), index, laneLessThan);
	m_lanes.insert(pos, added);
	return index;
}


int TakeLaneModel::baseLane() const
{
	return m_lanes.empty() ? -1 : m_lanes.front().index;
}


bool TakeLaneModel::hasLane(int index) const
{
	const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), index, laneLessThan);
	return pos != m_lanes.end() && pos->index == index;
}


bool TakeLaneModel::removeLane(int index)
{
	const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), index, laneLessThan);
	if (pos == m_lanes.end() || pos->index != index) { return false; }
	m_lanes.erase(pos);
	if (m_lanes.empty())
	{
		// No lane can supply a tick any more, so there is no composite left.
		m_segments.clear();
		return true;
	}
	// A composite stays total over its span (I6): the range a removed lane
	// supplied falls back to the base lane rather than becoming a hole. The
	// offset resets because the base lane's take has its own start.
	const int base = baseLane();
	for (TakeLaneSegment& seg : m_segments)
	{
		if (seg.laneIndex != index) { continue; }
		seg.laneIndex = base;
		seg.sourceOffset = 0;
	}
	mergeSegments();
	return true;
}


void TakeLaneModel::subtractRange(const TakeLaneSegment& seg, int beginTick, int endTick,
	std::vector<TakeLaneSegment>* out)
{
	if (out == nullptr) { return; }
	if (seg.endTick <= beginTick || seg.beginTick >= endTick)
	{
		out->push_back(seg);
		return;
	}
	if (seg.beginTick < beginTick)
	{
		TakeLaneSegment left = seg;
		left.endTick = beginTick;
		out->push_back(left);
	}
	if (seg.endTick > endTick)
	{
		TakeLaneSegment right = seg;
		right.beginTick = endTick;
		right.sourceOffset += endTick - seg.beginTick;
		out->push_back(right);
	}
}


bool TakeLaneModel::canSelect(int beginTick, int endTick, int laneIndex, int sourceOffset) const
{
	if (beginTick < 0 || endTick <= beginTick || sourceOffset < 0) { return false; }
	return hasLane(laneIndex);
}


bool TakeLaneModel::selectSegment(int beginTick, int endTick, int laneIndex, int sourceOffset)
{
	if (!canSelect(beginTick, endTick, laneIndex, sourceOffset)) { return false; }

	std::vector<TakeLaneSegment> painted;
	painted.reserve(m_segments.size() + 1);
	for (const TakeLaneSegment& seg : m_segments)
	{
		subtractRange(seg, beginTick, endTick, &painted);
	}
	TakeLaneSegment chosen;
	chosen.beginTick = beginTick;
	chosen.endTick = endTick;
	chosen.laneIndex = laneIndex;
	chosen.sourceOffset = sourceOffset;
	painted.push_back(chosen);
	m_segments.swap(painted);
	sortSegments();
	mergeSegments();
	return true;
}


void TakeLaneModel::sortSegments()
{
	std::sort(m_segments.begin(), m_segments.end(), segmentBefore);
}


void TakeLaneModel::mergeSegments()
{
	std::vector<TakeLaneSegment> merged;
	merged.reserve(m_segments.size());
	for (const TakeLaneSegment& seg : m_segments)
	{
		if (!merged.empty() && continuesTake(merged.back(), seg))
		{
			merged.back().endTick = seg.endTick;
			continue;
		}
		merged.push_back(seg);
	}
	m_segments.swap(merged);
}


int TakeLaneModel::fillGaps(int spanBegin, int spanEnd)
{
	const int base = baseLane();
	if (base < 0) { return 0; }
	TakeLaneSegment gap;
	gap.laneIndex = base;
	gap.sourceOffset = 0;

	std::vector<TakeLaneSegment> filled;
	filled.reserve(m_segments.size() + 1);
	int cursor = spanBegin;
	for (const TakeLaneSegment& seg : m_segments)
	{
		if (seg.beginTick > cursor)
		{
			gap.beginTick = cursor;
			gap.endTick = seg.beginTick;
			filled.push_back(gap);
		}
		filled.push_back(seg);
		cursor = seg.endTick;
	}
	if (cursor < spanEnd)
	{
		gap.beginTick = cursor;
		gap.endTick = spanEnd;
		filled.push_back(gap);
	}
	m_segments.swap(filled);
	return static_cast<int>(m_segments.size());
}


int TakeLaneModel::rebuild(int spanBegin, int spanEnd)
{
	sortSegments();
	mergeSegments();
	if (m_lanes.empty())
	{
		m_segments.clear();
		return 0;
	}
	if (spanBegin < 0 || spanEnd <= spanBegin) { return static_cast<int>(m_segments.size()); }

	std::vector<TakeLaneSegment> clamped;
	clamped.reserve(m_segments.size());
	for (const TakeLaneSegment& seg : m_segments)
	{
		TakeLaneSegment kept = seg;
		if (kept.beginTick < spanBegin)
		{
			kept.sourceOffset += spanBegin - kept.beginTick;
			kept.beginTick = spanBegin;
		}
		if (kept.endTick > spanEnd) { kept.endTick = spanEnd; }
		if (kept.endTick > kept.beginTick) { clamped.push_back(kept); }
	}
	m_segments.swap(clamped);
	return fillGaps(spanBegin, spanEnd);
}


bool TakeLaneModel::span(int* beginTick, int* endTick) const
{
	if (m_segments.empty()) { return false; }
	if (beginTick != nullptr) { *beginTick = m_segments.front().beginTick; }
	if (endTick != nullptr) { *endTick = m_segments.back().endTick; }
	return true;
}


bool TakeLaneModel::resolve(int tick, int* laneIndex, int* sourceOffset) const
{
	for (const TakeLaneSegment& seg : m_segments)
	{
		if (tick < seg.beginTick) { break; }
		if (tick >= seg.endTick) { continue; }
		if (laneIndex != nullptr) { *laneIndex = seg.laneIndex; }
		if (sourceOffset != nullptr) { *sourceOffset = seg.sourceOffset + (tick - seg.beginTick); }
		return true;
	}
	return false;
}


bool TakeLaneModel::resolveSource(const std::vector<Clip*>& clips, int tick,
	f_cnt_t* sourceFrame, int* laneIndex) const
{
	Clip* clip = takeAt(clips, tick, laneIndex);
	if (clip == nullptr) { return false; }
	if (sourceFrame != nullptr) { *sourceFrame = clip->sourceFrameAt(TimePos(tick)); }
	return true;
}


Clip* TakeLaneModel::takeAt(const std::vector<Clip*>& clips, int tick, int* laneIndex) const
{
	int lane = 0;
	int offset = 0;
	if (!resolve(tick, &lane, &offset)) { return nullptr; }
	for (Clip* clip : clips)
	{
		if (clip == nullptr || clip->laneIndex() != lane) { continue; }
		if (tick < clip->startPosition().getTicks()) { continue; }
		if (tick >= clip->endPosition().getTicks()) { continue; }
		if (laneIndex != nullptr) { *laneIndex = lane; }
		return clip;
	}
	return nullptr;
}


void TakeLaneModel::clear()
{
	m_lanes.clear();
	m_segments.clear();
}


void TakeLaneModel::saveSettings(QDomDocument& doc, QDomElement& element) const
{
	for (const TakeLane& lane : m_lanes)
	{
		QDomElement laneElement = doc.createElement(QStringLiteral("lane"));
		laneElement.setAttribute(QStringLiteral("index"), lane.index);
		if (!lane.name.isEmpty()) { laneElement.setAttribute(QStringLiteral("name"), lane.name); }
		element.appendChild(laneElement);
	}
	for (const TakeLaneSegment& seg : m_segments)
	{
		QDomElement segElement = doc.createElement(QStringLiteral("segment"));
		segElement.setAttribute(QStringLiteral("begin"), seg.beginTick);
		segElement.setAttribute(QStringLiteral("end"), seg.endTick);
		segElement.setAttribute(QStringLiteral("lane"), seg.laneIndex);
		// Additive (I9): a comp with no slip writes no srcpos attribute.
		if (seg.sourceOffset != 0)
		{
			segElement.setAttribute(QStringLiteral("srcpos"), seg.sourceOffset);
		}
		element.appendChild(segElement);
	}
}


void TakeLaneModel::loadSettings(const QDomElement& element)
{
	// Reset FIRST. A track element that carries no <takelanes> child (or an old
	// build's file) must leave an empty model, not the previous one: a journal
	// checkpoint restores by re-loading, so a field that survives its own
	// absence could never be undone.
	clear();
	for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		const QDomElement child = node.toElement();
		if (child.isNull()) { continue; }
		if (child.tagName() == QLatin1String("lane"))
		{
			const TakeLane lane = laneFromElement(child);
			const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), lane.index,
				laneLessThan);
			m_lanes.insert(pos, lane);
			continue;
		}
		if (child.tagName() == QLatin1String("segment"))
		{
			const TakeLaneSegment seg = segmentFromElement(child);
			if (seg.endTick > seg.beginTick && hasLane(seg.laneIndex)) { m_segments.push_back(seg); }
		}
	}
	sortSegments();
	mergeSegments();
}

} // namespace lmms
