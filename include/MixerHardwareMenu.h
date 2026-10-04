/*
 * MixerHardwareMenu.h - a mixer channel's "Hardware I/O" menu: output, cue sends, hardware insert
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

#ifndef LMMS_GUI_MIXER_HARDWARE_MENU_H
#define LMMS_GUI_MIXER_HARDWARE_MENU_H

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

/*! 0.5.0 F1 (MX-040, MX-041, MX-036, MX-061): a "Hardware I/O" submenu of @a menu for the channel
 *  @a channelId, read from audio.io_list and mixer.hardware_get when it is built:
 *  - Output: Default, None and every output bus of the interface (its name and channels; one the device
 *    did not open is marked), the current one checked - mixer.hardware_output;
 *  - Cue Sends: one entry per cue mix, opening mixer.cue_send's form for this channel;
 *  - Hardware Insert... (mixer.hardware_insert's form) and Measure Insert Latency (mixer.hardware_insert_ping);
 *  - Cue Mixes... (mixer.cue_master), Interface Channels... (audio.io_setup) and Hardware Routing
 *    (audio.io_list's answer).
 *  Every entry is a registered command, so each change is one undo step. */
LMMS_EXPORT void addHardwareMenu(QMenu* menu, const QString& channelId);

/*! 0.5.0 F7 (MX-060): a "Surround" submenu of @a menu for the channel @a channelId:
 *  - Format: Stereo, LCR, Quad, 5.1, 7.1 - mixer.channel_format, at once;
 *  - Surround Panner... (the panner window, SurroundPanner.h - mixer.surround_pan);
 *  - Downmix Options... (mixer.downmix_options' form) and Surround Routing (mixer.surround_get's answer). */
LMMS_EXPORT void addSurroundMenu(QMenu* menu, const QString& channelId);

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_HARDWARE_MENU_H
