/*
 * TrackVersions.cpp - named versions of a track's clips
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
#include "TrackVersions.h"

#include <QDomDocument>
#include <QDomElement>

#include "Clip.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{

TrackVersions& TrackVersions::instance()
{
	static TrackVersions* versions = [] {
		auto* made = new TrackVersions();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *versions;
}

TrackVersions::Set TrackVersions::of(int trackId) const
{
	return m_sets.value(trackId);
}

void TrackVersions::set(int trackId, const Set& versions)
{
	if (versions.clips.isEmpty()) { m_sets.remove(trackId); }
	else { m_sets.insert(trackId, versions); }
}

QString TrackVersions::clipsOf(Track* track)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("clips"));
	doc.appendChild(root);
	for (Clip* clip : track->getClips()) { clip->saveState(doc, root); }
	return doc.toString(-1);
}

void TrackVersions::replaceClips(Track* track, const QString& xml)
{
	track->deleteClips();
	QDomDocument doc;
	doc.setContent(xml);
	for (QDomElement element = doc.documentElement().firstChildElement(); !element.isNull(); element = element.nextSiblingElement())
	{
		Clip* clip = track->createClip(TimePos(0));
		clip->restoreState(element);
	}
}

void TrackVersions::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	for (auto set = m_sets.constBegin(); set != m_sets.constEnd(); ++set)
	{
		QDomElement track = doc.createElement(QStringLiteral("track"));
		track.setAttribute(QStringLiteral("id"), set.key());
		track.setAttribute(QStringLiteral("active"), set.value().active);
		for (auto version = set.value().clips.constBegin(); version != set.value().clips.constEnd(); ++version)
		{
			QDomElement node = doc.createElement(QStringLiteral("version"));
			node.setAttribute(QStringLiteral("name"), version.key());
			node.appendChild(doc.createCDATASection(version.value()));
			track.appendChild(node);
		}
		element.appendChild(track);
	}
	parent.appendChild(element);
}

void TrackVersions::load(const QDomElement& element)
{
	m_sets.clear();
	for (QDomElement track = element.firstChildElement(QStringLiteral("track")); !track.isNull(); track = track.nextSiblingElement(QStringLiteral("track")))
	{
		Set versions;
		versions.active = track.attribute(QStringLiteral("active"));
		for (QDomElement node = track.firstChildElement(QStringLiteral("version")); !node.isNull(); node = node.nextSiblingElement(QStringLiteral("version")))
		{
			versions.clips.insert(node.attribute(QStringLiteral("name")), node.text());
		}
		if (!versions.clips.isEmpty()) { m_sets.insert(track.attribute(QStringLiteral("id")).toInt(), versions); }
	}
}

} // namespace lmms
