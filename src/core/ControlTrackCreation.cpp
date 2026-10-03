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
#include "ControlReversibility.h"
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
	auto holder = std::make_shared<Track*>(track);
	addUndoStep([holder]() { if (*holder != nullptr) { removeTrack(*holder); *holder = nullptr; } },
		[holder, saved]() {
			QDomDocument again;
			again.setContent(saved);
			*holder = Track::create(again.documentElement().firstChildElement(), Engine::getSong());
		});
}

} // namespace lmms::control
