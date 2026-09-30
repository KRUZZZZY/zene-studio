/*
 * TrackRenderMenu.h - a track's Freeze / Unfreeze / Bounce in place items, and DAWproject files
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

#ifndef LMMS_GUI_TRACK_RENDER_MENU_H
#define LMMS_GUI_TRACK_RENDER_MENU_H

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "freeze and bounce-in-place have no interface": for an instrument or sample
 *  track's operations menu, Freeze track (freeze.track) or - on a frozen one - Unfreeze track
 *  (freeze.unfreeze), and Bounce in place (bounce.in_place). Both renders run in a child process
 *  and the command answers when it finishes, so the items show a wait cursor; a refusal is shown,
 *  typed. Other track types get nothing. */
LMMS_EXPORT void addTrackRenderActions(QMenu* menu, Track* track);

/*! KNOWN-LIMITATIONS "DAWproject import / export has no interface": File menu items Import
 *  DAWproject... and Export DAWproject... (dawproject.import / dawproject.export), each through a
 *  file dialog; the result or the refusal is shown. */
LMMS_EXPORT void addDawProjectActions(QMenu* fileMenu);

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_RENDER_MENU_H
