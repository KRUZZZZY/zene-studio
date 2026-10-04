/*
 * ProjectWorkspace.cpp - the workflows and the window layout a project keeps of its own
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

#include "ProjectWorkspace.h"

#include <QDomDocument>
#include <QDomElement>
#include <QJsonDocument>

namespace lmms
{

namespace
{

const QString WorkflowsTag = QStringLiteral("workflows");
const QString LayoutTag = QStringLiteral("layout");

//! <@a tag data="{json}"/> under @a root, only when @a value holds something.
void appendPart(QDomDocument& doc, QDomElement& root, const QString& tag, const QJsonObject& value)
{
	if (value.isEmpty()) { return; }
	QDomElement element = doc.createElement(tag);
	element.setAttribute(QStringLiteral("data"), QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
	root.appendChild(element);
}

//! The object under <@a tag>, or an empty one when there is none or it does not parse.
QJsonObject readPart(const QDomElement& root, const QString& tag)
{
	const QDomElement element = root.firstChildElement(tag);
	return element.isNull() ? QJsonObject() : QJsonDocument::fromJson(element.attribute(QStringLiteral("data")).toUtf8()).object();
}

} // namespace

ProjectWorkspace& ProjectWorkspace::instance()
{
	static ProjectWorkspace* workspace = [] {
		auto* made = new ProjectWorkspace();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *workspace;
}

void ProjectWorkspace::setPart(Part part, const QJsonObject& value)
{
	(part == Part::Workflows ? m_workflows : m_layout) = value;
}

QString ProjectWorkspace::elementName() const
{
	return QStringLiteral("workspace");
}

bool ProjectWorkspace::shouldPersist() const
{
	return !m_workflows.isEmpty() || !m_layout.isEmpty();
}

void ProjectWorkspace::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement root = doc.createElement(elementName());
	appendPart(doc, root, WorkflowsTag, m_workflows);
	appendPart(doc, root, LayoutTag, m_layout);
	parent.appendChild(root);
}

void ProjectWorkspace::load(const QDomElement& element)
{
	m_workflows = readPart(element, WorkflowsTag);
	m_layout = readPart(element, LayoutTag);
}

void ProjectWorkspace::clear()
{
	m_workflows = QJsonObject();
	m_layout = QJsonObject();
}

} // namespace lmms
