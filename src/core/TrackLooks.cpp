/*
 * TrackLooks.cpp - a track's icon, kept with the project
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

#include "TrackLooks.h"

#include <QDomDocument>
#include <QDomElement>

#include "Engine.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! A deleted track's icon stays in memory (undoing the delete brings the track back with its id) but is
//! not written: the file carries icons only for the tracks it carries.
bool songHasTrack(int id)
{
	return Engine::getSong() != nullptr && Engine::getSong()->findTrackById(id) != nullptr;
}

} // namespace

TrackLooks& TrackLooks::instance()
{
	static TrackLooks* looks = [] {
		auto* made = new TrackLooks();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *looks;
}

void TrackLooks::setIcon(int trackId, const QString& icon)
{
	if (icon.isEmpty()) { m_icons.remove(trackId); } else { m_icons.insert(trackId, icon); }
	changed();
}

void TrackLooks::changed() const
{
	if (m_onChange) { m_onChange(); }
}

QString TrackLooks::elementName() const
{
	return QStringLiteral("track-looks");
}

bool TrackLooks::shouldPersist() const
{
	for (auto it = m_icons.begin(); it != m_icons.end(); ++it)
	{
		if (songHasTrack(it.key())) { return true; }
	}
	return false;
}

void TrackLooks::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement root = doc.createElement(elementName());
	for (auto it = m_icons.begin(); it != m_icons.end(); ++it)
	{
		if (!songHasTrack(it.key())) { continue; }
		QDomElement track = doc.createElement(QStringLiteral("track"));
		track.setAttribute(QStringLiteral("id"), it.key());
		track.setAttribute(QStringLiteral("icon"), it.value());
		root.appendChild(track);
	}
	parent.appendChild(root);
}

void TrackLooks::load(const QDomElement& element)
{
	m_icons.clear();
	for (QDomElement track = element.firstChildElement(QStringLiteral("track")); !track.isNull();
		track = track.nextSiblingElement(QStringLiteral("track")))
	{
		const QString icon = track.attribute(QStringLiteral("icon"));
		bool ok = false;
		const int id = track.attribute(QStringLiteral("id")).toInt(&ok);
		if (ok && !icon.isEmpty()) { m_icons.insert(id, icon); }  // no id is no track (review L19 #14)
	}
	changed();
}

void TrackLooks::clear()
{
	m_icons.clear();
	changed();
}

} // namespace lmms
