/*
 * ControlCommandsRackZones.cpp - the zone half of the rack.* command group
 *                                (SPEC A11-A16): a key range and a velocity
 *                                range mapped to one of the rack's chains.
 *
 * The model is include/RackZones.h and it is persisted as <zone> children of
 * the channel's existing <rack> element - no second container. What this file
 * adds is the surface: create, drop, and the lookup an agent can ask about.
 *
 * HONEST SCOPE, and it is the reason docs/KNOWN-LIMITATIONS.md and the release
 * notes carry a line for it: the rack moves one stereo block, so it has no
 * per-note input to consult, and nothing in this build calls RackZones::resolve
 * to pick a chain while a note plays. What is here is the persisted, validated,
 * queryable zone model and its resolver - not note routing.
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

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackZones.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The zone a set of args describes. The ranges default to the whole space, so
//! a caller that states only keys gets a key zone and one that states only
//! velocities gets a velocity zone - the two are one object (see RackZones.h).
RackZone zoneFromArgs(const QJsonObject& args)
{
	RackZone zone;
	zone.lowKey = args.value(QStringLiteral("low_key")).toInt(RackZoneMinKey);
	zone.highKey = args.value(QStringLiteral("high_key")).toInt(RackZoneMaxKey);
	zone.lowVelocity = args.value(QStringLiteral("low_velocity")).toInt(RackZoneMinVelocity);
	zone.highVelocity = args.value(QStringLiteral("high_velocity")).toInt(RackZoneMaxVelocity);
	zone.chain = args.value(QStringLiteral("chain")).toInt(0);
	zone.sample = args.value(QStringLiteral("sample")).toString();
	return zone;
}

//! The typed refusal for a zone the engine's own bounds reject, naming the
//! bound that failed rather than only saying "invalid".
ControlResult zoneRefusal(const RackZone& zone, int chainCount)
{
	if (zone.lowKey < RackZoneMinKey || zone.highKey > RackZoneMaxKey || zone.lowKey > zone.highKey)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("key range %1..%2 is not inside %3..%4")
				.arg(zone.lowKey)
				.arg(zone.highKey)
				.arg(RackZoneMinKey)
				.arg(RackZoneMaxKey));
	}
	if (zone.lowVelocity < RackZoneMinVelocity || zone.highVelocity > RackZoneMaxVelocity ||
		zone.lowVelocity > zone.highVelocity)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("velocity range %1..%2 is not inside %3..%4")
				.arg(zone.lowVelocity)
				.arg(zone.highVelocity)
				.arg(RackZoneMinVelocity)
				.arg(RackZoneMaxVelocity));
	}
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'chain' %1 is not a chain of this rack (0..%2)")
			.arg(zone.chain)
			.arg(chainCount - 1));
}

void registerZoneAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.zone_add");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("zone_add");
	cmd.description = QStringLiteral("Add a key/velocity zone to a channel's rack: an inclusive "
		"key range (0..127) and an inclusive velocity range (0..200) mapped to one of the rack's "
		"chains, with an optional sample reference. Returns its zone-<n> id. Reversible through "
		"the ProjectJournal (an action checkpoint removes the zone this command created).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("low_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("high_key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("low_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
		{QStringLiteral("high_velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
		{QStringLiteral("chain"), integerProperty()},
		{QStringLiteral("sample"), stringProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("chain")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("zone"), stringProperty()},
		{QStringLiteral("zone_count"), integerProperty()},
		{QStringLiteral("zone_state"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const RackZone zone = zoneFromArgs(args);
		if (!isValidRackZone(zone, rack.chainCount())) { return zoneRefusal(zone, rack.chainCount()); }

		const QString channelIdText = channelIdOf(channel);
		const int before = rack.zones().zoneCount();
		const int index = rack.zones().addZone(zone);
		// A created zone has no before-state; the inverse is the operation, one
		// action step (SPEC A16 deliverable 5). The step drops the zone the
		// command appended (the last one at creation time), the same shape
		// rack.add_chain uses, so a later edit to another zone cannot make the
		// undo eat a zone nobody asked about.
		addUndoStep(
			[channelIdText, before]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current == nullptr) { return; }
				if (current->zones().zoneCount() <= before) { return; }
				current->zones().removeZone(current->zones().zoneCount() - 1);
			},
			[channelIdText, zone]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->zones().addZone(zone); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("zone"), zoneId(index));
		result.insert(QStringLiteral("zone_count"), rack.zones().zoneCount());
		result.insert(QStringLiteral("zone_state"), zoneJson(zone, index));
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("zone_count"), rack.zones().zoneCount() - 1);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.zone_remove"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("zone"), zoneId(index)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step removes the zone this "
					"command created, through the same RackZones::removeZone rack.zone_remove "
					"uses; a zone carries only the values it was created with")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerZoneRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.zone_remove");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("zone_remove");
	cmd.description = QStringLiteral("Drop a zone from a channel's rack. Reversible through the "
		"ProjectJournal (the recorded undo step re-inserts the captured zone at its index).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("zone"), stringProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("zone")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("removed"), stringProperty()},
		{QStringLiteral("zone_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int index = resolveZoneIndex(rack, args.value(QStringLiteral("zone")).toString(),
			&error);
		if (index < 0) { return error; }

		const RackZone zone = *rack.zones().zone(index);
		const QString channelIdText = channelIdOf(channel);
		rack.zones().removeZone(index);
		// A zone is pure data, so re-inserting the captured one at its index
		// restores the list exactly - including where it sits, which is what
		// RackZones::resolve's first-match rule reads.
		addUndoStep(
			[channelIdText, index, zone]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->zones().insertZone(index, zone); }
			},
			[channelIdText, index]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->zones().removeZone(index); }
			});

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("removed"), zoneId(index));
		result.insert(QStringLiteral("zone_count"), rack.zones().zoneCount());
		QJsonObject beforeState = zoneJson(zone, index);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.zone_add"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("zone"), zoneId(index)}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured "
					"zone at its own index, so the list - and the order the first-match rule "
					"reads - comes back exactly")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerZoneResolve(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.zone_resolve");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("zone_resolve");
	cmd.description = QStringLiteral("Ask which of a channel's rack zones a note falls into: the "
		"first zone, in the order it was added, whose key range and velocity range both contain "
		"(key, velocity). Read-only. NOTE: nothing in this build consults a zone while a note "
		"plays - see docs/KNOWN-LIMITATIONS.md.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("key"), integerProperty(RackZoneMinKey, RackZoneMaxKey)},
		{QStringLiteral("velocity"), integerProperty(RackZoneMinVelocity, RackZoneMaxVelocity)},
	}, {QStringLiteral("channel"), QStringLiteral("key"), QStringLiteral("velocity")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("key"), integerProperty()},
		{QStringLiteral("velocity"), integerProperty()},
		{QStringLiteral("matched"), booleanProperty()},
		{QStringLiteral("zone"), stringProperty()},
		{QStringLiteral("zone_state"), objectProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int key = static_cast<int>(args.value(QStringLiteral("key")).toDouble());
		const int velocity = static_cast<int>(args.value(QStringLiteral("velocity")).toDouble());
		const int index = rack.zones().resolve(key, velocity);

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdOf(channel));
		result.insert(QStringLiteral("key"), key);
		result.insert(QStringLiteral("velocity"), velocity);
		result.insert(QStringLiteral("matched"), index >= 0);
		result.insert(QStringLiteral("zone"), index >= 0 ? zoneId(index) : QString());
		result.insert(QStringLiteral("zone_state"), index >= 0
			? zoneJson(*rack.zones().zone(index), index)
			: QJsonObject());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerRackZoneCommands(ControlRegistry& registry)
{
	registerZoneAdd(registry);
	registerZoneRemove(registry);
	registerZoneResolve(registry);
}

} // namespace lmms
