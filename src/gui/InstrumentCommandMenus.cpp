/*
 * InstrumentCommandMenus.cpp - 0.5.0 F2 interface: an instrument track's output buses, MIDI
 *                              parts, layer rack and per-voice modulation (MX-114/112/120)
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

#include "InstrumentCommandMenus.h"

#include <QMenu>

#include "CommandListBuild.h"
#include "CommandLists.h"
#include "CommandForm.h"
#include "ControlEdit.h"
#include "DevicePicker.h"
#include "EffectDeviceMenu.h"
#include "LayerRackPanel.h"
#include "ShellMenuParts.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

//! "<what> - <the track's name>" (its id when it has none to read).
QString titled(const char* what, const QString& track)
{
	ControlResult unknown;
	const Track* resolved = control::resolveTrack(track, &unknown);
	return QObject::tr("%1 - %2").arg(QObject::tr(what), resolved != nullptr ? resolved->name() : track);
}

//! The window a menu entry's dialog belongs to: the first non-menu ancestor of @a menu.
QWidget* menuHost(QMenu* menu)
{
	QWidget* host = menu;
	while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
	return host;
}

} // namespace

namespace CommandLists
{

ListSpec outputBuses(const QString& track)
{
	const QJsonObject where{{QStringLiteral("target"), track}};
	ListSpec out = spec("Output Buses", "plugin.output_buses", "buses", {QStringLiteral("index"), QStringLiteral("name"),
		QStringLiteral("channels"), QStringLiteral("main"), QStringLiteral("active"), QStringLiteral("channel")}, where);
	out.title = titled("Output Buses", track);
	out.summaryKeys = QStringList{QStringLiteral("bus_count"), QStringLiteral("routed_count")};
	const QHash<QString, QString> bus{{QStringLiteral("bus"), QStringLiteral("index")}};
	// Bus 0 is the instrument's main output (it reaches the track as always): the command refuses to route
	// it and says so, which the list's Results log shows.
	out.actions = {act("Route to Channel...", "plugin.output_bus_route", bus, where, true),
		act("Un-route", "plugin.output_bus_route", bus, menuparts::with(where, "channel", QString()))};
	out.extras = {extra("Create a Channel for Each Bus", "plugin.output_buses_create_channels", where, false)};
	return out;
}

ListSpec midiParts(const QString& track)
{
	const QJsonObject where{{QStringLiteral("target"), track}};
	ListSpec out = spec("MIDI Parts", "plugin.midi_parts", "parts", {QStringLiteral("id"), QStringLiteral("midi_channel"),
		QStringLiteral("low_key"), QStringLiteral("high_key"), QStringLiteral("low_velocity"), QStringLiteral("high_velocity")}, where);
	out.title = titled("MIDI Parts", track);
	out.summaryKeys = QStringList{QStringLiteral("part_count")};
	const QHash<QString, QString> part{{QStringLiteral("part"), QStringLiteral("id")}};
	RowAction remove = act("Remove", "plugin.midi_part_remove", part, where);
	remove.confirm = true;
	// Edit's form leaves a field blank to keep its value: plugin.midi_part_set changes only what it is given.
	out.actions = {act("Edit...", "plugin.midi_part_set", part, where, true), remove};
	out.extras = {extra("Add Part...", "plugin.midi_part_add", where)};
	return out;
}

ListSpec layerZones(const QString& track)
{
	const QJsonObject where{{QStringLiteral("target"), track}};
	ListSpec out = spec("Layer Zones", "rack.layers", "zones", {QStringLiteral("zone"), QStringLiteral("layer"),
		QStringLiteral("low_key"), QStringLiteral("high_key"), QStringLiteral("low_velocity"), QStringLiteral("high_velocity")}, where);
	out.title = titled("Layer Zones", track);
	out.summaryKeys = QStringList{QStringLiteral("layer_count"), QStringLiteral("max_layers")};
	RowAction remove = act("Remove", "rack.layer_zone_remove", {{QStringLiteral("zone"), QStringLiteral("zone")}}, where);
	remove.confirm = true;
	remove.caution = QObject::tr("A layer left with no zone plays no note.");
	out.actions = {remove};
	out.extras = {extra("Add Zone...", "rack.layer_zone_add", where)};
	return out;
}

ListSpec voiceModulators(const QString& track)
{
	ListSpec out = spec("Per-Voice Modulators", "modulator.get_state", "modulators", {QStringLiteral("parent_id"),
		QStringLiteral("parent_name"), QStringLiteral("shape"), QStringLiteral("rate"), QStringLiteral("per_voice"),
		QStringLiteral("trigger"), QStringLiteral("parent_target_count")});
	out.title = titled("Per-Voice Modulators", track);
	out.nestedKeys = QStringList{QStringLiteral("source")};  // one row per modulator: its source, with the modulator's id and name
	out.parentFields = QStringList{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("target_count")};
	out.summaryKeys = QStringList{QStringLiteral("modulator_count"), QStringLiteral("max_modulators"), QStringLiteral("driving")};
	const QHash<QString, QString> modulator{{QStringLiteral("modulator"), QStringLiteral("parent_id")}};
	const QJsonObject onTrack{{QStringLiteral("target"), track}};
	RowAction remove = act("Remove", "modulator.remove", modulator);
	remove.confirm = true;
	out.actions = {act("Per-Voice on This Track", "modulator.voice_set", modulator,
			{{QStringLiteral("per_voice"), true}, {QStringLiteral("trigger"), track}}),
		act("Not Per-Voice", "modulator.voice_set", modulator, {{QStringLiteral("per_voice"), false}}),
		act("Assign an Instrument Parameter...", "modulator.instrument_target_set", modulator, onTrack, true), remove};
	out.extras = {extra("New Modulator...", "modulator.create")};
	return out;
}

ListSpec modulationAssignments(const QString& track)
{
	ListSpec out = spec("Modulation Assignments", "modulator.get_state", "modulators", {QStringLiteral("parent_name"),
		QStringLiteral("parameter"), QStringLiteral("track"), QStringLiteral("channel"), QStringLiteral("depth"),
		QStringLiteral("voice_mode"), QStringLiteral("resolved")});
	out.title = titled("Modulation Assignments", track);
	out.nestedKeys = QStringList{QStringLiteral("targets")};  // one row per route, under its modulator
	out.parentFields = QStringList{QStringLiteral("id"), QStringLiteral("name")};
	out.summaryKeys = QStringList{QStringLiteral("driving"), QStringLiteral("max_targets_per_modulator")};
	const QHash<QString, QString> route{{QStringLiteral("modulator"), QStringLiteral("parent_id")},
		{QStringLiteral("target"), QStringLiteral("index")}};
	out.actions = {act("Depth...", "modulator.depth_set", route, {}, true), act("Remove", "modulator.target_remove", route)};
	out.extras = {extra("Assign an Instrument Parameter...", "modulator.instrument_target_set", {{QStringLiteral("target"), track}})};
	return out;
}

} // namespace CommandLists

namespace InstrumentCommandMenus
{

using namespace menuparts;

QString submenuTitle()
{
	return QObject::tr("Instrument Setup");
}

void populate(QMenu* menu, const QString& track)
{
	QMenu* setup = menu->addMenu(submenuTitle());
	const QJsonObject self{{QStringLiteral("target"), track}};
	QMenu* buses = setup->addMenu(tr("Output Buses"));  // MX-114
	list(buses, "Output Buses...", [track] { return CommandLists::outputBuses(track); });
	add(buses, "Route an Output Bus...", "plugin.output_bus_route", self, true);
	add(buses, "Create a Channel for Each Bus", "plugin.output_buses_create_channels", self, false);
	QMenu* parts = setup->addMenu(tr("MIDI Parts"));  // MX-114
	list(parts, "MIDI Parts...", [track] { return CommandLists::midiParts(track); });
	add(parts, "Add a MIDI Part...", "plugin.midi_part_add", self, true);
	QMenu* layers = setup->addMenu(tr("Layers"));  // MX-112
	addCommandEntry(layers, tr("Layer Rack..."), "rack.layers", [layers, track] { showLayerRack(menuHost(layers), track); });
	addCommandEntry(layers, tr("Add a Layer..."), "rack.layer_add", [layers, track] {
		QWidget* host = menuHost(layers);
		const QString chosen = DevicePicker::pick(host, DevicePicker::Kind::Instrument, QObject::tr("Layer an instrument"));
		if (!chosen.isEmpty())
		{
			runCommand(host, CommandCall{QStringLiteral("rack.layer_add"),
				{{QStringLiteral("target"), track}, {QStringLiteral("device"), chosen}}, {}, QString(), false});
		}
	});
	list(layers, "Layer Zones...", [track] { return CommandLists::layerZones(track); });
	QMenu* voices = setup->addMenu(tr("Per-Voice Modulation"));  // MX-120
	list(voices, "Modulators...", [track] { return CommandLists::voiceModulators(track); });
	list(voices, "Modulation Assignments...", [track] { return CommandLists::modulationAssignments(track); });
	add(voices, "Make a Modulator Per-Voice...", "modulator.voice_set", {}, true,
		{{QStringLiteral("per_voice"), true}, {QStringLiteral("trigger"), track}});
	add(voices, "Assign an Instrument Parameter...", "modulator.instrument_target_set", self, true);
}

} // namespace InstrumentCommandMenus

} // namespace lmms::gui
