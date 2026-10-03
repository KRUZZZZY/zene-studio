/*
 * ArrangementSections.cpp - the song's named sections as spans of time
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
#include "ArrangementSections.h"

#include <algorithm>

#include <QDomElement>

namespace lmms
{

ArrangementSections& ArrangementSections::instance()
{
	static ArrangementSections* sections = [] {
		auto* made = new ArrangementSections();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *sections;
}

const ArrangementSection* ArrangementSections::find(int id) const
{
	const auto it = std::find_if(m_sections.begin(), m_sections.end(), [id](const ArrangementSection& s) { return s.id == id; });
	return it != m_sections.end() ? &*it : nullptr;
}

const ArrangementSection* ArrangementSections::at(tick_t tick) const
{
	const auto it = std::find_if(m_sections.begin(), m_sections.end(), [tick](const ArrangementSection& s) { return tick >= s.start && tick < s.end; });
	return it != m_sections.end() ? &*it : nullptr;
}

int ArrangementSections::add(tick_t start, tick_t end, const QString& name, const QString& color)
{
	if (m_sections.size() >= MaxSections || end <= start || start < 0) { return -1; }
	const int id = m_nextId++;
	m_sections.append(ArrangementSection{id, start, end, name, color});
	sort();
	return id;
}

bool ArrangementSections::replace(int id, const ArrangementSection& section)
{
	for (ArrangementSection& each : m_sections)
	{
		if (each.id != id) { continue; }
		each = section;
		each.id = id;
		sort();
		return true;
	}
	return false;
}

bool ArrangementSections::remove(int id)
{
	const auto before = m_sections.size();
	m_sections.erase(std::remove_if(m_sections.begin(), m_sections.end(), [id](const ArrangementSection& s) { return s.id == id; }), m_sections.end());
	return m_sections.size() != before;
}

void ArrangementSections::restore(const QVector<ArrangementSection>& sections)
{
	m_sections = sections;
	for (const ArrangementSection& each : m_sections) { m_nextId = std::max(m_nextId, each.id + 1); }
	sort();
}

void ArrangementSections::sort()
{
	std::stable_sort(m_sections.begin(), m_sections.end(), [](const ArrangementSection& a, const ArrangementSection& b) {
		return a.start != b.start ? a.start < b.start : a.id < b.id;
	});
}

void ArrangementSections::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement list = doc.createElement(elementName());
	for (const ArrangementSection& section : m_sections)
	{
		QDomElement element = doc.createElement(QStringLiteral("section"));
		element.setAttribute(QStringLiteral("id"), section.id);
		element.setAttribute(QStringLiteral("start"), section.start);
		element.setAttribute(QStringLiteral("end"), section.end);
		element.setAttribute(QStringLiteral("name"), section.name);
		if (!section.color.isEmpty()) { element.setAttribute(QStringLiteral("color"), section.color); }
		list.appendChild(element);
	}
	parent.appendChild(list);
}

void ArrangementSections::load(const QDomElement& element)
{
	QVector<ArrangementSection> loaded;
	for (QDomElement child = element.firstChildElement(QStringLiteral("section")); !child.isNull() && loaded.size() < MaxSections;
		child = child.nextSiblingElement(QStringLiteral("section")))
	{
		const ArrangementSection section{child.attribute(QStringLiteral("id")).toInt(), child.attribute(QStringLiteral("start")).toInt(),
			child.attribute(QStringLiteral("end")).toInt(), child.attribute(QStringLiteral("name")), child.attribute(QStringLiteral("color"))};
		if (section.end > section.start) { loaded.append(section); }
	}
	m_nextId = 1;
	restore(loaded);
}

void ArrangementSections::clear()
{
	m_sections.clear();
	m_nextId = 1;
}

} // namespace lmms
