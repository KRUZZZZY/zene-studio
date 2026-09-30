/*
 * MidiReconnectAction.h - Edit > MIDI Controller Reconnect: arm it and see what it holds
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

#ifndef LMMS_GUI_MIDI_RECONNECT_ACTION_H
#define LMMS_GUI_MIDI_RECONNECT_ACTION_H

#include <QString>

#include "lmms_export.h"

class QAction;
class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "MIDI controller auto-reconnection has no interface": a checkable item added
 *  to @a menu that arms and disarms re-connection (midi.reconnect_arm) and, each time the menu
 *  opens, re-reads midi.reconnect_status into its text - how many ports are bound, live and lost -
 *  so a controller that went away shows as lost without a socket client. */
LMMS_EXPORT QAction* addMidiReconnectAction(QMenu* menu);

//! The item's text for a midi.reconnect_status reply.
LMMS_EXPORT QString midiReconnectText(const QJsonObject& status);

} // namespace lmms::gui

#endif // LMMS_GUI_MIDI_RECONNECT_ACTION_H
