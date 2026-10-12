/*
 * ControlRackMacroWrites.h - one macro write with its undo halves, shared by rack.macro_set and rack.macro_set_pair
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
#ifndef LMMS_CONTROL_RACK_MACRO_WRITES_H
#define LMMS_CONTROL_RACK_MACRO_WRITES_H

#include <QJsonArray>
#include <QString>

#include "ControlRackSupport.h"
#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;
class Rack;

//! W1: what one macro write did - both halves of its undo step, and the parameter writes as the replies list them.
struct MacroApplied
{
	MacroRestore undo;
	MacroRestore redo;
	QJsonArray writes;
	int skipped = 0;
};

//! True when setting this macro changed its scalar or any resolved parameter.
LMMS_EXPORT bool macroAppliedChanged(const MacroApplied& applied);

/*! Sets macro @a macro (a valid index) of @a rack - the rack of the channel @a channelId names - to @a value and drives
 *  every bound parameter through its window. Pushes NO undo step: the caller pushes one for this write together with
 *  any write it pairs it with (rack.macro_set alone, rack.macro_set_pair two at once). Control thread only. */
LMMS_EXPORT MacroApplied applyMacroValue(Rack& rack, const QString& channelId, int macro, float value,
	bool journalParameters = true);

//! rack.macro_set_pair (W1): two macros of one rack set as ONE undo step - the XY pad's drag. Called by
//! registerRackMacroCommands, so the group registers it with the rest of the macro verbs.
void registerRackMacroPairCommand(ControlRegistry& registry);

} // namespace lmms

#endif // LMMS_CONTROL_RACK_MACRO_WRITES_H
