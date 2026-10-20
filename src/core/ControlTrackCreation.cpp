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

} // namespace

void recordCreatedTrack(Track* track)
{
	recordCreatedTrack(track, Engine::getSong());
}

void recordCreatedTrack(Track* track, TrackContainer* container)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("root"));
	doc.appendChild(root);
	track->saveState(doc, root);
	const QString saved = doc.toString();
	// The id, not the pointer: another step (track.remove, then its undo) can free this track and build an
	// equal one, and the redo below may build it under a new id, which it writes back (review W3-batchB #1).
	// The redo builds the track in the same container it was made in: a PatternStore copy stays in the store.
	auto id = std::make_shared<int>(track->id());
	addUndoStep([id, container]() {
			if (Track* live = trackWithIdIn(container, *id)) { removeFromContainer(container, live); }
		},
		[id, container, saved]() {
			QDomDocument again;
			again.setContent(saved);
			if (Track* made = Track::create(again.documentElement().firstChildElement(), container))
			{
				*id = made->id();
				updatePatternEditor(container);
			}
		});
}

Track* trackWithId(int id)
{
	ControlResult ignored;
	return id >= 0 ? resolveTrack(trackId(id), &ignored) : nullptr;
}

} // namespace lmms::control
