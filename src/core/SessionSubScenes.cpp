/*
 * SessionSubScenes.cpp - the launcher's sub-scenes in a project file: the <z:subscenes> section
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

#include "SessionSubScenes.h"

#include <QDomDocument>
#include <QDomElement>
#include <QTextStream>

#include "DocumentIndex.h"
#include "Engine.h"
#include "SessionModel.h"
#include "Song.h"

namespace lmms
{

namespace
{

const QString RowName = QStringLiteral("z:subscene");

SessionModel* model()
{
	return Engine::getSong() != nullptr ? &Engine::getSong()->sessionModel() : nullptr;
}

QString asText(const QDomElement& element)
{
	QString out;
	QTextStream stream(&out);
	element.save(stream, 1);
	return out;
}

//! Every row of a v1 section into @a out; false at the first child that is not a well-formed row, or that is content no row
//! carries (text, CDATA, a processing instruction), so the section is kept as it came. Comments and whitespace-only text
//! are layout and are skipped.
bool readRows(const QDomElement& section, std::vector<SubScene>* out)
{
	for (QDomNode node = section.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		if (node.isComment()) { continue; }  // comments carry nothing
		if (node.isText() && node.toText().data().trimmed().isEmpty()) { continue; }  // indentation carries nothing
		SubScene row;
		const bool fits = static_cast<int>(out->size()) < SubScene::MaxCount;
		if (!node.isElement() || !fits || node.nodeName() != RowName || !row.restoreState(node.toElement())) { return false; }
		out->push_back(std::move(row));
	}
	return true;
}

} // namespace

SessionSubScenes& SessionSubScenes::instance()
{
	static SessionSubScenes* section = [] {
		auto* made = new SessionSubScenes();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *section;
}

QString SessionSubScenes::nodeName()
{
	return QStringLiteral("z:subscenes");
}

bool SessionSubScenes::shouldPersist() const
{
	const SessionModel* session = model();
	return !m_preserved.isEmpty() || (session != nullptr && !session->subScenes().empty());
}

void SessionSubScenes::save(QDomDocument& doc, QDomElement& parent) const
{
	// A newer build's section goes back as it came, never rewritten from what this build understood of it.
	QDomDocument preserved;
	if (!m_preserved.isEmpty() && preserved.setContent(m_preserved, false))
	{
		parent.appendChild(doc.importNode(preserved.documentElement(), true));
		return;
	}
	const SessionModel* session = model();
	if (session == nullptr) { return; }
	// The `z` binding, as every z: section sets it (idempotent on the root).
	QDomElement root = doc.documentElement();
	if (!root.isNull()) { root.setAttribute(documentIndexNamespaceAttribute(), documentIndexNamespaceUri()); }
	QDomElement section = doc.createElement(nodeName());
	section.setAttribute(QStringLiteral("v"), Version);
	for (const SubScene& subScene : session->subScenes())
	{
		QDomElement row = doc.createElement(RowName);
		subScene.saveState(row);
		section.appendChild(row);
	}
	parent.appendChild(section);
}

void SessionSubScenes::load(const QDomElement& element)
{
	m_preserved.clear();
	SessionModel* session = model();
	std::vector<SubScene> rows;
	// Claim or preserve: a version this build writes, every row well formed - else the section is kept verbatim.
	if (session == nullptr || element.attribute(QStringLiteral("v")) != QString::number(Version) || !readRows(element, &rows))
	{
		m_preserved = asText(element);
		return;
	}
	session->setSubScenes(std::move(rows));
}

} // namespace lmms
