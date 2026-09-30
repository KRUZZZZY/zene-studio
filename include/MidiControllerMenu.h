/*
 * MidiControllerMenu.h - soft takeover and LED feedback on a MIDI-driven control's menu
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

#ifndef LMMS_GUI_MIDI_CONTROLLER_MENU_H
#define LMMS_GUI_MIDI_CONTROLLER_MENU_H

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class AutomatableModel;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "the controller surface is drivable through the socket, not from the
 *  interface": for a control driven by a MIDI controller, two checkable items in its connection
 *  submenu - Soft takeover (controller.soft_takeover: the hardware must reach the value before it
 *  moves it) and LED feedback (controller.feedback: the value is sent back to the controller) -
 *  naming the control as the commands do, by its full display name. Nothing is added for a control
 *  that is not MIDI-driven. */
LMMS_EXPORT void addMidiControllerToggles(QMenu* menu, AutomatableModel* model);

} // namespace lmms::gui

#endif // LMMS_GUI_MIDI_CONTROLLER_MENU_H
