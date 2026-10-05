/*
 * ControlCommandsPluginBuses.cpp - 0.5.0 MX-114: a multi-output instrument's
 *                                  extra output buses, routed to mixer
 *                                  channels (plugin.output_*).
 *
 * The model is include/InstrumentOutputBuses.h: bus 0 is the instrument's main output and reaches the
 * track as it always has; every other bus plays into the mixer channel it is routed to, or nowhere.
 * The routes are saved with the track (<outputbuses>), by the channel's persistent id.
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

#include <memory>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;

namespace
{

const QString kGroup = QStringLiteral("plugin");

//! The track's instrument's output buses; nullptr and a typed refusal when it has none to route.
InstrumentOutputBuses* busesOf(InstrumentTrack* track, ControlResult* error)
{
	Instrument* instrument = track->instrument();
	InstrumentOutputBuses* buses = instrument != nullptr ? instrument->outputBuses() : nullptr;
	if (buses == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1's instrument has a single output: only a hosted CLAP, VST3 or LV2 instrument "
				"has output buses").arg(trackIdOf(track)));
	}
	return buses;
}

//! Runs @a fn with the target track and its output buses, or answers the refusal.
template <typename Fn>
ControlResult withBuses(const QJsonObject& args, Fn&& fn)
{
	return withInstrumentTarget(args, [&fn](InstrumentTrack* track) {
		ControlResult error;
		InstrumentOutputBuses* buses = busesOf(track, &error);
		return buses != nullptr ? fn(track, *buses) : error;
	});
}

//! The same, re-resolved by id - what an undo step does, since the track may have been rebuilt.
InstrumentOutputBuses* busesById(const QString& trackIdText)
{
	ControlResult ignored;
	InstrumentTrack* track = resolveInstrumentTarget(trackIdText, &ignored);
	return track != nullptr ? busesOf(track, &ignored) : nullptr;
}

QString channelIdOfPersistent(int id)
{
	return id >= 0 ? QStringLiteral("ch-") + QString::number(id) : QString();
}

QJsonObject busJson(const InstrumentOutputBuses& buses, int index)
{
	const InstrumentOutputBus* bus = buses.bus(index);
	QJsonObject out;
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("name"), bus->name);
	out.insert(QStringLiteral("channels"), bus->channels);
	out.insert(QStringLiteral("main"), index == 0);
	out.insert(QStringLiteral("active"), bus->firstChannel >= 0);
	out.insert(QStringLiteral("channel"), index == 0 ? QString() : channelIdOfPersistent(buses.routeOf(index)));
	return out;
}

QJsonObject busesState(InstrumentTrack* track, const InstrumentOutputBuses& buses)
{
	QJsonArray list;
	for (int i = 0; i < buses.busCount(); ++i) { list.append(busJson(buses, i)); }
	QJsonObject out;
	out.insert(QStringLiteral("target"), trackIdOf(track));
	out.insert(QStringLiteral("buses"), list);
	out.insert(QStringLiteral("bus_count"), buses.busCount());
	out.insert(QStringLiteral("routed_count"), buses.routedCount());
	return out;
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("buses"), arrayProperty()},
		{QStringLiteral("bus_count"), integerProperty()},
		{QStringLiteral("routed_count"), integerProperty()},
		{QStringLiteral("created"), arrayProperty()},
	});
}

void registerOutputBuses(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.output_buses");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("output_buses");
	cmd.description = QStringLiteral("List an instrument track's output buses: bus 0 is the instrument's main "
		"output (it reaches the track as always), every other bus - a drum sampler's separate outputs, a "
		"multitimbral synth's part outputs - plays into the mixer channel it is routed to ('channel'), or "
		"nowhere. 'active' is false for a VST3 aux bus the plug-in keeps off until it is routed. Read-only.");
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), stringProperty()}}, {QStringLiteral("target")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		return withBuses(args, [](InstrumentTrack* track, InstrumentOutputBuses& buses) {
			return ControlResult::success(busesState(track, buses));
		});
	};
	registry.registerCommand(cmd);
}

//! The persistent id of the channel @a text names ("" un-routes: -1); false with a typed refusal.
bool routeChannelArg(const QString& text, int* channelId, ControlResult* error)
{
	*channelId = -1;
	if (text.isEmpty()) { return true; }
	MixerChannel* channel = resolveMixerChannel(text, error);
	if (channel == nullptr) { return false; }
	if (channel->isBus())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is a parallel bus: it takes sends only, never an instrument's output").arg(text));
		return false;
	}
	*channelId = channel->id();
	return true;
}

ControlResult routeBus(InstrumentTrack* track, InstrumentOutputBuses& buses, const QJsonObject& args)
{
	const int bus = args.value(QStringLiteral("bus")).toInt(-1);
	if (bus < 1 || bus >= buses.busCount())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no extra output bus %1 (the instrument has buses 1..%2; bus 0 is the main output)")
				.arg(bus).arg(buses.busCount() - 1));
	}
	ControlResult error;
	int channelId = -1;
	if (!routeChannelArg(args.value(QStringLiteral("channel")).toString(), &channelId, &error)) { return error; }
	const int previous = buses.routeOf(bus);
	if (!buses.route(bus, channelId))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the plug-in did not activate output bus %1").arg(bus));
	}
	const QString trackIdText = trackIdOf(track);
	// Only a real change is an undo step: routing a bus to the channel it already
	// uses would otherwise record a no-op step that consumes the next undo.
	if (previous == channelId)
	{
		return ControlResult::success(busesState(track, buses));
	}
	addUndoStep([trackIdText, bus, previous]() {
		if (InstrumentOutputBuses* current = busesById(trackIdText)) { current->route(bus, previous); }
	}, [trackIdText, bus, channelId]() {
		if (InstrumentOutputBuses* current = busesById(trackIdText)) { current->route(bus, channelId); }
	});
	QJsonObject result = busesState(track, buses);
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("bus"), bus}, {QStringLiteral("channel"), channelIdOfPersistent(previous)}},
		QStringLiteral("plugin.output_bus_route"),
		QJsonObject{{QStringLiteral("target"), trackIdText}, {QStringLiteral("bus"), bus},
			{QStringLiteral("channel"), channelIdOfPersistent(previous)}},
		true, QStringLiteral("action checkpoint: the recorded undo step routes the bus back to the channel it "
			"had (or nowhere) through InstrumentOutputBuses::route")));
	return ControlResult::success(result);
}

void registerOutputBusRoute(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.output_bus_route");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("output_bus_route");
	cmd.description = QStringLiteral("Route one of an instrument's extra output buses (1..n) to a mixer channel "
		"('channel': ch-<n>), or un-route it ('channel' empty). The bus bypasses the track's volume, pan and "
		"effects, follows the track's mute and is aligned by the channel's latency compensation. A VST3 aux "
		"bus is activated (with every bus before it) when first routed. Saved with the project. Reversible "
		"through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("bus"), integerProperty(1, InstrumentOutputBuses::MaxBuses - 1)},
		{QStringLiteral("channel"), stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("bus")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withBuses(args, [&args](InstrumentTrack* track, InstrumentOutputBuses& buses) {
			return routeBus(track, buses, args);
		});
	};
	registry.registerCommand(cmd);
}

//! One channel made for one bus: what the redo makes again and the undo takes away.
struct CreatedRoute
{
	int bus = 0;
	QString name;
	int channelId = -1;
};

//! A new mixer channel named @a name with bus @a bus routed to it; its persistent id, or -1 (removed again).
int createChannelFor(InstrumentOutputBuses& buses, int bus, const QString& name)
{
	Mixer* mixer = Engine::mixer();
	const int index = mixer->createChannel();
	MixerChannel* channel = mixer->mixerChannel(index);
	channel->m_name = name;
	if (buses.route(bus, channel->id())) { return channel->id(); }
	mixer->deleteChannel(index);
	return -1;
}

void deleteChannelById(int channelId)
{
	Mixer* mixer = Engine::mixer();
	for (int i = static_cast<int>(mixer->numChannels()) - 1; i > 0; --i)
	{
		if (mixer->mixerChannel(i)->id() == channelId) { mixer->deleteChannel(i); return; }
	}
}

void recordCreation(const QString& trackIdText, const std::vector<CreatedRoute>& created)
{
	auto made = std::make_shared<std::vector<CreatedRoute>>(created);
	addUndoStep([trackIdText, made]() {
		InstrumentOutputBuses* buses = busesById(trackIdText);
		for (auto it = made->rbegin(); it != made->rend(); ++it)
		{
			if (buses != nullptr) { buses->route(it->bus, -1); }
			deleteChannelById(it->channelId);
		}
	}, [trackIdText, made]() {
		InstrumentOutputBuses* buses = busesById(trackIdText);
		for (CreatedRoute& route : *made)
		{
			if (buses != nullptr) { route.channelId = createChannelFor(*buses, route.bus, route.name); }
		}
	});
}

ControlResult createChannels(InstrumentTrack* track, InstrumentOutputBuses& buses)
{
	std::vector<CreatedRoute> created;
	QJsonArray list;
	for (int bus = 1; bus < buses.busCount(); ++bus)
	{
		if (buses.routeOf(bus) >= 0) { continue; }
		CreatedRoute route{bus, QStringLiteral("%1 %2").arg(track->name(), buses.bus(bus)->name), -1};
		route.channelId = createChannelFor(buses, bus, route.name);
		if (route.channelId < 0) { continue; }
		created.push_back(route);
		list.append(QJsonObject{{QStringLiteral("bus"), bus}, {QStringLiteral("channel"), channelIdOfPersistent(route.channelId)}});
	}
	const QString trackIdText = trackIdOf(track);
	if (!created.empty()) { recordCreation(trackIdText, created); }
	QJsonObject result = busesState(track, buses);
	result.insert(QStringLiteral("created"), list);
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("routed_count"), buses.routedCount() - static_cast<int>(created.size())}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step un-routes every bus this command routed and "
			"deletes the channels it created (Mixer::deleteChannel); its redo creates and routes them again")));
	return ControlResult::success(result);
}

void registerCreateChannels(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.output_buses_create_channels");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("output_buses_create_channels");
	cmd.description = QStringLiteral("Create one mixer channel per extra output bus of an instrument that is not "
		"routed yet, named after the track and the bus, and route the bus to it - the multi-output setup in one "
		"step. 'created' lists each bus and its new ch-<n>. One undo step removes the channels again.");
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), stringProperty()}}, {QStringLiteral("target")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withBuses(args, [](InstrumentTrack* track, InstrumentOutputBuses& buses) { return createChannels(track, buses); });
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginBusCommands(ControlRegistry& registry)
{
	registerOutputBuses(registry);
	registerOutputBusRoute(registry);
	registerCreateChannels(registry);
}

} // namespace lmms
