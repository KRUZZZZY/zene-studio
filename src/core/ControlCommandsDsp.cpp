/*
 * ControlCommandsDsp.cpp - dsp.get_state, the read-back of every device chain
 *                          (SPEC A11-A14).
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
#include "ControlRegistry.h"
#include "Effect.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

namespace
{

QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {})
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject stringProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
}

QJsonObject intProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}};
}

//! One chain as dsp.get_state reports it. The device ids are the same fx-<n>
//! ids plugin.unload / bypass / param_* accept, so a caller can chain the two.
QJsonObject chainJson(const ControlTarget& target)
{
	QJsonArray devices;
	const std::vector<Effect*>& chain = target.chain->effects();
	for (int i = 0; i < static_cast<int>(chain.size()); ++i)
	{
		devices.append(controlEffectJson(chain[static_cast<std::size_t>(i)], i));
	}

	QJsonObject out;
	out.insert(QStringLiteral("id"), target.id);
	out.insert(QStringLiteral("kind"), target.kind);
	out.insert(QStringLiteral("type"), target.typeName);
	out.insert(QStringLiteral("count"), devices.size());
	out.insert(QStringLiteral("devices"), devices);

	InstrumentTrack* track = target.instrumentTrack;
	if (track != nullptr && track->instrument() != nullptr)
	{
		const Instrument* instrument = track->instrument();
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), QStringLiteral("inst"));
		entry.insert(QStringLiteral("plugin"), QString::fromUtf8(instrument->descriptor()->name));
		entry.insert(QStringLiteral("display_name"),
			QString::fromUtf8(instrument->descriptor()->displayName));
		entry.insert(QStringLiteral("parameters"),
			controlParameterList(controlInstrumentParameters(
				const_cast<Instrument*>(instrument))));
		out.insert(QStringLiteral("instrument"), entry);
	}
	return out;
}

void appendTrackChains(QJsonArray* chains, bool withDevicesOnly)
{
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		ControlTarget target;
		ControlResult ignored;
		if (!resolveControlTarget(control::trackId(i), &target, &ignored)) { continue; }
		if (withDevicesOnly && target.chain->effects().empty() &&
			target.instrumentTrack == nullptr)
		{
			continue;
		}
		chains->append(chainJson(target));
	}
}

void appendChannelChains(QJsonArray* chains, bool withDevicesOnly)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; mixer != nullptr && i < static_cast<int>(mixer->numChannels()); ++i)
	{
		ControlTarget target;
		ControlResult ignored;
		if (!resolveControlTarget(control::channelId(i), &target, &ignored)) { continue; }
		if (withDevicesOnly && target.chain->effects().empty()) { continue; }
		chains->append(chainJson(target));
	}
}

int countDevices(const QJsonArray& chains)
{
	int devices = 0;
	for (const QJsonValue& chain : chains)
	{
		devices += chain.toObject().value(QStringLiteral("count")).toInt();
	}
	return devices;
}

QJsonObject allChains()
{
	QJsonArray chains;
	appendTrackChains(&chains, true);
	appendChannelChains(&chains, true);

	QJsonObject out;
	out.insert(QStringLiteral("chains"), chains);
	out.insert(QStringLiteral("count"), chains.size());
	out.insert(QStringLiteral("device_count"), countDevices(chains));
	return out;
}

QJsonObject oneChain(const ControlTarget& target)
{
	QJsonArray chains;
	chains.append(chainJson(target));

	QJsonObject out;
	out.insert(QStringLiteral("chains"), chains);
	out.insert(QStringLiteral("count"), 1);
	out.insert(QStringLiteral("device_count"), countDevices(chains));
	return out;
}

} // namespace

void registerDspCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("dsp.get_state");
	cmd.group = QStringLiteral("dsp");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Every device chain with its fx-<n> instances, each one's "
		"parameter values and, on an instrument track, its 'inst' entry with the instrument's "
		"parameters. With 'target' it reads exactly that target, an empty chain included; "
		"without it, every track and mixer channel that carries at least one device.");
	cmd.argsSchema = schemaObject({{QStringLiteral("target"), stringProperty()}});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("chains"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("count"), intProperty()},
		{QStringLiteral("device_count"), intProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		const QString id = args.value(QStringLiteral("target")).toString();
		if (id.isEmpty()) { return ControlResult::success(allChains()); }

		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(id, &target, &error)) { return error; }
		return ControlResult::success(oneChain(target));
	};
	registry.registerCommand(cmd);
}

} // namespace lmms
