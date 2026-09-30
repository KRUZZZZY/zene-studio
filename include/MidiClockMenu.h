/*
 * MidiClockMenu.h - Edit > MIDI Clock: send, follow and see the lock
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

#ifndef LMMS_GUI_MIDI_CLOCK_MENU_H
#define LMMS_GUI_MIDI_CLOCK_MENU_H

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "MIDI clock is the engine and the socket, and there is no interface for it":
 *  a "MIDI Clock" submenu of @a menu with three checkable items - Send Clock (clock.master_set),
 *  Follow External Clock and Follow Its Tempo (clock.slave_set) - and a disabled line that, each
 *  time the submenu opens, re-reads clock.get_state into the lock indicator. A refused switch (a
 *  master with no MIDI client to send through) stays unchecked and the line says why. */
LMMS_EXPORT QMenu* addMidiClockMenu(QMenu* menu);

//! The indicator line for a clock.get_state reply.
LMMS_EXPORT QString midiClockStatusText(const QJsonObject& state);

} // namespace lmms::gui

#endif // LMMS_GUI_MIDI_CLOCK_MENU_H
