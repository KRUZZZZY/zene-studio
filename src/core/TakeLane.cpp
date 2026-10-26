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
#include <cstdint>
#include <limits>
#include <unordered_map>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QLatin1String>

#include <limits>

#include "AudioEngine.h"
#include "Clip.h"
#include "Engine.h"
#include "ProjectIds.h"
#include "TimePos.h"

namespace lmms
{

namespace
{

//! The engine's change lock for one lane edit. Render reads the composite under the same lock, so an
//! edit made from the UI or the socket must hold it. Without an engine there is nothing to race.
AudioEngine::RequestChangesGuard changeLockForEdit()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->requestChangesGuard() : AudioEngine::RequestChangesGuard{};
}

} // namespace


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
 *  ended, takes the same lane, and reads that lane's take at the same SLIP
 *  (TakeLaneSegment, 0.5.0 E2) - one continuous read. A different slip is a
 *  jump in the take and is NOT merged away. */
bool continuesTake(const TakeLaneSegment& previous, const TakeLaneSegment& seg)
{
	if (previous.endTick != seg.beginTick) { return false; }
	if (previous.laneIndex != seg.laneIndex) { return false; }
	return seg.sourceOffset == previous.sourceOffset;
}

//! Merges each segment that continues the one before it (continuesTake) into that one span.
void mergeAdjacentSegments(std::vector<TakeLaneSegment>* segments)
{
	std::vector<TakeLaneSegment> merged;
	merged.reserve(segments->size());
	for (const TakeLaneSegment& seg : *segments)
	{
		if (!merged.empty() && continuesTake(merged.back(), seg))
		{
			merged.back().endTick = seg.endTick;
			continue;
		}
		merged.push_back(seg);
	}
	segments->swap(merged);
}

//! The composite once lane \p index is removed: its segments fall back to the lowest lane that remains, at
//! offset 0 (the base lane's take has its own start). Empty when no lane remains.
std::vector<TakeLaneSegment> segmentsAfterRemovingLane(const std::vector<TakeLane>& lanes,
	const std::vector<TakeLaneSegment>& segments, int index)
{
	int base = -1;
	for (const TakeLane& lane : lanes)
	{
		if (lane.index != index) { base = lane.index; break; }
	}
	if (base < 0) { return {}; }
	std::vector<TakeLaneSegment> remapped = segments;
	for (TakeLaneSegment& seg : remapped)
	{
		if (seg.laneIndex != index) { continue; }
		seg.laneIndex = base;
		seg.sourceOffset = 0;
	}
	mergeAdjacentSegments(&remapped);
	return remapped;
}

//! True when no lane holds more spans than a play handle can snapshot (TakeLaneModel::MaxCompositeSpansPerLane).
bool withinPlaybackSpanBudget(const std::vector<TakeLaneSegment>& segments)
{
	std::unordered_map<int, int> spanCounts;
	for (const TakeLaneSegment& seg : segments)
	{
		if (++spanCounts[seg.laneIndex] > TakeLaneModel::MaxCompositeSpansPerLane) { return false; }
	}
	return true;
}

bool addSlipToTick(int tick, int slip, tick_t* sourceTick)
{
	const std::int64_t slipped = static_cast<std::int64_t>(tick) + slip;
	if (slipped < std::numeric_limits<tick_t>::min()
		|| slipped > std::numeric_limits<tick_t>::max()) { return false; }
	if (sourceTick != nullptr) { *sourceTick = static_cast<tick_t>(slipped); }
	return true;
}

bool containsTake(int groupId, int laneIndex, const std::vector<Clip*>& clips)
{
	for (const Clip* clip : clips)
	{
		if (clip != nullptr && clip->belongsToTakeGroup(groupId) && clip->laneIndex() == laneIndex) { return true; }
	}
	return false;
}


int newestTakeLane(int groupId, const std::vector<Clip*>& clips)
{
	const Clip* newest = nullptr;
	for (const Clip* clip : clips)
	{
		if (clip == nullptr || !clip->belongsToTakeGroup(groupId)) { continue; }
		if (newest == nullptr || clip->id() > newest->id()) { newest = clip; }
	}
	return newest == nullptr ? -1 : newest->laneIndex();
}


bool isTakeGroupElement(const QDomElement& element)
{
	return element.tagName() == QLatin1String("z:takegroup")
		|| element.tagName() == QLatin1String("takegroup");
}

