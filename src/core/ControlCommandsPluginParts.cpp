/*
 * ControlCommandsPluginParts.cpp - 0.5.0 MX-114: one instrument track
 *                                  addressing a multitimbral plug-in on
 *                                  several MIDI channels (plugin.midi_part*).
 *
 * The model is include/InstrumentMidiParts.h - a RackZones list whose `chain` is the MIDI channel - so a
 * part is stated and refused exactly as a rack zone is, and persisted as <zone> children of the track's
 * <midiparts> element.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "InstrumentMidiParts.h"
#include "InstrumentTrack.h"
#include "RackZones.h"

namespace lmms
{

namespace control
{

InstrumentTrack* resolveInstrumentTarget(const QString& id, ControlResult* error)
{
	ControlTarget target;
	if (!resolveControlTarget(id, &target, error)) { return nullptr; }
	if (target.instrumentTrack == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is a %2 target: this verb needs an instrument track").arg(target.id, target.typeName));
	}
	return target.instrumentTrack;
}

RackZone zoneArgs(const QJsonObject& args, int chain)
{
	RackZone zone;
	zone.lowKey = args.value(QStringLiteral("low_key")).toInt(RackZoneMinKey);
	zone.highKey = args.value(QStringLiteral("high_key")).toInt(RackZoneMaxKey);
	zone.lowVelocity = args.value(QStringLiteral("low_velocity")).toInt(RackZoneMinVelocity);
	zone.highVelocity = args.value(QStringLiteral("high_velocity")).toInt(RackZoneMaxVelocity);
	zone.chain = chain;
	return zone;
}

ControlResult zoneArgsRefusal(const RackZone& zone, int count, const QString& chainName)
{
	if (zone.lowKey < RackZoneMinKey || zone.highKey > RackZoneMaxKey || zone.lowKey > zone.highKey)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("key range %1..%2 is not inside %3..%4")
			.arg(zone.lowKey).arg(zone.highKey).arg(RackZoneMinKey).arg(RackZoneMaxKey));
	}
	if (zone.lowVelocity < RackZoneMinVelocity || zone.highVelocity > RackZoneMaxVelocity
		|| zone.lowVelocity > zone.highVelocity)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("velocity range %1..%2 is not inside %3..%4")
			.arg(zone.lowVelocity).arg(zone.highVelocity).arg(RackZoneMinVelocity).arg(RackZoneMaxVelocity));
	}
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("there is no %1 %2 (0..%3)").arg(chainName).arg(zone.chain).arg(count - 1));
}

} // namespace control

using namespace control;

namespace
{

const QString kGroup = QStringLiteral("plugin");

QString partId(int index) { return QStringLiteral("part-") + QString::number(index); }

QJsonObject partJson(const RackZone& zone, int index)
{
	QJsonObject out = zoneJson(zone, index);
	out.insert(QStringLiteral("id"), partId(index));
	out.insert(QStringLiteral("midi_channel"), zone.chain + 1);  // 1..16, as a MIDI channel is spoken
	// A part is a zone without a sample: the zone fields that do not apply are not reported.
	for (const char* key : {"chain", "zone", "sample", "sample_loaded", "sample_frames"}) { out.remove(QLatin1String(key)); }
	return out;
}

QJsonObject partsState(InstrumentTrack* track)
{
	const RackZones& zones = track->midiParts().zones();
	QJsonArray parts;
	for (int i = 0; i < zones.zoneCount(); ++i) { parts.append(partJson(*zones.zone(i), i)); }
	QJsonObject out;
	out.insert(QStringLiteral("target"), trackIdOf(track));
	out.insert(QStringLiteral("parts"), parts);
	out.insert(QStringLiteral("part_count"), zones.zoneCount());
	return out;
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("parts"), arrayProperty()},
		{QStringLiteral("part_count"), integerProperty()},
		{QStringLiteral("part"), stringProperty()},
		{QStringLiteral("removed"), stringProperty()},
	});
}

//! The track's part list again by id (an undo step outlives the pointer).
RackZones* partsById(const QString& trackIdText)
{
	ControlResult ignored;
	InstrumentTrack* track = resolveInstrumentTarget(trackIdText, &ignored);
	return track != nullptr ? &track->midiParts().zones() : nullptr;
}

void registerParts(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.midi_parts");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("midi_parts");
	cmd.description = QStringLiteral("List an instrument track's MIDI parts: key and velocity zones that each send "
		"their notes on one MIDI channel (1..16), so one track plays several parts of a multitimbral plug-in. "
		"First match wins; a note no part claims keeps the track's own channel. Read-only.");
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), stringProperty()}}, {QStringLiteral("target")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [](InstrumentTrack* track) { return ControlResult::success(partsState(track)); });
	};
	registry.registerCommand(cmd);
}

ControlResult addPart(InstrumentTrack* track, const QJsonObject& args)
{
	const RackZone zone = zoneArgs(args, args.value(QStringLiteral("midi_channel")).toInt(0) - 1);
	if (!isValidRackZone(zone, InstrumentMidiParts::Channels))
	{
		return zoneArgsRefusal(zone, InstrumentMidiParts::Channels, QStringLiteral("MIDI channel index"));
	}
	RackZones& zones = track->midiParts().zones();
	const int before = zones.zoneCount();
	const int index = zones.addZone(zone);
	const QString trackIdText = trackIdOf(track);
	addUndoStep([trackIdText, before]() {
		RackZones* current = partsById(trackIdText);
		if (current != nullptr && current->zoneCount() > before) { current->removeZone(current->zoneCount() - 1); }
	}, [trackIdText, zone]() {
		if (RackZones* current = partsById(trackIdText)) { current->addZone(zone); }
	});
	QJsonObject result = partsState(track);
	result.insert(QStringLiteral("part"), partId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("part_count"), before}}, QStringLiteral("plugin.midi_part_remove"),
		QJsonObject{{QStringLiteral("target"), trackIdText}, {QStringLiteral("part"), partId(index)}}, true,
		QStringLiteral("action checkpoint: the recorded undo step drops the part this command appended "
			"(RackZones::removeZone), the shape rack.zone_add uses")));
	return ControlResult::success(result);
}

void registerPartAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.midi_part_add");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("midi_part_add");
	cmd.description = QStringLiteral("Add a MIDI part to an instrument track: notes in the key range (0..127) and "
		"velocity range (0..200, the note volume) are sent to the instrument on 'midi_channel' (1..16). "
		"Channel-wide messages (pitch bend, controllers) then go to every part's channel. Saved with the track. "
		"Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("midi_channel"), integerProperty(1, InstrumentMidiParts::Channels)},
		{QStringLiteral("low_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("high_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("low_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
		{QStringLiteral("high_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
	}, {QStringLiteral("target"), QStringLiteral("midi_channel")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) { return addPart(track, args); });
	};
	registry.registerCommand(cmd);
}

ControlResult removePart(InstrumentTrack* track, const QString& id)
{
	RackZones& zones = track->midiParts().zones();
	const int index = idToIndex(id, QStringLiteral("part-"));
	if (zones.zone(index) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no part '%1' (the track has %2)").arg(id).arg(zones.zoneCount()));
	}
	const RackZone zone = *zones.zone(index);
	zones.removeZone(index);
	const QString trackIdText = trackIdOf(track);
	addUndoStep([trackIdText, index, zone]() {
		if (RackZones* current = partsById(trackIdText)) { current->insertZone(index, zone); }
	}, [trackIdText, index]() {
		if (RackZones* current = partsById(trackIdText)) { current->removeZone(index); }
	});
	QJsonObject result = partsState(track);
	result.insert(QStringLiteral("removed"), partId(index));
	result.insert(QStringLiteral("__transaction"), transactionPayload(partJson(zone, index),
		QStringLiteral("plugin.midi_part_add"),
		QJsonObject{{QStringLiteral("target"), trackIdText},
			{QStringLiteral("midi_channel"), zone.chain + 1},
			{QStringLiteral("low_key"), zone.lowKey},
			{QStringLiteral("high_key"), zone.highKey},
			{QStringLiteral("low_velocity"), zone.lowVelocity},
			{QStringLiteral("high_velocity"), zone.highVelocity}}, true,
		QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured part at its own index, "
			"so the first-match order comes back exactly")));
	return ControlResult::success(result);
}

void registerPartRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.midi_part_remove");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("midi_part_remove");
	cmd.description = QStringLiteral("Drop a MIDI part (part-<n>) from an instrument track; a note sounding on its "
		"channel still ends there. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("part"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("part")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) {
			return removePart(track, args.value(QStringLiteral("part")).toString());
		});
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginPartCommands(ControlRegistry& registry)
{
	registerParts(registry);
	registerPartAdd(registry);
	registerPartRemove(registry);
}

} // namespace lmms
