/*
 * ControlTrackRoleLinks.cpp - the automation links a kept instrument role takes with it (Q050-283.2)
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

#include "ControlTrackRoleLinks.h"

#include <QDomDocument>
#include <QSet>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

namespace lmms::control
{

namespace
{

//! Adds the `id` of @a element and of every element below it to @a ids.
void collectIds(const QDomElement& element, QSet<int>* ids)
{
	bool ok = false;
	const int id = element.attribute(QStringLiteral("id")).toInt(&ok);
	if (ok && id != 0) { ids->insert(id); }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		collectIds(child, ids);
	}
}

//! The ids carried by either the row's mute/solo children or its instrument-role elements.
QSet<int> trackControlIds(const QString& xml, bool rowControls)
{
	QSet<int> ids;
	QDomDocument document;
	if (!document.setContent(xml)) { return ids; }
	const QDomElement track = document.documentElement().firstChildElement(QStringLiteral("track"));
	for (QDomElement child = track.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const bool isRowControl = child.tagName() == QLatin1String("muted") || child.tagName() == QLatin1String("solo");
		if (isRowControl != rowControls) { continue; }
		collectIds(child, &ids);
	}
	return ids;
}

//! Every track that can hold an automation clip: the song's, the pattern store's, the song-global one.
std::vector<Track*> automationTracks()
{
	std::vector<Track*> tracks;
	Song* song = Engine::getSong();
	const auto add = [&tracks](const TrackContainer::TrackList& list) {
		for (Track* track : list)
		{
			if (track->type() == Track::Type::Automation || track->type() == Track::Type::HiddenAutomation)
			{
				tracks.push_back(track);
			}
		}
	};
	if (song != nullptr) { add(song->tracks()); }
	if (Engine::patternStore() != nullptr) { add(Engine::patternStore()->tracks()); }
	if (song != nullptr && song->globalAutomationTrack() != nullptr) { tracks.push_back(song->globalAutomationTrack()); }
	return tracks;
}

Track* automationTrackWithId(int id)
{
	for (Track* track : automationTracks())
	{
		if (track->id() == id) { return track; }
	}
	return nullptr;
}

//! The clip a link names: by index when the clip there still starts where it did, else by start tick.
AutomationClip* clipOf(const KeptAutomationLink& link)
{
	Track* track = automationTrackWithId(link.track);
	if (track == nullptr) { return nullptr; }
	const auto& clips = track->getClips();
	if (link.clip < static_cast<int>(clips.size()))
	{
		auto* clip = dynamic_cast<AutomationClip*>(clips[static_cast<std::size_t>(link.clip)]);
		if (clip != nullptr && clip->startPosition().getTicks() == link.position) { return clip; }
	}
	for (Clip* each : clips)
	{
		auto* clip = dynamic_cast<AutomationClip*>(each);
		if (clip != nullptr && clip->startPosition().getTicks() == link.position) { return clip; }
	}
	return nullptr;
}

//! The live control a link names: the same lookups AutomationClip::resolveAllIDs makes.
AutomatableModel* controlOf(int id)
{
	ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return nullptr; }
	for (const jo_id_t candidate : {static_cast<jo_id_t>(id), ProjectJournal::idFromSave(static_cast<jo_id_t>(id))})
	{
		if (auto* model = dynamic_cast<AutomatableModel*>(journal->journallingObject(candidate))) { return model; }
	}
	return nullptr;
}

} // namespace

std::vector<KeptAutomationLink> detachTrackAutomation(const QString& xml, bool rowControls)
{
	std::vector<KeptAutomationLink> links;
	const QSet<int> ids = trackControlIds(xml, rowControls);
	if (ids.isEmpty()) { return links; }
	for (Track* track : automationTracks())
	{
		const auto& clips = track->getClips();
		for (int index = 0; index < static_cast<int>(clips.size()); ++index)
		{
			auto* clip = dynamic_cast<AutomationClip*>(clips[static_cast<std::size_t>(index)]);
			if (clip == nullptr) { continue; }
			for (const auto& target : clip->objectsSnapshot())
			{
				AutomatableModel* model = target.data();
				if (model == nullptr) { continue; }
				const int written = static_cast<int>(ProjectJournal::idToWrite(model->id()));
				if (!ids.contains(written)) { continue; }
				links.push_back({track->id(), index, clip->startPosition().getTicks(), written});
				clip->removeObject(model);
				QObject::disconnect(model, SIGNAL(destroyed(lmms::jo_id_t)), clip, SLOT(objectDestroyed(lmms::jo_id_t)));
			}
		}
	}
	return links;
}

int reattachInstrumentAutomation(const std::vector<KeptAutomationLink>& links)
{
	int made = 0;
	for (const KeptAutomationLink& link : links)
	{
		AutomationClip* clip = clipOf(link);
		AutomatableModel* model = controlOf(link.model);
		if (clip != nullptr && model != nullptr && clip->addObject(model)) { ++made; }
	}
	return made;
}

} // namespace lmms::control