int nextLaneAfter(int laneIndex)
{
	return laneIndex == std::numeric_limits<int>::max() ? laneIndex : laneIndex + 1;
}

int nextLaneIndexOf(const QDomElement& element)
{
	bool ok = false;
	const int nextLane = element.attribute(QStringLiteral("nextlane")).toInt(&ok);
	return ok && nextLane >= 0 ? nextLane : 0;
}

TakeLane laneFromElement(const QDomElement& element)
{
	TakeLane lane;
	lane.index = std::max(0, element.attribute(QStringLiteral("index"), QStringLiteral("0")).toInt());
	lane.name = element.attribute(QStringLiteral("name"));
	// The ENTITY id rides the clip-id rule (SPEC-ARCH-4 1.5 R4/R3): take the
	// document's value and raise the counter above it, or allocate one and
	// count it as assigned when the document carries no id (a pre-S7 payload).
	bool ok = false;
	const int id = element.attribute(QStringLiteral("id")).toInt(&ok);
	if (ok && id >= 0)
	{
		lane.id = id;
		ProjectIds::observe(id);
	}
	else
	{
		lane.id = ProjectIds::allocate();
		ProjectIds::noteLoadAssignment(lane.id);
	}
	return lane;
}

bool readNonNegativeInt(const QDomElement& element, const QString& name, int* value,
	bool optional = false)
{
	if (!element.hasAttribute(name))
	{
		if (!optional) { return false; }
		*value = 0;
		return true;
	}
	bool ok = false;
	const int parsed = element.attribute(name).toInt(&ok);
	if (!ok || parsed < 0) { return false; }
	*value = parsed;
	return true;
}

bool segmentFromElement(const QDomElement& element, TakeLaneSegment* segment)
{
	// 0.5.0 E2: the slip is `slip`. A pre-0.5.0 `srcpos` is ignored on purpose - it held the old, contradictory
	// reading and no playback path ever applied it, so dropping it keeps an old project sounding as it did.
	return readNonNegativeInt(element, QStringLiteral("begin"), &segment->beginTick)
		&& readNonNegativeInt(element, QStringLiteral("end"), &segment->endTick)
		&& readNonNegativeInt(element, QStringLiteral("lane"), &segment->laneIndex)
		&& readNonNegativeInt(element, QStringLiteral("slip"), &segment->sourceOffset, true);
}

} // namespace


void TakeLaneModel::setAuditionLane(int lane)
{
	const auto changeLock = changeLockForEdit();
	m_auditionLane = lane;
}


int TakeLaneModel::activeTakeLane(int groupId) const
{
	const auto it = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& group, int id) { return group.groupId < id; });
	return it != m_takeGroups.end() && it->groupId == groupId ? it->activeLane : -1;
}


bool TakeLaneModel::userChoseTake(int groupId) const
{
	const auto it = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& group, int id) { return group.groupId < id; });
	return it != m_takeGroups.end() && it->groupId == groupId && it->userChosen;
}


bool TakeLaneModel::canSelectTake(int groupId, int laneIndex, const std::vector<Clip*>& clips) const
{
	if (activeTakeLane(groupId) < 0 || laneIndex < 0) { return false; }
	for (const Clip* clip : clips)
	{
		if (clip != nullptr && clip->belongsToTakeGroup(groupId) && clip->laneIndex() == laneIndex) { return true; }
	}
	return false;
}


void TakeLaneModel::publishTakeGroup(int groupId, int activeLane, const std::vector<Clip*>& clips) const
{
	for (Clip* clip : clips)
	{
		if (clip != nullptr && clip->belongsToTakeGroup(groupId)) { clip->publishActiveTakeLane(activeLane); }
	}
}


bool TakeLaneModel::recordTake(int groupId, int laneIndex, const std::vector<Clip*>& clips, bool overrideChoice)
{
	const auto changeLock = changeLockForEdit();
	if (groupId < 0 || laneIndex < 0 || !containsTake(groupId, laneIndex, clips)) { return false; }

	auto it = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& group, int id) { return group.groupId < id; });
	if (it == m_takeGroups.end() || it->groupId != groupId)
	{
		it = m_takeGroups.insert(it, TakeGroupSelection{groupId, laneIndex, false});
	}
	else if (!it->userChosen || overrideChoice)
	{
		it->activeLane = laneIndex;
		it->userChosen = false;
	}
	publishTakeGroup(groupId, it->activeLane, clips);
	return true;
}


