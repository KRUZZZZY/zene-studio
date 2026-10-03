/*
 * ControlCommandsMcu.cpp - surface.mcu_*: connect a Mackie Control surface, read it, page its banks
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

/*! WF-126 (0.5.0 L13): native control-surface support - a Mackie Control Universal surface
 *  (McuSurface) on two of the MIDI client's ports. The connection is device state, not project
 *  state: nothing here is recorded, and what the surface then does records as the commands it
 *  runs. */

#include <QJsonArray>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "McuSurface.h"
#include "MidiClient.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject disconnected()
{
	return {{QStringLiteral("connected"), false}};
}

ControlResult ports(const QJsonObject&)
{
	MidiClient* client = Engine::audioEngine() != nullptr ? Engine::audioEngine()->midiClient() : nullptr;
	if (client == nullptr) { return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("no MIDI client")); }
	return ControlResult::success({{QStringLiteral("inputs"), QJsonArray::fromStringList(client->readablePorts())},
		{QStringLiteral("outputs"), QJsonArray::fromStringList(client->writablePorts())},
		{QStringLiteral("raw"), client->isRaw()}});
}

ControlResult connectSurface(const QJsonObject& args)
{
	if (Engine::audioEngine() == nullptr || Engine::audioEngine()->midiClient() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("no MIDI client"));
	}
	McuSurface* surface = McuSurface::connect(args.value(QStringLiteral("input")).toString(),
		args.value(QStringLiteral("output")).toString());
	if (args.contains(QStringLiteral("bank"))) { surface->setBank(args.value(QStringLiteral("bank")).toInt()); }
	return ControlResult::success(surface->state());
}

ControlResult disconnectSurface(const QJsonObject&)
{
	const bool was = McuSurface::instance() != nullptr;
	McuSurface::disconnect();
	QJsonObject out = disconnected();
	out.insert(QStringLiteral("was_connected"), was);
	return ControlResult::success(out);
}

ControlResult surfaceState(const QJsonObject&)
{
	McuSurface* surface = McuSurface::instance();
	return ControlResult::success(surface != nullptr ? surface->state() : disconnected());
}

ControlResult setBank(const QJsonObject& args)
{
	McuSurface* surface = McuSurface::instance();
	if (surface == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no surface is connected (surface.mcu_connect)")); }
	surface->setBank(args.value(QStringLiteral("bank")).toInt());
	return ControlResult::success(surface->state());
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("surface");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerMcuCommands(ControlRegistry& registry)
{
	add(registry, "mcu_ports", QStringLiteral("The MIDI client's input and output ports, by the names "
		"surface.mcu_connect takes."), objectSchema({}), ports);
	add(registry, "mcu_connect", QStringLiteral("Connect a Mackie Control Universal surface: 'input' and "
		"'output' name the MIDI client's ports (surface.mcu_ports). Its eight strips are a bank of the song's "
		"instrument and sample tracks from 'bank': faders set volume, V-pots pan, mute / solo / rec / select and "
		"the transport buttons run the registry's own commands, and the motor faders and LEDs follow. The strip "
		"LCD (SysEx) is not sent. Replaces a connected surface. Device state: not recorded."),
		objectSchema({{QStringLiteral("input"), stringProperty()}, {QStringLiteral("output"), stringProperty()},
			{QStringLiteral("bank"), integerProperty(0, 4096)}}), connectSurface);
	add(registry, "mcu_disconnect", QStringLiteral("Disconnect the control surface."), objectSchema({}), disconnectSurface);
	add(registry, "mcu_state", QStringLiteral("The control surface: connected or not, its ports, its bank, the "
		"track each strip drives and how many events came in and went out."), objectSchema({}), surfaceState);
	add(registry, "mcu_bank", QStringLiteral("Show the tracks from 'bank' on the surface's strips (the bank and "
		"channel buttons do the same)."), objectSchema({{QStringLiteral("bank"), integerProperty(0, 4096)}},
		{QStringLiteral("bank")}), setBank);
}

} // namespace lmms
