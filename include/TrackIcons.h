/*
 * TrackIcons.h - a track's chosen icon on its header
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

#ifndef LMMS_GUI_TRACK_ICONS_H
#define LMMS_GUI_TRACK_ICONS_H

#include <string>

#include "lmms_export.h"

class QToolButton;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! 0.5.0 W3 L13 (WF-120): the icon a user chose for a track (track.set_icon, TrackLooks.h) on the
 *  track's header button. True when the chosen icon is on @a button - the caller paints and is done;
 *  false when the type's own icon applies, which this puts back first when a chosen one was showing.
 *  @a shown is the button's record of the icon it shows (the chosen ones are prefixed "zene:"). */
LMMS_EXPORT bool applyTrackIcon(QToolButton* button, const Track* track, std::string* shown);

//! Repaint every track header: TrackLooks' change hook, installed with the menus.
LMMS_EXPORT void repaintTrackHeaders();

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_ICONS_H
