/*
 * MpeInputAction.h - Edit > MPE Input: the MIDI Polyphonic Expression input switch
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

#ifndef LMMS_GUI_MPE_INPUT_ACTION_H
#define LMMS_GUI_MPE_INPUT_ACTION_H

#include "lmms_export.h"

class QAction;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "device.mpe_set is drivable through the socket, not from the interface": a
 *  checkable "MPE Input" item added to @a menu. Toggling runs device.mpe_set; the check mark is
 *  re-read from device.mpe_get_state every time the menu opens, so a switch thrown over the socket
 *  shows. Off, MIDI input is what it was before MPE; on, a member channel's bend, pressure and CC74
 *  are that note's own expression (docs/MPE.md). */
LMMS_EXPORT QAction* addMpeInputAction(QMenu* menu);

} // namespace lmms::gui

#endif // LMMS_GUI_MPE_INPUT_ACTION_H
