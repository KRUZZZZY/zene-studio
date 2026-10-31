/*
 * AddTrackMenu.h - the arranger's menu for adding tracks with a destination (Q050-212)
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

#ifndef LMMS_GUI_ADD_TRACK_MENU_H
#define LMMS_GUI_ADD_TRACK_MENU_H

class QMenu;
class QToolButton;
class QWidget;

namespace lmms::gui
{

/*! Rebuilds @a menu: Instrument Track and Sample Track, each listing where the new track plays (the mixer's regular
 *  channels as they are now, and the default), and Add Tracks to New Bus... Every entry dispatches track.add or
 *  track.add_to_new_bus. Exposed for the test. */
void populateAddTrackMenu(QMenu* menu);

//! The toolbar button holding that menu, rebuilt each time it opens.
QToolButton* makeAddTrackButton(QWidget* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_ADD_TRACK_MENU_H