bool TakeLaneModel::selectTake(int groupId, int laneIndex, const std::vector<Clip*>& clips)
{
	const auto changeLock = changeLockForEdit();
	if (!canSelectTake(groupId, laneIndex, clips)) { return false; }
	auto it = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& group, int id) { return group.groupId < id; });
	it->activeLane = laneIndex;
	it->userChosen = true;
	publishTakeGroup(groupId, laneIndex, clips);
	return true;
}


void TakeLaneModel::removeTakeClip(int groupId, int laneIndex, const std::vector<Clip*>& clips)
{
	const auto changeLock = changeLockForEdit();
	auto it = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& group, int id) { return group.groupId < id; });
	if (it == m_takeGroups.end() || it->groupId != groupId) { return; }
	const int newestLane = newestTakeLane(groupId, clips);
	if (newestLane < 0)
	{
		m_takeGroups.erase(it);
		return;
	}
	if (it->activeLane == laneIndex && !containsTake(groupId, laneIndex, clips))
	{
		it->activeLane = newestLane;
		it->userChosen = false; // a deleted choice cannot remain the group's active take
	}
	publishTakeGroup(groupId, it->activeLane, clips);
}


void TakeLaneModel::synchronizeTakePlayback(const std::vector<Clip*>& clips) const
{
	for (Clip* clip : clips)
	{
		if (clip == nullptr) { continue; }
		clip->publishActiveTakeLane(clip->hasTakeGroup() ? activeTakeLane(clip->takeGroupId()) : -1);
	}
}


void TakeLaneModel::setPlayAllTakes(bool enabled)
{
	const auto changeLock = changeLockForEdit();
	m_playAllTakes.store(enabled, std::memory_order_relaxed);
}

int TakeLaneModel::addLane(const QString& name)
{
	const auto changeLock = changeLockForEdit();
	// Clips keep their lane tag when a lane is removed. Never recycle an index:
	// a later recorded pass must not inherit the identity of a still-live clip.
	if (m_nextLaneIndex == std::numeric_limits<int>::max()) { return -1; }
	const int index = m_nextLaneIndex++;
	TakeLane added;
	added.index = index;
	added.name = name;
	// The entity id is allocated with the lane, exactly as the Track and Clip
	// constructors allocate theirs (SPEC-ARCH-4 1.5 R4, the lane- family S3
	// declared for S7).
	added.id = ProjectIds::allocate();
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
	const auto changeLock = changeLockForEdit();
	const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), index, laneLessThan);
	if (pos == m_lanes.end() || pos->index != index) { return false; }
	// A composite stays total over its span (I6): the range a removed lane
	// supplied falls back to the base lane rather than becoming a hole (see
	// segmentsAfterRemovingLane). A result over the playback span budget is refused.
	std::vector<TakeLaneSegment> remapped = segmentsAfterRemovingLane(m_lanes, m_segments, index);
	if (!withinPlaybackSpanBudget(remapped)) { return false; }
	m_lanes.erase(pos);
	m_segments.swap(remapped);
	return true;
}


bool TakeLaneModel::canRemoveLane(int index) const
{
	return hasLane(index) && withinPlaybackSpanBudget(segmentsAfterRemovingLane(m_lanes, m_segments, index));
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
		TakeLaneSegment right = seg;  // the slip is the segment's, not its start's: it is kept
		right.beginTick = endTick;
		out->push_back(right);
	}
}


bool TakeLaneModel::canSelect(int beginTick, int endTick, int laneIndex, int sourceOffset) const
{
	if (beginTick < 0 || endTick <= beginTick || sourceOffset < 0) { return false; }
	if (!addSlipToTick(endTick - 1, sourceOffset, nullptr)) { return false; }
	if (!hasLane(laneIndex)) { return false; }
	// Judged on the composite the selection would leave, normalised the way selectSegment() normalises it.
	std::vector<TakeLaneSegment> painted;
	painted.reserve(m_segments.size() + 1);
	for (const TakeLaneSegment& seg : m_segments) { subtractRange(seg, beginTick, endTick, &painted); }
	TakeLaneSegment chosen;
	chosen.beginTick = beginTick;
	chosen.endTick = endTick;
	chosen.laneIndex = laneIndex;
	chosen.sourceOffset = sourceOffset;
	painted.push_back(chosen);
	std::sort(painted.begin(), painted.end(), segmentBefore);
	mergeAdjacentSegments(&painted);
	return withinPlaybackSpanBudget(painted);
}


