/*
 * EffectDeviceMenu.h - an effect's device entries: insert beside it, copy it, compare two settings
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
#ifndef LMMS_GUI_EFFECT_DEVICE_MENU_H
#define LMMS_GUI_EFFECT_DEVICE_MENU_H

#include <QJsonObject>

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Effect;
class EffectChain;
}

namespace lmms::gui
{

/*! 0.5.0 L10: what a device's menu offers beyond moving and removing it, each through the registry:
 *  - MX-074: Insert Effect Before / After - the effect picker, then plugin.load at that position;
 *  - MX-076: Copy to Channels... (one plugin.copy naming every channel chosen) and Copy Chain to Channels...
 *    (chain.copy);
 *  - MX-092: A/B - store the settings in A or B, toggle between them (plugin.ab).
 *  Used by the rack's effect entries and the Device panel's cards. */
LMMS_EXPORT void addEffectDeviceMenus(QMenu* menu, Effect* effect);

//! The plugin.* address of @a chain: {target: trk-<n> / ch-<n>} plus {chain: n} for a rack chain;
//! empty when no track or channel owns it.
LMMS_EXPORT QJsonObject chainAddress(const EffectChain* chain);

} // namespace lmms::gui

#endif // LMMS_GUI_EFFECT_DEVICE_MENU_H
