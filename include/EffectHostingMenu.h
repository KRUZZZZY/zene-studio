/*
 * EffectHostingMenu.h - an effect's Hosting submenu: in-process or a separate process, and restart
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

#ifndef LMMS_GUI_EFFECT_HOSTING_MENU_H
#define LMMS_GUI_EFFECT_HOSTING_MENU_H

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms
{
class Effect;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "Out-of-process hosting is socket-only": a "Hosting" submenu of @a menu for
 *  @a effect, read from its own oop.get_state entry - a line with its hosting state; Run in a
 *  separate process (checkable, oop.set_mode; offered only where its family has a client and the
 *  plugin can switch - otherwise disabled with the reason as its tooltip); and Restart its process
 *  (oop.restart), offered only while it runs in one. */
LMMS_EXPORT void addEffectHostingMenu(QMenu* menu, Effect* effect);

//! The line's text for one device entry of oop.get_state.
LMMS_EXPORT QString hostingStatusText(const QJsonObject& device);

} // namespace lmms::gui

#endif // LMMS_GUI_EFFECT_HOSTING_MENU_H
