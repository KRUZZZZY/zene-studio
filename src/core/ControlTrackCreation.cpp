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
#include "Song.h"
#include "Track.h"

namespace lmms::control
{

void recordCreatedTrack(Track* track)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("root"));
	doc.appendChild(root);
	track->saveState(doc, root);
	const QString saved = doc.toString();
	// The id, not the pointer: another step (track.remove, then its undo) can free this track and build an
	// equal one, and the redo below may build it under a new id, which it writes back (review W3-batchB #1).
	auto id = std::make_shared<int>(track->id());
	addUndoStep([id]() { if (Track* live = trackWithId(*id)) { removeTrack(live); } },
		[id, saved]() {
			QDomDocument again;
			again.setContent(saved);
			if (Track* made = Track::create(again.documentElement().firstChildElement(), Engine::getSong())) { *id = made->id(); }
		});
}

Track* trackWithId(int id)
{
	ControlResult ignored;
	return id >= 0 ? resolveTrack(trackId(id), &ignored) : nullptr;
}

} // namespace lmms::control
