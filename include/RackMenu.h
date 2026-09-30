/*
 * RackMenu.h - a mixer channel's Rack: parallel chains, the chain it routes to, and its macros
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

#ifndef LMMS_GUI_RACK_MENU_H
#define LMMS_GUI_RACK_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms
{
class MixerChannel;
}

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "No racks in the interface" and "Macros and key/velocity zones are socket-only":
 *  a "Rack" submenu of @a menu for @a channel, read from rack.get_state - a line
 *  naming the chains and the routing; Route to (Parallel, or one chain; rack.set_selected); Add
 *  parallel chain (rack.add_chain) and Remove chain (rack.remove_chain, chain 0 is the channel's
 *  own and is not offered); one item per macro showing its value, which sets it (rack.macro_set),
 *  Add macro... (rack.macro_add) and Remove macro (rack.macro_remove). Each change is one
 *  control.undo step. Each macro also gets "Bind <name> to" (effect > parameter of the channel's own
 *  chain, over the full range; rack.macro_target_add) and "Unbind <name>" (rack.macro_target_remove).
 *  The key/velocity zones and a narrower binding range stay socket-only. */
LMMS_EXPORT void addRackMenu(QMenu* menu, const MixerChannel* channel);

//! The line for a rack.get_state reply.
LMMS_EXPORT QString rackStatusText(const QJsonObject& state);

//! Tests replace the macro-name prompt (default: a text input; empty cancels) and the macro-value
//! prompt (default: a number input 0..1 seeded with @a current; nullopt cancels).
LMMS_EXPORT void setRackMacroNamePrompt(std::function<QString()> prompt);
LMMS_EXPORT void setRackMacroValuePrompt(std::function<std::optional<double>(double current)> prompt);

} // namespace lmms::gui

#endif // LMMS_GUI_RACK_MENU_H
