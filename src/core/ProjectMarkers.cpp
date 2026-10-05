/*
 * ProjectMarkers.cpp - the project's markers: named, coloured positions on the timeline
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

#include "ProjectMarkers.h"

#include <algorithm>
#include <limits>

#include <QDomDocument>
#include <QDomElement>
#include <QSet>

namespace lmms
{

namespace
{

//! A <marker>'s id, or -1 when it has none a marker can carry (absent, unreadable or negative).
int savedId(const QDomElement& element)
{
	bool ok = false;
	const int id = element.attribute(QStringLiteral("id")).toInt(&ok);
	return ok && id >= 0 ? id : -1;
}

//! The lowest id in 1..\a maxId no marker in \a taken holds. The marker count is
//! capped at maxId, so while a marker is still missing or duplicated there is
//! always a free id to hand it even when the ids above it have run out of
//! representable values - the caller only reaches this with a marker still
//! needing one.
int lowestFreeId(const QSet<int>& taken, int maxId)
{
	for (int id = 1; id <= maxId; ++id)
	{
		if (!taken.contains(id)) { return id; }
	}
	return -1;
}

//! A hand-edited file's ids made unique: one that is missing or already taken gets a fresh one after the rest,
//! so find, replace and remove each address exactly one marker.
void makeIdsUnique(QVector<Marker>& markers, int maxId)
{
	int next = 1;
	for (const Marker& marker : markers)
	{
		if (marker.id >= 0 && marker.id < std::numeric_limits<int>::max()) { next = std::max(next, marker.id + 1); }
	}
	QSet<int> taken;
	for (Marker& marker : markers)
	{
		if (marker.id >= 0 && !taken.contains(marker.id)) { taken.insert(marker.id); continue; }
		// The running counter has run out of representable values above the rest of
		// this file's ids, but leaving the duplicate as it is would leave one address
		// naming two markers, so drop to a free id below them instead.
		marker.id = next < std::numeric_limits<int>::max() ? next++ : lowestFreeId(taken, maxId);
		taken.insert(marker.id);
	}
}

} // namespace

ProjectMarkers& ProjectMarkers::instance()
{
	static ProjectMarkers* markers = [] {
		auto* made = new ProjectMarkers();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *markers;
}

const Marker* ProjectMarkers::find(int id) const
{
	const auto it = std::find_if(m_markers.begin(), m_markers.end(), [id](const Marker& m) { return m.id == id; });
	return it != m_markers.end() ? &*it : nullptr;
}

const Marker* ProjectMarkers::next(tick_t tick) const
{
	const auto it = std::find_if(m_markers.begin(), m_markers.end(), [tick](const Marker& m) { return m.tick > tick; });
	return it != m_markers.end() ? &*it : nullptr;
}

const Marker* ProjectMarkers::previous(tick_t tick) const
{
	const auto it = std::find_if(m_markers.rbegin(), m_markers.rend(), [tick](const Marker& m) { return m.tick < tick; });
	return it != m_markers.rend() ? &*it : nullptr;
}

int ProjectMarkers::add(tick_t tick, const QString& name, const QString& color)
{
	if (m_markers.size() >= MaxMarkers) { return -1; }
	// restore() can leave the counter at the top of its range after a project
	// loaded ids that fill everything above it, and incrementing there is a signed
	// overflow - the marker would come back with a negative id. Take a free id from
	// below instead. The count check above leaves at least one free id to find, so
	// the -1 above stays "there are MaxMarkers already" and nothing else changes.
	QSet<int> taken;
	for (const Marker& marker : m_markers) { taken.insert(marker.id); }
	const int id = m_nextId < std::numeric_limits<int>::max() ? m_nextId++ : lowestFreeId(taken, MaxMarkers);
	m_markers.append(Marker{id, std::max<tick_t>(0, tick), name, color});
	sortAndNotify();
	return id;
}

bool ProjectMarkers::remove(int id)
{
	const auto before = m_markers.size();
	m_markers.erase(std::remove_if(m_markers.begin(), m_markers.end(), [id](const Marker& m) { return m.id == id; }), m_markers.end());
	if (m_markers.size() == before) { return false; }
	sortAndNotify();
	return true;
}

bool ProjectMarkers::replace(int id, const Marker& marker)
{
	for (Marker& existing : m_markers)
	{
		if (existing.id != id) { continue; }
		existing = marker;
		existing.id = id;
		existing.tick = std::max<tick_t>(0, existing.tick);
		sortAndNotify();
		return true;
	}
	return false;
}

void ProjectMarkers::restore(const QVector<Marker>& markers)
{
	m_markers = markers;
	// An id a file gives as INT_MAX has no next one (the + 1 would overflow); it is kept, and skipped here.
	for (const Marker& marker : m_markers)
	{
		if (marker.id < std::numeric_limits<int>::max()) { m_nextId = std::max(m_nextId, marker.id + 1); }
	}
	sortAndNotify();
}

QString ProjectMarkers::elementName() const
{
	return QStringLiteral("markers");
}

void ProjectMarkers::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement list = doc.createElement(elementName());
	for (const Marker& marker : m_markers)
	{
		QDomElement element = doc.createElement(QStringLiteral("marker"));
		element.setAttribute(QStringLiteral("id"), marker.id);
		element.setAttribute(QStringLiteral("tick"), marker.tick);
		element.setAttribute(QStringLiteral("name"), marker.name);
		if (!marker.color.isEmpty()) { element.setAttribute(QStringLiteral("color"), marker.color); }
		if (!marker.action.isEmpty()) { element.setAttribute(QStringLiteral("action"), marker.action); }
		if (!marker.args.isEmpty()) { element.setAttribute(QStringLiteral("args"), marker.args); }
		list.appendChild(element);
	}
	parent.appendChild(list);
}

void ProjectMarkers::load(const QDomElement& element)
{
	QVector<Marker> loaded;
	for (QDomElement child = element.firstChildElement(QStringLiteral("marker")); !child.isNull() && loaded.size() < MaxMarkers;
		child = child.nextSiblingElement(QStringLiteral("marker")))
	{
		// A negative tick is clamped as add() and replace() clamp it.
		loaded.append(Marker{savedId(child), std::max(0, child.attribute(QStringLiteral("tick")).toInt()),
			child.attribute(QStringLiteral("name")), child.attribute(QStringLiteral("color")), child.attribute(QStringLiteral("action")),
			child.attribute(QStringLiteral("args"))});
	}
	makeIdsUnique(loaded, MaxMarkers);
	m_nextId = 1;
	restore(loaded);
}

void ProjectMarkers::clear()
{
	m_markers.clear();
	m_nextId = 1;
	emit changed();
}

void ProjectMarkers::sortAndNotify()
{
	std::stable_sort(m_markers.begin(), m_markers.end(),
		[](const Marker& a, const Marker& b) { return a.tick != b.tick ? a.tick < b.tick : a.id < b.id; });
	emit changed();
}

} // namespace lmms
