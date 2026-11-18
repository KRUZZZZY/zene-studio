/*
 * InstrumentCommandMenus.h - 0.5.0 F2 interface: an instrument track's output buses, MIDI
 *                            parts, layer rack and per-voice modulation (MX-114/112/120)
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

#ifndef LMMS_GUI_INSTRUMENT_COMMAND_MENUS_H
#define LMMS_GUI_INSTRUMENT_COMMAND_MENUS_H

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui::InstrumentCommandMenus
{

//! The title of the submenu populate() adds, the same on every surface that offers it.
LMMS_EXPORT QString submenuTitle();

/*! The wave 2 F2 instrument features (plans/PLAN-050-CATCH-UP.md), which were command-only, as one
 *  "Instrument Setup" submenu of @a menu for the instrument track @a track (trk-<n>): its output buses
 *  (plugin.output_*), its MIDI parts (plugin.midi_part*), its layer rack (rack.layer*) and its per-voice
 *  modulation (modulator.voice_set / instrument_target_set). The same submenu is offered by the track's
 *  header menu and its Inspector "More" menu (ShellCommandMenus::populateTrack) and by its instrument's
 *  device card (DeviceActionMenu). Every entry runs or lists through a registered command and declares it
 *  as "controlCommand", so each edit is journalled and undone exactly as the socket's is. */
LMMS_EXPORT void populate(QMenu* menu, const QString& track);

} // namespace lmms::gui::InstrumentCommandMenus

#endif // LMMS_GUI_INSTRUMENT_COMMAND_MENUS_H
