/*
 * ControlCommandsRackWrap.cpp - rack.wrap: move a channel's devices into a chain of its rack
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
/*! 0.5.0 L10 (MX-103): wrap devices into a rack. rack.wrap moves devices of a channel's own chain
 *  into a new chain of its rack, in order and with their settings. Wrapping every device selects the
 *  new chain, so the channel sounds exactly as before and further chains can be layered beside it;
 *  wrapping some makes them a parallel layer beside what stays (the rack's parallel selection).
 *  One undo step puts the devices, the chains and the selection back. */

#include <memory>

#include <QJsonArray>

#include "ControlDeviceMoves.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Rack.h"

namespace lmms
{

using namespace control;

namespace
{

//! The devices a call names on @a chain ('plugins', fx-<n> ids; every device when left out).
bool namedDevices(const QJsonObject& args, const ControlTarget& target, std::vector<Effect*>* out, ControlResult* error)
{
	if (!args.contains(QStringLiteral("plugins")))
	{
		*out = target.chain->effects();
		return true;
	}
	for (const QJsonValue& id : args.value(QStringLiteral("plugins")).toArray())
	{
		Effect* effect = resolveControlEffect(target, id.toString(), error);
		if (effect == nullptr) { return false; }
		out->push_back(effect);
	}
	return true;
}

//! Add the wrapping chain and select @a selectedAfter, recording a step whose undo removes the chain and
//! puts the selection back (it runs after the moves are undone) and whose redo adds it again.
int addWrappingChain(const QString& channel, int selectedAfter)
{
	ControlResult ignored;
	Rack* rack = resolveRack(channel, &ignored);
	const int chains = rack->chainCount();
	const int selectedBefore = rack->selectedChain();
	const auto undo = [channel, chains, selectedBefore]() {
		ControlResult unused;
		Rack* current = resolveRack(channel, &unused);
		if (current == nullptr) { return; }
		while (current->chainCount() > chains) { current->removeChain(current->chainCount() - 1); }
		current->setSelectedChain(selectedBefore);
	};
	const auto redo = [channel, chains, selectedAfter]() {
		ControlResult unused;
		Rack* current = resolveRack(channel, &unused);
		if (current == nullptr || current->chainCount() != chains) { return; }
		current->addChain();
		current->setSelectedChain(selectedAfter);
	};
	addUndoStep(undo, redo);
	const int index = rack->addChain();
	rack->setSelectedChain(selectedAfter);
	return index;
}

ControlResult wrap(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	const QString channelId = args.value(QStringLiteral("channel")).toString();
	MixerChannel* channel = resolveRackChannel(channelId, &error);
	if (channel == nullptr || !resolveControlTarget(channelId, &target, &error)) { return error; }
	std::vector<Effect*> devices;
	if (!namedDevices(args, target, &devices, &error)) { return error; }
	if (devices.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("the channel's chain has no devices to wrap")); }
	if (!devicesRecordable(devices))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device's settings are too large to record for an undo"));
	}
	Rack& rack = channel->m_rack;
	const bool all = devices.size() == target.chain->effects().size();
	QStringList states;
	for (Effect* effect : devices) { states << controlEffectStateXml(effect); }
	const QString id = channelIdOf(channel);
	const int index = rack.chainCount();
	const int selected = all ? index : Rack::Parallel;
	addWrappingChain(id, selected);
	removeDevicesRecorded(target.chain, devices);
	const QJsonArray ids = recreateDevices([id, index]() -> EffectChain* {
		ControlResult unused;
		Rack* current = resolveRack(id, &unused);
		return current != nullptr && index < current->chainCount() ? current->chain(index) : nullptr;
	}, states, -1, &error);
	const QJsonObject result{{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("chain"), index},
		{QStringLiteral("plugins"), ids}, {QStringLiteral("selected"), selected},
		{QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), channelIdOf(channel)}}, QStringLiteral("control.undo"),
			{}, true, QStringLiteral("action checkpoints: the recorded undo steps take the wrapped devices out of the new chain, put "
				"them back in the channel's chain at their slots, and remove the chain and restore the selection"))}};
	return ControlResult::success(result);
}

} // namespace

void registerRackWrapCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.wrap");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("wrap");
	cmd.description = QStringLiteral("Wrap devices of a channel's own chain into a new chain of its rack, in order and with their "
		"settings: 'plugins' (fx-<n> ids) or, left out, every device. Wrapping every device selects the new chain, so the "
		"channel sounds as before and more chains can be layered beside it (rack.add_chain, rack.set_selected parallel); "
		"wrapping some makes them a parallel layer beside the devices that stay. One undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("plugins"), arrayProperty()}},
		{QStringLiteral("channel")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = wrap;
	registry.registerCommand(cmd);
}

} // namespace lmms
