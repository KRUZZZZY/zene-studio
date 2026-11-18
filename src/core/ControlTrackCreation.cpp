/*
 * ControlTrackCreation.cpp - one undo step for a track a command created
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
#include "ControlTrackCreation.h"

#include <memory>

#include <QDomDocument>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms::control
{

namespace
{

//! The live track of \a container whose id is \a id, or nullptr when that container no longer holds one.
Track* trackWithIdIn(const TrackContainer* container, int id)
{
	for (Track* track : container->tracks())
	{
		if (track->id() == id) { return track; }
	}
	return nullptr;
}

void updatePatternEditor(TrackContainer* container)
{
	if (container != Engine::patternStore()) { return; }
	gui::GuiApplication* application = gui::getGUI();
	if (application != nullptr && application->patternEditor() != nullptr
		&& application->patternEditor()->m_editor != nullptr)
	{
		application->patternEditor()->m_editor->updateMaxSteps();
	}
}

//! Removes \a track from \a container the way that container's editor does: its view first, then the track. The
//! song's own removal is control::removeTrack; the PatternStore's view is in the Pattern Editor.
void removeFromContainer(TrackContainer* container, Track* track)
{
	if (container == Engine::getSong()) { removeTrack(track); return; }
	gui::GuiApplication* gui = gui::getGUI();
	if (container == Engine::patternStore() && gui != nullptr && gui->patternEditor() != nullptr)
	{
		gui::TrackContainerView* editor = gui->patternEditor()->m_editor;
		for (gui::TrackView* view : editor->trackViews())
		{
			if (view->getTrack() == track)
			{
				editor->deleteTrackView(view);
				updatePatternEditor(container);
				return;
			}
		}
	}
	delete track;
	updatePatternEditor(container);
}

//! What an undo step keeps of one created track: its id (written back by a redo), its saved document, its container.
struct Recorded
{
	std::shared_ptr<int> id;
	QString saved;
	TrackContainer* container;
};

} // namespace

void recordCreatedTrack(Track* track)
{
	recordCreatedTrack(track, Engine::getSong());
}

void recordCreatedTrack(Track* track, TrackContainer* container)
{
	recordCreatedTracks({captureCreatedTrack(track, container)});
}

CreatedTrack captureCreatedTrack(Track* track, TrackContainer* container)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("root"));
	doc.appendChild(root);
	track->saveState(doc, root);
	return CreatedTrack{track, container, doc.toString()};
}

void recordCreatedTracks(const QVector<CreatedTrack>& created)
{
	if (created.isEmpty()) { return; }
	// The id, not the pointer: another step (track.remove, then its undo) can free a track and build an equal one,
	// and the redo writes the new id back (review W3-batchB #1). A redo builds each track in the container it was
	// made in: a PatternStore copy stays in the store.
	auto records = std::make_shared<QVector<Recorded>>();
	for (const CreatedTrack& c : created)
	{
		records->append(Recorded{std::make_shared<int>(c.track->id()), c.saved, c.container});
	}
	addUndoStep([records]() {
			// Last made, first removed: a track another one depends on goes after it.
			for (auto it = records->crbegin(); it != records->crend(); ++it)
			{
				if (Track* live = trackWithIdIn(it->container, *it->id)) { removeFromContainer(it->container, live); }
			}
		},
		[records]() {
			for (const Recorded& r : *records)
			{
				QDomDocument again;
				again.setContent(r.saved);
				if (Track* made = Track::create(again.documentElement().firstChildElement(), r.container))
				{
					*r.id = made->id();
					updatePatternEditor(r.container);
				}
			}
		});
}

Track* trackWithId(int id)
{
	ControlResult ignored;
	return id >= 0 ? resolveTrack(trackId(id), &ignored) : nullptr;
}

} // namespace lmms::control
