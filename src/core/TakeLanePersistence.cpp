/*
 * TakeLanePersistence.cpp - XML persistence for the take-lane model
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#include "TakeLane.h"

#include <algorithm>
#include <limits>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QLatin1String>

#include "AudioEngine.h"
#include "Engine.h"
#include "ProjectIds.h"

namespace lmms
{

namespace
{

AudioEngine::RequestChangesGuard changeLockForEdit()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->requestChangesGuard() : AudioEngine::RequestChangesGuard{};
}

bool laneIndexLessThan(const TakeLane& lane, int index) { return lane.index < index; }

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
			laneIndexLessThan);
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