bool TakeLaneModel::selectSegment(int beginTick, int endTick, int laneIndex, int sourceOffset)
{
	const auto changeLock = changeLockForEdit();
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
	mergeAdjacentSegments(&m_segments);
}


int TakeLaneModel::fillGaps(int spanBegin, int spanEnd)
{
	const auto changeLock = changeLockForEdit();
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
	const auto changeLock = changeLockForEdit();
	const std::vector<TakeLaneSegment> original = m_segments;
	sortSegments();
	mergeSegments();
	if (m_lanes.empty())
	{
		m_segments.clear();
		return 0;
	}
	if (spanBegin < 0 || spanEnd <= spanBegin)
	{
		if (withinPlaybackSpanBudget(m_segments)) { return static_cast<int>(m_segments.size()); }
		m_segments = original;
		return -1;
	}

	std::vector<TakeLaneSegment> clamped;
	clamped.reserve(m_segments.size());
	for (const TakeLaneSegment& seg : m_segments)
	{
		TakeLaneSegment kept = seg;
		if (kept.beginTick < spanBegin) { kept.beginTick = spanBegin; }  // the slip is kept (E2)
		if (kept.endTick > spanEnd) { kept.endTick = spanEnd; }
		if (kept.endTick > kept.beginTick) { clamped.push_back(kept); }
	}
	m_segments.swap(clamped);
	const int result = fillGaps(spanBegin, spanEnd);
	if (withinPlaybackSpanBudget(m_segments)) { return result; }
	m_segments = original;
	return -1;
}


bool TakeLaneModel::canRebuild(int spanBegin, int spanEnd) const
{
	TakeLaneModel preview;
	preview.m_lanes = m_lanes;
	preview.m_segments = m_segments;
	return preview.rebuild(spanBegin, spanEnd) >= 0;
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
		if (sourceOffset != nullptr) { *sourceOffset = seg.sourceOffset; }
		return true;
	}
	return false;
}


bool TakeLaneModel::resolveSource(const std::vector<Clip*>& clips, int tick,
	f_cnt_t* sourceFrame, int* laneIndex) const
{
	int slip = 0;
	if (!resolve(tick, nullptr, &slip)) { return false; }
	tick_t sourceTick = 0;
	if (!addSlipToTick(tick, slip, &sourceTick)) { return false; }
	Clip* clip = takeAt(clips, tick, laneIndex);
	if (clip == nullptr) { return false; }
	if (sourceFrame != nullptr) { *sourceFrame = clip->sourceFrameAt(TimePos(sourceTick)); }
	return true;
}


Clip* TakeLaneModel::takeAt(const std::vector<Clip*>& clips, int tick, int* laneIndex) const
{
	int lane = 0;
	int slip = 0;
	if (!resolve(tick, &lane, &slip)) { return nullptr; }
	tick_t sourceTick = 0;
	if (!addSlipToTick(tick, slip, &sourceTick)) { return nullptr; }
	for (Clip* clip : clips)
	{
		if (clip == nullptr || clip->laneIndex() != lane) { continue; }
		// The take's handle plays where the clip is (tick), and reads it slip ticks later (E2).
		if (tick < clip->startPosition().getTicks()) { continue; }
		if (sourceTick >= clip->endPosition().getTicks()) { continue; }
		if (laneIndex != nullptr) { *laneIndex = lane; }
		return clip;
	}
	return nullptr;
}


void TakeLaneModel::clear()
{
	const auto changeLock = changeLockForEdit();
	m_lanes.clear();
	m_segments.clear();
	m_takeGroups.clear();
	m_nextLaneIndex = 0;
	m_playAllTakes.store(false, std::memory_order_relaxed);
	m_auditionLane = -1;
}


