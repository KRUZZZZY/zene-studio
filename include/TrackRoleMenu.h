/*
 * TrackRoleMenu.h - the Actions-menu items that switch a row between neutral and instrument (Q050-283.2)
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

#ifndef LMMS_GUI_TRACK_ROLE_MENU_H
#define LMMS_GUI_TRACK_ROLE_MENU_H

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! 0.5.0 L04 (Q050-283.2): the role items of @a track's Actions menu, each running track.set_role -
 *  the command an agent runs. On a neutral row an "Assign instrument" submenu (object name
 *  `assignInstrumentMenu`): "Restore <plug-in>" (`restoreInstrumentRoleAction`) when the row keeps an
 *  instrument role, then one item per instrument plug-in of this build. On an instrument track
 *  "Make neutral (keep instrument)" (`makeNeutralAction`). Other track types get nothing.
 *  The command runs queued, once the menu has closed: the switch replaces the row's view, which owns
 *  the menu the item is in. A refusal is shown in a message box. */
LMMS_EXPORT void addTrackRoleMenu(QMenu* menu, Track* track);

} // namespace lmms::gui

#endif // LMMS_GUI_TRACK_ROLE_MENU_H
