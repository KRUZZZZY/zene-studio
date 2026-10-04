/*
 * ControlArrangingCommands.h - the 0.5.0 F3 command groups' registration entry points
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

#ifndef LMMS_CONTROL_ARRANGING_COMMANDS_H
#define LMMS_CONTROL_ARRANGING_COMMANDS_H

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;

//! WF-107: track.set_spacer / track.spacers (ControlCommandsTrackSpacers.cpp).
LMMS_EXPORT void registerTrackSpacerCommands(ControlRegistry& registry);
//! WF-042: reference.* - another project open read-only beside this one (ControlCommandsReference.cpp).
LMMS_EXPORT void registerReferenceCommands(ControlRegistry& registry);
//! WF-042 / WF-048: project.recent, project.switch, project.new_document (ControlCommandsDocuments.cpp).
LMMS_EXPORT void registerDocumentCommands(ControlRegistry& registry);

/*! All three, called from registerProjectInfoCommands (the project group's L12 entry point, which these
 *  extend), so the registry's shared registration list did not have to change. */
LMMS_EXPORT void registerArrangingCommands(ControlRegistry& registry);

} // namespace lmms

#endif // LMMS_CONTROL_ARRANGING_COMMANDS_H
