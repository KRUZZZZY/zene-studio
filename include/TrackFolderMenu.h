/*
 * TrackFolderMenu.h - a track's "Folder" menu: membership, collapse, pin, routing
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

#ifndef LMMS_GUI_TRACK_FOLDER_MENU_H
#define LMMS_GUI_TRACK_FOLDER_MENU_H

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "folder tracks ... drivable through the socket, not from the interface":
 *  a "Folder" submenu for @a track's operations menu. Every item runs the registry command an
 *  agent runs - "Move to folder" (track.set_folder, with "No folder" for the container root),
 *  and on a folder itself "Collapsed" (track.folder_set_collapsed), "Pinned" (track.set_pinned)
 *  and "Route children through the folder" (track.set_routing) - so the menu and the socket
 *  cannot disagree. A refusal (a cycle, say) is shown in a message box, typed text and all. */
LMMS_EXPORT void addTrackFolderMenu(QMenu* menu, Track* track);

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_FOLDER_MENU_H
