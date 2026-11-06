/*
 * NeutralTrackView.h - the header of a neutral arrangement row (Q050-283.1)
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


#ifndef LMMS_GUI_NEUTRAL_TRACK_VIEW_H
#define LMMS_GUI_NEUTRAL_TRACK_VIEW_H

#include <QPixmap>

#include "TrackView.h"

namespace lmms
{

class NeutralTrack;

namespace gui
{

//! The row of a neutral track: the standard track header (name, mute, solo, menu), no content widgets.
class LMMS_EXPORT NeutralTrackView : public TrackView
{
public:
	NeutralTrackView(NeutralTrack* track, TrackContainerView* tcv);
	~NeutralTrackView() override = default;

	//! The 20x20 glyph of a neutral row (an empty outlined box), drawn at runtime so it needs no theme file.
	static QPixmap icon();
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_NEUTRAL_TRACK_VIEW_H
