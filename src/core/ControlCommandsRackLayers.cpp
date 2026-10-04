/*
 * ControlCommandsRackLayers.cpp - 0.5.0 MX-112: instrument layers - several
 *                                 instruments on one track, each with key and
 *                                 velocity zones (rack.layer*).
 *
 * The model is include/InstrumentLayers.h. Its zones ARE the rack's zone model (RackZones: the same
 * struct, bounds, refusals and <zone> persistence rack.zone_add uses), with `chain` naming a layer; what
 * differs is the rule - a layer rack STACKS, so a note plays on every layer a matching zone names.
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

#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Instrument.h"
#include "InstrumentLayers.h"
#include "InstrumentTrack.h"
#include "RackZones.h"

namespace lmms
{

using namespace control;

namespace
{

const QString kGroup = QStringLiteral("rack");

QString layerId(int index) { return QStringLiteral("layer-") + QString::number(index); }

QJsonObject layerJson(const InstrumentTrack* track, int index)
{
	const Instrument* instrument = track->instrument();
	QJsonObject out;
	out.insert(QStringLiteral("id"), layerId(index));
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("base"), index == 0);
	out.insert(QStringLiteral("plugin"), instrument != nullptr ? QString::fromUtf8(instrument->descriptor()->name) : QString());
	out.insert(QStringLiteral("display_name"), instrument != nullptr ? instrument->displayName() : QString());
	return out;
}

QJsonObject layerZoneJson(const RackZone& zone, int index)
{
	QJsonObject out = zoneJson(zone, index);
	out.insert(QStringLiteral("layer"), layerId(zone.chain));
	for (const char* key : {"chain", "sample", "sample_loaded", "sample_frames"}) { out.remove(QLatin1String(key)); }
	return out;
}

QJsonObject layersState(InstrumentTrack* track)
{
	const InstrumentLayers& layers = track->layers();
	QJsonArray list{layerJson(track, 0)};
	for (int i = 1; i <= layers.layerCount(); ++i) { list.append(layerJson(layers.layer(i), i)); }
	QJsonArray zones;
	for (int i = 0; i < layers.zones().zoneCount(); ++i) { zones.append(layerZoneJson(*layers.zones().zone(i), i)); }
	QJsonObject out;
	out.insert(QStringLiteral("target"), trackIdOf(track));
	out.insert(QStringLiteral("layers"), list);
	out.insert(QStringLiteral("layer_count"), layers.layerCount());
	out.insert(QStringLiteral("max_layers"), InstrumentLayers::MaxLayers);
	out.insert(QStringLiteral("zones"), zones);
	return out;
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("layers"), arrayProperty()},
		{QStringLiteral("layer_count"), integerProperty()},
		{QStringLiteral("max_layers"), integerProperty()},
		{QStringLiteral("zones"), arrayProperty()},
		{QStringLiteral("layer"), stringProperty()},
		{QStringLiteral("zone"), stringProperty()},
		{QStringLiteral("removed"), stringProperty()},
	});
}

//! The track's layer rack again by id (an undo step outlives the pointer).
InstrumentLayers* layersById(const QString& trackIdText)
{
	ControlResult ignored;
	InstrumentTrack* track = resolveInstrumentTarget(trackIdText, &ignored);
	return track != nullptr ? &track->layers() : nullptr;
}

std::vector<RackZone> zoneList(const RackZones& zones)
{
	std::vector<RackZone> list;
	for (int i = 0; i < zones.zoneCount(); ++i) { list.push_back(*zones.zone(i)); }
	return list;
}

void registerLayers(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.layers");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("layers");
	cmd.description = QStringLiteral("Read an instrument track's layer rack: layer-0 is the track's own instrument, "
		"layer-1.. the instruments layered on it, and the key/velocity zones that pick which layers a note "
		"plays on (every layer a matching zone names sounds - a layer rack stacks). Read-only.");
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), stringProperty()}}, {QStringLiteral("target")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [](InstrumentTrack* track) { return ControlResult::success(layersState(track)); });
	};
	registry.registerCommand(cmd);
}

//! The instrument module and key a dev-<n> instrument entry loads with; false with a typed refusal.
bool instrumentModule(const QString& device, QString* plugin, Plugin::Descriptor::SubPluginFeatures::Key* key,
	bool* useKey, ControlResult* error)
{
	ControlDeviceEntry entry;
	int index = -1;
	if (!controlDeviceById(device, &entry, &index, error)) { return false; }
	if (entry.kind != QLatin1String("instrument"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is an effect: a layer holds an instrument").arg(device));
		return false;
	}
	return controlDeviceModule(entry, plugin, key, useKey, error) && controlPluginIsInstantiable(*plugin, error);
}

void recordLayerAdd(const QString& trackIdText, int index, const QString& xml, const std::vector<RackZone>& zones)
{
	addUndoStep([trackIdText, index]() {
		if (InstrumentLayers* layers = layersById(trackIdText)) { layers->removeLayer(index); }
	}, [trackIdText, index, xml, zones]() {
		InstrumentLayers* layers = layersById(trackIdText);
		if (layers != nullptr && layers->insertLayer(index, xml) == index) { layers->zones().assign(zones); }
	});
}

ControlResult addLayer(InstrumentTrack* track, const QString& device)
{
	QString plugin;
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool useKey = false;
	ControlResult error;
	if (!instrumentModule(device, &plugin, &key, &useKey, &error)) { return error; }
	InstrumentLayers& layers = track->layers();
	if (layers.layerCount() >= InstrumentLayers::MaxLayers)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the layer rack is full (%1 layers)").arg(InstrumentLayers::MaxLayers));
	}
	const int index = layers.addLayer(plugin, useKey ? &key : nullptr);
	if (index < 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the engine could not load instrument '%1'").arg(plugin));
	}
	const QString trackIdText = trackIdOf(track);
	recordLayerAdd(trackIdText, index, layers.layerXml(index), zoneList(layers.zones()));
	QJsonObject result = layersState(track);
	result.insert(QStringLiteral("layer"), layerId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("layer_count"), index - 1}}, QStringLiteral("rack.layer_remove"),
		QJsonObject{{QStringLiteral("target"), trackIdText}, {QStringLiteral("layer"), layerId(index)}}, true,
		QStringLiteral("action checkpoint: the recorded undo step removes the layer this command added, with the "
			"zone it was given; its redo re-creates the layer from the state captured after the add")));
	return ControlResult::success(result);
}

void registerLayerAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.layer_add");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("layer_add");
	cmd.description = QStringLiteral("Layer another instrument (an instrument dev-<n>) on an instrument track. "
		"It plays into the track's mixer channel, follows the track's volume, pan and mute, and gets a "
		"whole-keyboard zone (rack.layer_zone_add / layer_zone_remove narrow it). Its parameters are reached "
		"with plugin.param_get / param_set as plugin 'layer-<n>'. Saved with the track. Reversible through "
		"the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("device"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("device")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) {
			return addLayer(track, args.value(QStringLiteral("device")).toString());
		});
	};
	registry.registerCommand(cmd);
}

ControlResult removeLayer(InstrumentTrack* track, const QString& id)
{
	InstrumentLayers& layers = track->layers();
	const int index = idToIndex(id, QStringLiteral("layer-"));
	if (layers.layer(index) == nullptr)
	{
		return ControlResult::failure(index == 0 ? ControlErrorKind::Refused : ControlErrorKind::NotFound,
			index == 0 ? QStringLiteral("layer-0 is the track's own instrument (plugin.load replaces it)")
				: QStringLiteral("no layer '%1' (the track has %2)").arg(id).arg(layers.layerCount()));
	}
	const QString xml = layers.layerXml(index);
	const std::vector<RackZone> zones = zoneList(layers.zones());
	layers.removeLayer(index);
	const QString trackIdText = trackIdOf(track);
	addUndoStep([trackIdText, index, xml, zones]() {
		InstrumentLayers* current = layersById(trackIdText);
		if (current != nullptr && current->insertLayer(index, xml) == index) { current->zones().assign(zones); }
	}, [trackIdText, index]() {
		if (InstrumentLayers* current = layersById(trackIdText)) { current->removeLayer(index); }
	});
	QJsonObject result = layersState(track);
	result.insert(QStringLiteral("removed"), layerId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("layer"), layerId(index)}, {QStringLiteral("layer_count"), layers.layerCount() + 1}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step re-creates the layer from its captured track "
			"state (its instrument and the instrument's own state) at its index, and restores every zone")));
	return ControlResult::success(result);
}

void registerLayerRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.layer_remove");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("layer_remove");
	cmd.description = QStringLiteral("Remove a layer (layer-<n>, n >= 1) from an instrument track's layer rack; its "
		"zones go with it and the later layers move down one. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("layer"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("layer")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) {
			return removeLayer(track, args.value(QStringLiteral("layer")).toString());
		});
	};
	registry.registerCommand(cmd);
}

ControlResult addLayerZone(InstrumentTrack* track, const QJsonObject& args)
{
	InstrumentLayers& layers = track->layers();
	const QString layerText = args.value(QStringLiteral("layer")).toString();
	const RackZone zone = zoneArgs(args, idToIndex(layerText, QStringLiteral("layer-")));
	if (!isValidRackZone(zone, layers.layerCount() + 1)) { return zoneArgsRefusal(zone, layers.layerCount() + 1, QStringLiteral("layer")); }
	const int before = layers.zones().zoneCount();
	const int index = layers.zones().addZone(zone);
	const QString trackIdText = trackIdOf(track);
	addUndoStep([trackIdText, before]() {
		InstrumentLayers* current = layersById(trackIdText);
		if (current != nullptr && current->zones().zoneCount() > before) { current->zones().removeZone(current->zones().zoneCount() - 1); }
	}, [trackIdText, zone]() {
		if (InstrumentLayers* current = layersById(trackIdText)) { current->zones().addZone(zone); }
	});
	QJsonObject result = layersState(track);
	result.insert(QStringLiteral("zone"), zoneId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(QJsonObject{{QStringLiteral("zone_count"), before}},
		QStringLiteral("rack.layer_zone_remove"),
		QJsonObject{{QStringLiteral("target"), trackIdText}, {QStringLiteral("zone"), zoneId(index)}}, true,
		QStringLiteral("action checkpoint: the recorded undo step drops the zone this command appended "
			"(RackZones::removeZone), the shape rack.zone_add uses")));
	return ControlResult::success(result);
}

void registerLayerZoneAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.layer_zone_add");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("layer_zone_add");
	cmd.description = QStringLiteral("Add a key/velocity zone to an instrument track's layer rack: notes in the key "
		"range (0..127) and velocity range (0..200) play on 'layer' (layer-<n>; layer-0 is the track's own "
		"instrument, which plays every note until it has a zone of its own). Reversible through the "
		"ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("layer"), stringProperty()},
		{QStringLiteral("low_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("high_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("low_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
		{QStringLiteral("high_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
	}, {QStringLiteral("target"), QStringLiteral("layer")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) { return addLayerZone(track, args); });
	};
	registry.registerCommand(cmd);
}

ControlResult removeLayerZone(InstrumentTrack* track, const QString& id)
{
	RackZones& zones = track->layers().zones();
	const int index = idToIndex(id, QStringLiteral("zone-"));
	if (zones.zone(index) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no zone '%1' (the layer rack has %2)").arg(id).arg(zones.zoneCount()));
	}
	const RackZone zone = *zones.zone(index);
	zones.removeZone(index);
	const QString trackIdText = trackIdOf(track);
	addUndoStep([trackIdText, index, zone]() {
		if (InstrumentLayers* current = layersById(trackIdText)) { current->zones().insertZone(index, zone); }
	}, [trackIdText, index]() {
		if (InstrumentLayers* current = layersById(trackIdText)) { current->zones().removeZone(index); }
	});
	QJsonObject result = layersState(track);
	result.insert(QStringLiteral("removed"), zoneId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(layerZoneJson(zone, index),
		QStringLiteral("rack.layer_zone_add"), QJsonObject{{QStringLiteral("target"), trackIdText}}, true,
		QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured zone at its own index")));
	return ControlResult::success(result);
}

void registerLayerZoneRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.layer_zone_remove");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("layer_zone_remove");
	cmd.description = QStringLiteral("Drop a zone (zone-<n>) from an instrument track's layer rack. A layer left "
		"with no zone plays no note. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("zone"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("zone")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) {
			return removeLayerZone(track, args.value(QStringLiteral("zone")).toString());
		});
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerRackLayerCommands(ControlRegistry& registry)
{
	registerLayers(registry);
	registerLayerAdd(registry);
	registerLayerRemove(registry);
	registerLayerZoneAdd(registry);
	registerLayerZoneRemove(registry);
}

} // namespace lmms
