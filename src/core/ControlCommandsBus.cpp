/*
 * ControlCommandsBus.cpp - the bus.* command group (SPEC A11-A16).
 *
 * Feature row 29 of docs/FEATURE-LIST-0.3.0.md ("Audio ports / AudioBus"), the
 * parallel-bus half. The engine side exists and is proven by registered tests
 * (tests/src/core/AudioBusTest.cpp, AudioBusHandleTest.cpp, and the Phase D
 * mixer tests): Mixer::createBusChannel() / Mixer::isBusChannel() (Phase D,
 * task #587) and MixerChannel::isBus() / setIsBus() with the pre-fader default
 * rule in Mixer::createChannelSend (src/core/Mixer.cpp:1089). The audit's row 29
 * says "pin and bus topology are reachable only from C++". This file registers
 * the topology.
 *
 * WHAT A BUS IS HERE, so the verbs below are not read as more than they are: a
 * bus is a MixerChannel with m_isBus set. It never receives instrument output
 * (Mixer::mixToChannel refuses) and its incoming sends default to pre-fader, so
 * its input is the sum of what other channels SEND it. Its own output is routed
 * by its regular sends - which is why the set verbs for a bus are the mixer's
 * own: mixer.set_volume and mixer.route_to / mixer.send_to act on any channel,
 * a bus included, and no bus.set_* duplicates them.
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

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Every bus channel, with the PDC and routing view channelLatencyJson gives.
QJsonArray busReport(Mixer* mixer)
{
	QJsonArray buses;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (channel->isBus()) { buses.append(channelLatencyJson(*channel)); }
	}
	return buses;
}

ControlResult handleBusList()
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("this instance has no mixer"));
	}
	const QJsonArray buses = busReport(mixer);
	QJsonObject result;
	result.insert(QStringLiteral("buses"), buses);
	result.insert(QStringLiteral("count"), buses.size());
	result.insert(QStringLiteral("channel_count"), static_cast<int>(mixer->numChannels()));
	result.insert(QStringLiteral("note"),
		QStringLiteral("a bus is a mixer channel with is_bus true; mixer.set_volume, "
			"mixer.route_to and mixer.send_to act on it like any other channel"));
	return ControlResult::success(result);
}

ControlResult handleBusCreate()
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("this instance has no mixer"));
	}

	const int before = static_cast<int>(mixer->numChannels());
	// SPEC A16 deliverable 5, the mixer.add_channel shape: a created channel has
	// no before-state, so the inverse is the OPERATION - ONE action step deletes
	// the bus this command is about to create. A fresh bus carries only
	// defaults (its name, its fader and no sends), so removing it restores the
	// mixer exactly; the before-state records the channel count.
	control::addUndoStep(
		[mixer, before]() {
			if (static_cast<int>(mixer->numChannels()) > before)
			{
				mixer->deleteChannel(static_cast<int>(mixer->numChannels()) - 1);
			}
		},
		[mixer]() { mixer->createBusChannel(); });
	const int index = mixer->createBusChannel();
	MixerChannel* channel = mixer->mixerChannel(index);
	// The new bus's PERSISTENT id (MixerChannel::id()), not its index: the
	// inverse below names the bus this command created, and a ch-<n> addressed
	// by position would name a different channel once an earlier one is removed
	// (SPEC-stable-ids.md slice 2).
	const QString channelIdText = control::channelIdOf(channel);

	QJsonObject result;
	result.insert(QStringLiteral("channel"), channelIdText);
	result.insert(QStringLiteral("index"), index);
	result.insert(QStringLiteral("name"), channel->m_name);
	result.insert(QStringLiteral("is_bus"), channel->isBus());
	result.insert(QStringLiteral("count"), static_cast<int>(mixer->numChannels()));

	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("count"), before);
	QJsonObject transaction = transactionPayload(beforeState,
		QStringLiteral("bus.remove"),
		QJsonObject{{QStringLiteral("channel"), channelIdText}},
		true,
		QStringLiteral("action checkpoint: the recorded undo step deletes the bus this command "
			"created, through the same Mixer::deleteChannel path bus.remove uses; a fresh bus "
			"carries only defaults, so removing it restores the mixer exactly. The before-state "
			"records the channel count"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

ControlResult handleBusRemove(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(
		args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	if (!channel->isBus())
	{
		// Refused before any write: a non-bus channel's removal is the mixer's
		// own command, and saying so beats silently deleting an ordinary
		// channel through a bus.* verb.
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' is not a bus (it has no is_bus flag); remove an ordinary "
				"channel with mixer.remove_channel")
				.arg(control::channelIdOf(channel)));
	}

	Mixer* mixer = Engine::mixer();
	const int index = channel->index();
	const int before = static_cast<int>(mixer->numChannels());
	const QJsonObject beforeState = channelLatencyJson(*channel);
	// Read the id BEFORE the delete: after Mixer::deleteChannel the pointer is
	// gone, and the id is what the caller was told (SPEC-stable-ids.md slice 2
	// - it survives the delete).
	const QString removedId = control::channelIdOf(channel);
	mixer->deleteChannel(index);

	QJsonObject result;
	result.insert(QStringLiteral("removed"), removedId);
	result.insert(QStringLiteral("count"), static_cast<int>(mixer->numChannels()));

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("count_before"), before);
	QJsonObject transaction = transactionPayload(beforeState,
		QStringLiteral("UNIMPLEMENTED: restore a deleted bus channel"),
		inverseArgs,
		false,
		QStringLiteral("snapshot only: a deleted MixerChannel cannot be recreated by "
			"ProjectJournal, and no command creates a bus WITH state - the before-state holds the "
			"bus's name, fader, sends and effects so the state is not lost, only the automatic "
			"replay is missing"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

} // namespace

void registerBusCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("bus.list");
		cmd.group = QStringLiteral("bus");
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("Every parallel bus channel with its stable ch-<n> id, "
			"fader, incoming sends and PDC numbers. Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("buses"), arrayProperty()},
			{QStringLiteral("count"), integerProperty()},
			{QStringLiteral("channel_count"), integerProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleBusList(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("bus.create");
		cmd.group = QStringLiteral("bus");
		cmd.verb = QStringLiteral("create");
		cmd.description = QStringLiteral("Create a parallel bus channel (Mixer::createBusChannel) "
			"and return its new ch-<n> id. A bus never receives instrument output; sends INTO it "
			"default to pre-fader. Reversible: one undo step deletes the bus this created.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("channel"), stringProperty()},
			{QStringLiteral("index"), integerProperty()},
			{QStringLiteral("name"), stringProperty()},
			{QStringLiteral("is_bus"), booleanProperty()},
			{QStringLiteral("count"), integerProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) { return handleBusCreate(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("bus.remove");
		cmd.group = QStringLiteral("bus");
		cmd.verb = QStringLiteral("remove");
		cmd.description = QStringLiteral("Delete a bus channel. Refused for a channel that is not "
			"a bus. NOT reversible: the command records the bus's full state but nothing recreates "
			"a channel with state, so control.undo refuses, typed, and names the fallback.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("channel"), stringProperty()}},
			{QStringLiteral("channel")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("removed"), stringProperty()},
			{QStringLiteral("count"), integerProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleBusRemove(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
