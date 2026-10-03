/*
 * MixerWindows.h - the large meters and the routing matrix
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
#ifndef LMMS_GUI_MIXER_WINDOWS_H
#define LMMS_GUI_MIXER_WINDOWS_H

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

/*! 0.5.0 L08/L09: two windows over the mixer -
 *  - Large Meters (MX-131): the master's level as two tall bars, its correlation and loudness, big
 *    enough to read across the room;
 *  - Routing Matrix (MX-055): every channel against every channel, each cell the send between them
 *    (routing.matrix, the same data an agent reads); double-click a cell to send there;
 *  - Compare Channel EQ (MX-140): two channels' strip EQ curves over each other (mixer.eq_compare).
 *  Add their entries to @a menu. */
LMMS_EXPORT void addMixerWindows(QMenu* menu);

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_WINDOWS_H