void TakeLaneModel::saveSettings(QDomDocument& doc, QDomElement& element) const
{
	if (m_nextLaneIndex > 0) { element.setAttribute(QStringLiteral("nextlane"), m_nextLaneIndex); }
	if (playAllTakes()) { element.setAttribute(QStringLiteral("playall"), 1); }
	for (const TakeLane& lane : m_lanes)
	{
		// The ARCH-4 S7 vocabulary: the ENTITY spelling, one for both contexts
		// (top-level <z:lanes> section, or a checkpoint payload's <takelanes>).
		QDomElement laneElement = doc.createElement(QStringLiteral("z:lane"));
		laneElement.setAttribute(QStringLiteral("id"), lane.id);
		laneElement.setAttribute(QStringLiteral("index"), lane.index);
		if (!lane.name.isEmpty()) { laneElement.setAttribute(QStringLiteral("name"), lane.name); }
		element.appendChild(laneElement);
	}
	for (const TakeGroupSelection& group : m_takeGroups)
	{
		QDomElement groupElement = doc.createElement(QStringLiteral("z:takegroup"));
		groupElement.setAttribute(QStringLiteral("group"), group.groupId);
		groupElement.setAttribute(QStringLiteral("active"), group.activeLane);
		if (group.userChosen) { groupElement.setAttribute(QStringLiteral("user"), 1); }
		element.appendChild(groupElement);
	}
	for (const TakeLaneSegment& seg : m_segments)
	{
		QDomElement segElement = doc.createElement(QStringLiteral("z:segment"));
		segElement.setAttribute(QStringLiteral("begin"), seg.beginTick);
		segElement.setAttribute(QStringLiteral("end"), seg.endTick);
		segElement.setAttribute(QStringLiteral("lane"), seg.laneIndex);
		// Additive (I9): a comp with no slip writes no slip attribute (0.5.0 E2: `slip`, not the old `srcpos`).
		if (seg.sourceOffset != 0) { segElement.setAttribute(QStringLiteral("slip"), seg.sourceOffset); }
		element.appendChild(segElement);
	}
}


void TakeLaneModel::loadSettings(const QDomElement& element)
{
	const auto changeLock = changeLockForEdit();
	// Reset FIRST. A track element that carries no <takelanes> child (or an old
	// build's file) must leave an empty model, not the previous one: a journal
	// checkpoint restores by re-loading, so a field that survives its own
	// absence could never be undone.
	clear();
	m_nextLaneIndex = nextLaneIndexOf(element);
	m_playAllTakes.store(element.attribute(QStringLiteral("playall"), QStringLiteral("0")).toInt() != 0,
		std::memory_order_relaxed);
	for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		loadSettingsChild(node.toElement());
	}
	sortSegments();
	mergeSegments();
}


void TakeLaneModel::loadSettingsChild(const QDomElement& child)
{
	if (child.isNull()) { return; }
	// Read-both spellings (S1's rule, ARCH-4 S7): the `z:`-prefixed one this
	// build writes in either context, and the bare pre-S7 one a payload or a
	// hand-written file may still carry.
	if (child.tagName() == QLatin1String("z:lane") || child.tagName() == QLatin1String("lane"))
	{
		const TakeLane lane = laneFromElement(child);
		m_nextLaneIndex = std::max(m_nextLaneIndex, nextLaneAfter(lane.index));
		const auto pos = std::lower_bound(m_lanes.begin(), m_lanes.end(), lane.index,
			laneLessThan);
		m_lanes.insert(pos, lane);
		return;
	}
	if (isTakeGroupElement(child))
	{
		loadTakeGroup(child);
		return;
	}
	if (child.tagName() == QLatin1String("z:segment")
		|| child.tagName() == QLatin1String("segment"))
	{
		TakeLaneSegment seg;
		if (segmentFromElement(child, &seg) && seg.endTick > seg.beginTick
			&& hasLane(seg.laneIndex)) { m_segments.push_back(seg); }
	}
}


void TakeLaneModel::loadTakeGroup(const QDomElement& element)
{
	bool groupOk = false;
	bool activeOk = false;
	const int groupId = element.attribute(QStringLiteral("group")).toInt(&groupOk);
	const int activeLane = element.attribute(QStringLiteral("active")).toInt(&activeOk);
	if (!groupOk || !activeOk || groupId < 0 || activeLane < 0) { return; }
	TakeGroupSelection group{groupId, activeLane,
		element.attribute(QStringLiteral("user"), QStringLiteral("0")).toInt() != 0};
	auto pos = std::lower_bound(m_takeGroups.begin(), m_takeGroups.end(), groupId,
		[](const TakeGroupSelection& existing, int id) { return existing.groupId < id; });
	if (pos != m_takeGroups.end() && pos->groupId == groupId) { *pos = group; }
	else { m_takeGroups.insert(pos, group); }
}

} // namespace lmms
