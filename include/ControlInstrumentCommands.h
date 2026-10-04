/*
 * ControlInstrumentCommands.h - 0.5.0 F2 (MX-112, MX-114, MX-120): the
 *                               instrument verbs of the plugin.*, rack.* and
 *                               modulator.* groups, and what they share.
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

#ifndef LMMS_CONTROL_INSTRUMENT_COMMANDS_H
#define LMMS_CONTROL_INSTRUMENT_COMMANDS_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;
class InstrumentTrack;
struct RackZone;

/*! Registered from their groups' own registration functions (registerPluginCommands,
 *  registerRackCommands, registerModulatorCommands), so no shared registry file changes. */
//! plugin.output_buses / output_bus_route / output_buses_create_channels (MX-114, ControlCommandsPluginBuses.cpp)
LMMS_EXPORT void registerPluginBusCommands(ControlRegistry& registry);
//! plugin.midi_parts / midi_part_add / midi_part_remove (MX-114, ControlCommandsPluginParts.cpp)
LMMS_EXPORT void registerPluginPartCommands(ControlRegistry& registry);
//! rack.layers / layer_add / layer_remove / layer_zone_add / layer_zone_remove (MX-112, ControlCommandsRackLayers.cpp)
LMMS_EXPORT void registerRackLayerCommands(ControlRegistry& registry);
//! modulator.voice_set / instrument_target_set (MX-120, ControlCommandsModulatorVoices.cpp)
LMMS_EXPORT void registerModulatorVoiceCommands(ControlRegistry& registry);

namespace control
{

//! The instrument track a "trk-<n>" names; nullptr and a typed refusal for anything else.
LMMS_EXPORT InstrumentTrack* resolveInstrumentTarget(const QString& id, ControlResult* error);

//! Runs @a fn with the instrument track args' "target" names, or answers resolveInstrumentTarget's refusal.
template <typename Fn>
auto withInstrumentTarget(const QJsonObject& args, Fn&& fn) -> ControlResult
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrumentTarget(args.value(QStringLiteral("target")).toString(), &error);
	return track != nullptr ? fn(track) : error;
}

//! A zone from the rack-zone argument names (low_key, high_key, low_velocity, high_velocity), with
//! @a chain as the chain/layer/channel it names.
LMMS_EXPORT RackZone zoneArgs(const QJsonObject& args, int chain);

//! The typed refusal for a zone outside the engine's bounds, or naming none of @a count chains.
LMMS_EXPORT ControlResult zoneArgsRefusal(const RackZone& zone, int count, const QString& chainName);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_INSTRUMENT_COMMANDS_H
