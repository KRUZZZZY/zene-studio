/*
 * ViewMemory.cpp - memory locations and saved selections, kept with the project
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

#include "ViewMemory.h"

#include <QDomDocument>
#include <QDomElement>
#include <QJsonDocument>
#include <QJsonObject>

namespace lmms
{

namespace
{

const QString LocationTag = QStringLiteral("location");
const QString SelectionTag = QStringLiteral("selection");

void appendEntries(QDomDocument& doc, QDomElement& root, const QString& tag, const QJsonArray& entries)
{
	for (const QJsonValue& entry : entries)
	{
		QDomElement element = doc.createElement(tag);
		element.setAttribute(QStringLiteral("data"), QString::fromUtf8(QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact)));
		root.appendChild(element);
	}
}

QJsonArray readEntries(const QDomElement& root, const QString& tag)
{
	QJsonArray out;
	for (QDomElement element = root.firstChildElement(tag); !element.isNull(); element = element.nextSiblingElement(tag))
	{
		const QJsonObject entry = QJsonDocument::fromJson(element.attribute(QStringLiteral("data")).toUtf8()).object();
		if (!entry.value(QStringLiteral("name")).toString().isEmpty()) { out.append(entry); }
	}
	return out;
}

} // namespace

ViewMemory& ViewMemory::instance()
{
	static ViewMemory* memory = [] {
		auto* made = new ViewMemory();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *memory;
}

void ViewMemory::setEntries(Kind kind, const QJsonArray& entries)
{
	(kind == Kind::Location ? m_locations : m_selections) = entries;
}

QJsonObject ViewMemory::entry(Kind kind, const QString& name) const
{
	for (const QJsonValue& value : entries(kind))
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject(); }
	}
	return {};
}

QString ViewMemory::elementName() const
{
	return QStringLiteral("view-memory");
}

bool ViewMemory::shouldPersist() const
{
	return !m_locations.isEmpty() || !m_selections.isEmpty();
}

void ViewMemory::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement root = doc.createElement(elementName());
	appendEntries(doc, root, LocationTag, m_locations);
	appendEntries(doc, root, SelectionTag, m_selections);
	parent.appendChild(root);
}

void ViewMemory::load(const QDomElement& element)
{
	m_locations = readEntries(element, LocationTag);
	m_selections = readEntries(element, SelectionTag);
}

void ViewMemory::clear()
{
	m_locations = QJsonArray();
	m_selections = QJsonArray();
}

} // namespace lmms
