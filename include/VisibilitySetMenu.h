/*
 * VisibilitySetMenu.h - the song editor's Views menu: named track-visibility sets
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

#ifndef LMMS_GUI_VISIBILITY_SET_MENU_H
#define LMMS_GUI_VISIBILITY_SET_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

//! Asks for a new set's name; nothing when cancelled. Replaceable for tests.
using VisibilitySetPrompt = std::function<std::optional<QString>()>;
LMMS_EXPORT void setVisibilitySetPrompt(VisibilitySetPrompt prompt);

/*! KNOWN-LIMITATIONS "... the named visibility sets are drivable through the socket, not from the
 *  interface": fills @a menu (cleared first; call it from aboutToShow) with All tracks
 *  (track.visibility_show_all), every set the project holds - the active one checked - applying it
 *  (track.visibility_set_apply), "Save visible tracks as..." (track.visibility_set_save with the
 *  tracks now shown) and a Remove submenu (track.visibility_set_remove). */
LMMS_EXPORT void populateVisibilitySetMenu(QMenu* menu);

} // namespace lmms::gui

#endif // LMMS_GUI_VISIBILITY_SET_MENU_H
