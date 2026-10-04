/*
 * CollabCommands.h - the collab.* command group and its reversibility rows
 *                    (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_COMMANDS_H
#define LMMS_COLLAB_COMMANDS_H

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;

/*! collab.host / stop / invite / join / leave / get_state / set_role / resync / show_panel - peer-to-peer
 *  collaboration (WF-050) and remote control (WF-052) without a server. Registered in every build: without Qt
 *  Network (ZENE_HAVE_COLLAB undefined) collab.get_state answers available:false and the others refuse with
 *  an "unavailable:" message, so the surface is the same everywhere. */
LMMS_EXPORT void registerCollabCommands(ControlRegistry& registry);

namespace control
{
struct ReversibilityRow;
//! The group's rows (ControlReversibilityTableCollab.cpp), to be joined into reversibilityRowTable().
LMMS_EXPORT const ReversibilityRow* reversibilityCollabRowTable(int* rowCount);
} // namespace control

} // namespace lmms

#endif // LMMS_COLLAB_COMMANDS_H
