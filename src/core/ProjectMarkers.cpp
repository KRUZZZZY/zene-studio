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

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

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
	const int id = m_nextId++;
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
	for (const Marker& marker : m_markers) { m_nextId = std::max(m_nextId, marker.id + 1); }
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
		loaded.append(Marker{child.attribute(QStringLiteral("id")).toInt(), child.attribute(QStringLiteral("tick")).toInt(),
			child.attribute(QStringLiteral("name")), child.attribute(QStringLiteral("color"))});
	}
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
