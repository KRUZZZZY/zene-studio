/*
 * ControlRackSupport.h - the helpers the rack.* command group shares between
 *                        its rack half (ControlCommandsRack.cpp) and its macro
 *                        half (ControlCommandsRackMacros.cpp).
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
 */

#ifndef LMMS_CONTROL_RACK_SUPPORT_H
#define LMMS_CONTROL_RACK_SUPPORT_H

#include <utility>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "RackMacros.h"
#include "RackZones.h"
#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class Rack;
class MixerChannel;

//! The model a macro target names on a rack, or nullptr with @a why set.
//! Defined in RackMacros.cpp; repeated here so the callers of this header do not
//! have to know where the resolver lives.
LMMS_EXPORT AutomatableModel* rackMacroTargetModel(Rack& rack, const RackMacroTarget& target,
	QString* why);

//! The rack of the mixer channel a "ch-<n>" id names, or nullptr with @a error
//! set (invalid_args for a malformed id, not_found for a channel that is not
//! there). One definition for the whole rack.* group.
LMMS_EXPORT Rack* resolveRack(const QString& id, ControlResult* error);
//! The channel the same id names, for the callers that need both.
LMMS_EXPORT MixerChannel* resolveRackChannel(const QString& id, ControlResult* error);

//! One macro as rack.get_state and the macro commands report it.
LMMS_EXPORT QJsonObject macroJson(const RackMacro& macro, int index);
//! One zone as rack.get_state and the zone commands report it.
LMMS_EXPORT QJsonObject zoneJson(const RackZone& zone, int index);
//! One macro target as the same reports it.
LMMS_EXPORT QJsonObject macroTargetJson(const RackMacroTarget& target, int index);

//! "macro-<n>" / "zone-<n>" for a list index.
LMMS_EXPORT QString macroId(int index);
LMMS_EXPORT QString zoneId(int index);

//! The index a "macro-<n>" id names on @a rack; -1 with @a error set otherwise.
//! Same for zones.
LMMS_EXPORT int resolveMacroIndex(const Rack& rack, const QString& id, ControlResult* error);
LMMS_EXPORT int resolveZoneIndex(const Rack& rack, const QString& id, ControlResult* error);

//! The whole channel rack as rack.get_state reports it: the chains, the
//! selector, every macro with its targets, and every zone.
LMMS_EXPORT QJsonObject rackState(MixerChannel* channel, Rack& rack);

/*! One half of a rack.macro_set's inverse: the macro's own value plus the value
 *  every parameter had before (or what was written, for the redo).
 *
 *  It holds NO raw model pointer on purpose. An undo step can run long after
 *  the command that recorded it - a project may have been opened in between -
 *  so the step re-resolves each target through rackMacroTargetModel when it
 *  runs and SKIPS a target that is no longer there. That is the same rule
 *  controlRestoreCapturedState follows for a device.
 */
struct MacroRestore
{
	/*! The mixer channel the rack belongs to, as the "ch-<n>" id this surface
	 *  addresses it by, re-resolved at undo time. The ID and not the channel's
	 *  POSITION (SPEC-stable-ids.md slice 2): a step recorded against a
	 *  position would write its parameters into a DIFFERENT channel once a
	 *  sibling channel was deleted or the mixer reordered, which is the defect
	 *  the persistent id exists to remove - and the id is what the command's
	 *  reply and its transaction carry, so the step and the wire agree. It is
	 *  the string form because that is what resolveRack() takes.
	 */
	QString channelId;
	int macro = 0;
	float value = 0.0f;
	std::vector<std::pair<RackMacroTarget, float>> parameters;
};

//! Writes @a restore's macro value and every (target, value) pair back.
//! Allocates on the control thread (resolution) - it is an undo step, never an
//! audio path.
LMMS_EXPORT void writeMacroAssignments(const MacroRestore& restore);

} // namespace lmms

#endif // LMMS_CONTROL_RACK_SUPPORT_H
