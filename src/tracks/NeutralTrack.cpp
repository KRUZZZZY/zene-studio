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

void NeutralTrack::saveTrackSpecificSettings(QDomDocument&, QDomElement&, bool)
{
	// Nothing role-specific exists yet: the row is neutral. The (empty) element Track::saveTrack
	// wraps around this call is what tells a loader the row is a NeutralTrack's.
}

void NeutralTrack::loadTrackSpecificSettings(const QDomElement&)
{
}

} // namespace lmms
