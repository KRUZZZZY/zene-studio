/*
 * ProjectInfo.cpp - a project's metadata and its document type
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

#include "ProjectInfo.h"

#include <QDomElement>

namespace lmms
{

namespace
{

ProjectInfo::ChangeHook s_hook = nullptr;

void announce(ProjectInfo::Change change)
{
	if (s_hook != nullptr) { s_hook(change); }
}

} // namespace

void ProjectInfo::setChangeHook(ChangeHook hook)
{
	s_hook = hook;
}

void ProjectInfo::announceCreated() const
{
	announce(Change::Created);
}

ProjectInfo::ProjectInfo() :
	m_type(documentTypes().first())
{
}

ProjectInfo& ProjectInfo::instance()
{
	static ProjectInfo* info = [] {
		auto* made = new ProjectInfo();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *info;
}

const QStringList& ProjectInfo::fieldNames()
{
	static const QStringList names{QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("genre"),
		QStringLiteral("year"), QStringLiteral("composer"), QStringLiteral("copyright"), QStringLiteral("comment")};
	return names;
}

const QStringList& ProjectInfo::documentTypes()
{
	static const QStringList types{QStringLiteral("production"), QStringLiteral("mastering"), QStringLiteral("live")};
	return types;
}

bool ProjectInfo::setField(const QString& name, const QString& value)
{
	if (!fieldNames().contains(name)) { return false; }
	if (value.isEmpty()) { m_fields.remove(name); }
	else { m_fields.insert(name, value.left(MaxFieldLength)); }
	return true;
}

bool ProjectInfo::setDocumentType(const QString& type)
{
	if (!documentTypes().contains(type)) { return false; }
	if (type != m_type)
	{
		m_type = type;
		announce(Change::Edited);
	}
	return true;
}

void ProjectInfo::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	if (m_type != documentTypes().first()) { element.setAttribute(QStringLiteral("type"), m_type); }
	// In the fixed field order, so the same metadata always writes the same bytes.
	for (const QString& name : fieldNames())
	{
		if (m_fields.contains(name)) { element.setAttribute(name, m_fields.value(name)); }
	}
	parent.appendChild(element);
}

void ProjectInfo::load(const QDomElement& element)
{
	clear();
	setDocumentType(element.attribute(QStringLiteral("type")));
	for (const QString& name : fieldNames()) { setField(name, element.attribute(name)); }
}

void ProjectInfo::clear()
{
	m_fields.clear();
	if (m_type != documentTypes().first())
	{
		m_type = documentTypes().first();
		announce(Change::Edited);
	}
}

} // namespace lmms
