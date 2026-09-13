/*
 * TrackContainerFolders.cpp - the track container's trk-<n> id lookup, the
 *                             folder relation's post-load pass and the named
 *                             visibility-set store
 *                             (docs/TRACK-FOLDER-DESIGN.md; owner items 3+20+21).
 *
 * Split out of TrackContainer.cpp, where all of it landed first: the file-length
 * ratchet measures a file as a unit (Gate 7, 500 lines) and TrackContainer.cpp is
 * upstream-inherited, so this fork's own half of the container belongs in a file
 * of its own - the same reason the control-command groups and the reversibility
 * table are split by seam and not by re-anchor. TrackContainer's own declaration,
 * its save/load hooks and its ownership rules stay where they were.
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

#include <QDomDocument>
#include <QDomElement>
#include <QStringList>

#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

namespace lmms
{


// ---------------------------------------------------------------------------
// trk-<n> addressing by id, never by position (SPEC-stable-ids.md), and the
// folder relation's post-load pass (docs/TRACK-FOLDER-DESIGN.md section 4.3).
// ---------------------------------------------------------------------------
Track* TrackContainer::findTrackById(int id) const
{
	Track* found = nullptr;
	m_tracksMutex.lockForRead();
	for (Track* track : m_tracks)
	{
		if (track->id() == id) { found = track; break; }
	}
	m_tracksMutex.unlock();
	return found;
}


TrackFolder* TrackContainer::findTrackFolderById(int id) const
{
	Track* track = findTrackById(id);
	return track != nullptr && track->type() == Track::Type::Folder
		? static_cast<TrackFolder*>(track) : nullptr;
}


int TrackContainer::resolveTrackFolders()
{
	int repaired = 0;
	m_tracksMutex.lockForRead();
	const TrackList list = m_tracks;
	m_tracksMutex.unlock();
	for (Track* track : list)
	{
		const int folderId = track->pendingFolderId();
		if (folderId < 0) { continue; }
		TrackFolder* folder = findTrackFolderById(folderId);
		if (folder != nullptr)
		{
			if (folder != track) { track->setParentFolder(folder); }
		}
		else
		{
			// A dangling parent is the same class of defect Song's id pass
			// repairs, and it is COUNTED rather than hidden: the child goes to
			// the container root and the count comes back to the caller.
			++repaired;
			qWarning("track-folder: trk-%d names folder %d, which is not a folder in this "
				"container; the track is left at the container root", track->id(), folderId);
		}
		track->setPendingFolderId(-1);
	}
	return repaired;
}


// ---------------------------------------------------------------------------
// Named visibility sets (owner items 3+20+21; docs/TRACK-FOLDER-DESIGN.md
// section 6). A set is a named, id-based list of tracks; applying it makes
// exactly its members visible and hides every other track of the container.
// Track::visible is a VIEW flag: it does not mute and does not change a render.
// ---------------------------------------------------------------------------
void TrackContainer::saveVisibilitySets(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(QStringLiteral("visibilitysets"));
	// metadata="1" is load-bearing: loadSettings constructs a Track from every
	// element child that is not marked, so an unmarked element would become a
	// track of an unrecognised type (the rule <takelanes> records inside a track).
	element.setAttribute(QStringLiteral("metadata"), 1);
	if (!m_activeVisibilitySet.isEmpty())
	{
		element.setAttribute(QStringLiteral("active"), m_activeVisibilitySet);
	}
	for (const VisibilitySet& set : m_visibilitySets)
	{
		QDomElement setElement = doc.createElement(QStringLiteral("set"));
		setElement.setAttribute(QStringLiteral("name"), set.name);
		QStringList ids;
		for (int trackId : set.trackIds) { ids.append(QString::number(trackId)); }
		setElement.setAttribute(QStringLiteral("tracks"), ids.join(QLatin1Char(',')));
		element.appendChild(setElement);
	}
	parent.appendChild(element);
}


void TrackContainer::loadVisibilitySets(const QDomElement& element)
{
	m_activeVisibilitySet = element.attribute(QStringLiteral("active"));
	for (QDomElement setElement = element.firstChildElement(QStringLiteral("set"));
		!setElement.isNull();
		setElement = setElement.nextSiblingElement(QStringLiteral("set")))
	{
		VisibilitySet set;
		set.name = setElement.attribute(QStringLiteral("name"));
		if (set.name.isEmpty()) { continue; }
		for (const QString& id : setElement.attribute(QStringLiteral("tracks"))
			.split(QLatin1Char(','), Qt::SkipEmptyParts))
		{
			set.trackIds.push_back(id.toInt());
		}
		m_visibilitySets.append(set);
	}
}


const TrackContainer::VisibilitySet* TrackContainer::findVisibilitySet(const QString& name) const
{
	for (const VisibilitySet& set : m_visibilitySets)
	{
		if (set.name == name) { return &set; }
	}
	return nullptr;
}


bool TrackContainer::saveVisibilitySet(const QString& name, const std::vector<int>& trackIds)
{
	if (name.isEmpty()) { return false; }
	for (VisibilitySet& set : m_visibilitySets)
	{
		if (set.name == name) { set.trackIds = trackIds; return true; }
	}
	VisibilitySet set;
	set.name = name;
	set.trackIds = trackIds;
	m_visibilitySets.append(set);
	return true;
}


bool TrackContainer::removeVisibilitySet(const QString& name)
{
	for (int i = 0; i < m_visibilitySets.size(); ++i)
	{
		if (m_visibilitySets.at(i).name != name) { continue; }
		m_visibilitySets.remove(i);
		if (m_activeVisibilitySet == name) { m_activeVisibilitySet.clear(); }
		return true;
	}
	return false;
}


bool TrackContainer::applyVisibilitySet(const QString& name, int* shown, int* hidden)
{
	const VisibilitySet* set = findVisibilitySet(name);
	if (set == nullptr) { return false; }
	int visibleTracks = 0;
	int hiddenTracks = 0;
	m_tracksMutex.lockForRead();
	for (Track* track : m_tracks)
	{
		const bool member = std::find(set->trackIds.begin(), set->trackIds.end(),
			track->id()) != set->trackIds.end();
		track->setVisible(member);
		if (member) { ++visibleTracks; } else { ++hiddenTracks; }
	}
	m_tracksMutex.unlock();
	m_activeVisibilitySet = name;
	if (shown != nullptr) { *shown = visibleTracks; }
	if (hidden != nullptr) { *hidden = hiddenTracks; }
	if (Engine::getSong() != nullptr) { Engine::getSong()->setModified(); }
	return true;
}


void TrackContainer::clearVisibilitySets()
{
	m_visibilitySets.clear();
	m_activeVisibilitySet.clear();
}

} // namespace lmms
