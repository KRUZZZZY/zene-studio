/*
 * NeutralTrack.cpp - a neutral arrangement row (Q050-283.1)
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


#include "NeutralTrack.h"

#include <QDomDocument>
#include <QDomElement>

#include "NeutralTrackView.h"

namespace lmms
{

NeutralTrack::NeutralTrack(TrackContainer* tc)
	: Track(Type::Neutral, tc)
{
	setName(tr("Neutral track"));
}

bool NeutralTrack::play(const TimePos&, const f_cnt_t, const f_cnt_t, int)
{
	return false;
}

gui::TrackView* NeutralTrack::createView(gui::TrackContainerView* view)
{
	return new gui::NeutralTrackView(this, view);
}

Clip* NeutralTrack::createClip(const TimePos&)
{
	return nullptr;
}

namespace
{

//! The element, inside `<neutraltrack>`, that carries one kept role (Q050-283.2).
const QString KeptRoleElement = QStringLiteral("keptrole");
const QString InstrumentRoleKind = QStringLiteral("instrument");

//! The plug-in a captured instrument track names: `<track><instrumenttrack><instrument name=...>`.
QString instrumentPluginOf(const QString& xml)
{
	QDomDocument document;
	if (xml.isEmpty() || !document.setContent(xml)) { return QString(); }
	return document.documentElement().firstChildElement(QStringLiteral("track"))
		.firstChildElement(QStringLiteral("instrumenttrack"))
		.firstChildElement(QStringLiteral("instrument")).attribute(QStringLiteral("name"));
}

} // namespace

void NeutralTrack::setKeptInstrumentRole(const QString& xml)
{
	m_keptInstrumentRole = xml;
	m_keptInstrumentPlugin = instrumentPluginOf(xml);
}

void NeutralTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode)
{
	// Stage 1 wrote an empty element; a row that keeps no role still does, byte for byte.
	if (presetMode || m_keptInstrumentRole.isEmpty()) { return; }
	QDomElement kept = doc.createElement(KeptRoleElement);
	kept.setAttribute(QStringLiteral("kind"), InstrumentRoleKind);
	kept.setAttribute(QStringLiteral("plugin"), m_keptInstrumentPlugin);
	kept.appendChild(doc.createTextNode(m_keptInstrumentRole));
	parent.appendChild(kept);
}

void NeutralTrack::loadTrackSpecificSettings(const QDomElement& element)
{
	// Absent means none: a row loaded without a kept role must not keep the one it had before.
	QString kept;
	for (QDomElement role = element.firstChildElement(KeptRoleElement); !role.isNull();
		role = role.nextSiblingElement(KeptRoleElement))
	{
		if (role.attribute(QStringLiteral("kind")) == InstrumentRoleKind) { kept = role.text(); }
	}
	setKeptInstrumentRole(kept);
}

} // namespace lmms
