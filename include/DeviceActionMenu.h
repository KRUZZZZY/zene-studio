/*
 * DeviceActionMenu.h - a device card's one action menu: its button and its right-click offer the same entries
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

#ifndef LMMS_GUI_DEVICE_ACTION_MENU_H
#define LMMS_GUI_DEVICE_ACTION_MENU_H

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

class DeviceCard;

/*! 0.5.0 theme 12: a device card's actions, in one order on every card - from its "Actions" button and from a
 *  right-click alike. An effect: Bypass, Open Editor, Presets (load, save), A/B, Replace With..., Move Left,
 *  Move Right, Remove; then Insert Before / After, Copy, the Parameters and option entries, and Hosting (a crashed
 *  host is restarted there). An instrument: Open Editor, Presets, Replace Instrument.... Every entry is a registered
 *  command (plugin.bypass, plugin.editor_open, plugin.preset_*, plugin.ab, plugin.replace, plugin.move,
 *  plugin.unload, plugin.load ...) and carries its id as "controlCommand". */
LMMS_EXPORT void fillDeviceActionMenu(QMenu* menu, DeviceCard* card);

} // namespace lmms::gui

#endif // LMMS_GUI_DEVICE_ACTION_MENU_H
