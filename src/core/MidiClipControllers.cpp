/*
 * MidiClipControllers.cpp - a MIDI clip's controller lanes: CC, pitch bend and channel pressure curves
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

#include "MidiClipControllers.h"

#include <algorithm>
#include <cmath>

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace
{

const QString PitchBendId = QStringLiteral("pitchbend");
const QString PressureId = QStringLiteral("aftertouch");
const QString CcPrefix = QStringLiteral("cc:");
const QString ProgramId = QStringLiteral("program");

} // namespace

QString ControllerLane::id() const
{
	switch (kind)
	{
	case ControllerKind::PitchBend: return PitchBendId;
	case ControllerKind::ChannelPressure: return PressureId;
	case ControllerKind::ProgramChange: return ProgramId;
	case ControllerKind::ControlChange: break;
	}
	return CcPrefix + QString::number(controller);
}

int ControllerLane::maximum() const
{
	return kind == ControllerKind::PitchBend ? 16383 : 127;
}

int ControllerLane::valueAt(tick_t tick) const
{
	if (points.empty()) { return 0; }
	const auto after = std::upper_bound(points.begin(), points.end(), tick,
		[](tick_t t, const ControllerPoint& p) { return t < p.tick; });
	if (after == points.begin()) { return points.front().value; }
	if (after == points.end() || kind == ControllerKind::ProgramChange) { return std::prev(after)->value; }
	const ControllerPoint& a = *std::prev(after);
	const ControllerPoint& b = *after;
	const double t = static_cast<double>(tick - a.tick) / (b.tick - a.tick);
	return static_cast<int>(std::lround(a.value + (b.value - a.value) * t));
}

bool ControllerLane::fromId(const QString& id, ControllerKind* kind, int* controller)
{
	*controller = 0;
	if (id == PitchBendId) { *kind = ControllerKind::PitchBend; return true; }
	if (id == PressureId) { *kind = ControllerKind::ChannelPressure; return true; }
	if (id == ProgramId) { *kind = ControllerKind::ProgramChange; return true; }
	if (!id.startsWith(CcPrefix)) { return false; }
	bool ok = false;
	const int number = id.mid(CcPrefix.size()).toInt(&ok);
	if (!ok || number < 0 || number > 127) { return false; }
	*kind = ControllerKind::ControlChange;
	*controller = number;
	return true;
}

const ControllerLane* MidiClipControllers::lane(ControllerKind kind, int controller) const
{
	for (const ControllerLane& each : m_lanes)
	{
		if (each.kind == kind && (kind != ControllerKind::ControlChange || each.controller == controller)) { return &each; }
	}
	return nullptr;
}

void MidiClipControllers::setPoints(ControllerKind kind, int controller, std::vector<ControllerPoint> points)
{
	std::stable_sort(points.begin(), points.end(), [](const ControllerPoint& a, const ControllerPoint& b) { return a.tick < b.tick; });
	// One point per tick: the last one given for a tick wins.
	std::vector<ControllerPoint> unique;
	for (const ControllerPoint& point : points)
	{
		if (!unique.empty() && unique.back().tick == point.tick) { unique.back() = point; }
		else { unique.push_back(point); }
	}
	const int number = kind == ControllerKind::ControlChange ? controller : 0;
	const auto it = std::find_if(m_lanes.begin(), m_lanes.end(),
		[kind, number](const ControllerLane& each) { return each.kind == kind && each.controller == number; });
	if (unique.empty())
	{
		if (it != m_lanes.end()) { m_lanes.erase(it); }
		return;
	}
	if (it != m_lanes.end())
	{
		it->points = std::move(unique);
		it->lastTick = -2;  // send again from wherever the clip is
		return;
	}
	ControllerLane lane;
	lane.kind = kind;
	lane.controller = number;
	lane.points = std::move(unique);
	m_lanes.push_back(std::move(lane));
}

void MidiClipControllers::save(QDomDocument& doc, QDomElement& clipElement) const
{
	if (m_lanes.empty()) { return; }
	QDomElement root = doc.createElement(QStringLiteral("controllers"));
	root.setAttribute(QStringLiteral("metadata"), 1);
	for (const ControllerLane& lane : m_lanes)
	{
		QDomElement laneElement = doc.createElement(QStringLiteral("lane"));
		laneElement.setAttribute(QStringLiteral("id"), lane.id());
		for (const ControllerPoint& point : lane.points)
		{
			QDomElement p = doc.createElement(QStringLiteral("p"));
			p.setAttribute(QStringLiteral("t"), point.tick);
			p.setAttribute(QStringLiteral("v"), point.value);
			laneElement.appendChild(p);
		}
		root.appendChild(laneElement);
	}
	clipElement.appendChild(root);
}

void MidiClipControllers::load(const QDomElement& clipElement)
{
	m_lanes.clear();
	const QDomElement root = clipElement.firstChildElement(QStringLiteral("controllers"));
	for (QDomElement laneElement = root.firstChildElement(QStringLiteral("lane")); !laneElement.isNull();
		laneElement = laneElement.nextSiblingElement(QStringLiteral("lane")))
	{
		ControllerKind kind = ControllerKind::ControlChange;
		int controller = 0;
		if (!ControllerLane::fromId(laneElement.attribute(QStringLiteral("id")), &kind, &controller)) { continue; }
		const int top = kind == ControllerKind::PitchBend ? 16383 : 127;
		std::vector<ControllerPoint> points;
		for (QDomElement p = laneElement.firstChildElement(QStringLiteral("p")); !p.isNull(); p = p.nextSiblingElement(QStringLiteral("p")))
		{
			points.push_back({std::max(0, p.attribute(QStringLiteral("t")).toInt()), std::clamp(p.attribute(QStringLiteral("v")).toInt(), 0, top)});
		}
		setPoints(kind, controller, std::move(points));
	}
}

} // namespace lmms
