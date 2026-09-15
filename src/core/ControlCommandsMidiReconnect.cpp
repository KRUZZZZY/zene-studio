/*
 * ControlCommandsMidiReconnect.cpp - the read half of the MIDI controller
 *                                    auto-reconnection group: what the engine
 *                                    remembers, and which live ports that
 *                                    memory is matched against
 *                                    (0.3.0 feature-list row 18, OWNER-31
 *                                    item 7, SPEC A11-A16).
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

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlCommandsMidiReconnectShared.h"
#include "ControlRegistry.h"
#include "MidiClient.h"
#include "MidiReconnect.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using namespace midireconnectcontrol;

//! One live port, as midi.clients_list reports it.
/*!
 * 'name' is the full name the engine would subscribe to - and the name it writes
 * into a project's <midiport inports>, which is why the address in front of it
 * matters: it is the part that changes when the device is replugged and the part
 * the re-connection has to replace. 'identity' is the same name with the address
 * taken off, i.e. the part that survives. 'bound' says whether some engine port
 * holds this live name in an assignment right now.
 */
QJsonObject portJson(const MidiReconnect& memory, const QString& name, bool readable)
{
	const int space = name.indexOf(QLatin1Char(' '));
	QJsonObject out;
	out.insert(QStringLiteral("name"), name);
	out.insert(QStringLiteral("address"), midiPortAddress(name));
	out.insert(QStringLiteral("identity"), midiPortIdentity(name));
	out.insert(QStringLiteral("client"), space < 0 ? QString() : name.mid(space + 1).section(
		QLatin1Char(':'), 0, 0));
	out.insert(QStringLiteral("port"), name.section(QLatin1Char(':'), -1));
	out.insert(QStringLiteral("direction"),
		readable ? QStringLiteral("read") : QStringLiteral("write"));
	const QString bound = boundPortOf(memory, readable, name);
	out.insert(QStringLiteral("bound"), !bound.isEmpty());
	out.insert(QStringLiteral("engine_port"), bound);
	return out;
}

//! Every live port of both directions, gathered under its client NAME.
/*!
 * The client name is the middle field of "<client>:<port> <name>:<port name>",
 * and it is what a user calls the device. Grouping by it is what makes the
 * device that came back visible as the same client at a new address.
 *
 * ONE array, built in one pass over both directions, with the index of each
 * client name resolved against THIS array - not against a list that outlives
 * the call. (The first version shared a name list between a readable pass and a
 * writable pass while the array itself was per call, so the writable pass used
 * an index from the readable one and wrote past the end of its own array: a
 * SIGSEGV on every call, caught by the transcript test and confirmed under gdb
 * at QJsonArray::replace.)
 */
QJsonArray clientsJson(const MidiReconnect& memory, const QStringList& readable,
	const QStringList& writable, QJsonArray* ports)
{
	QJsonArray clients;
	QHash<QString, int> indexOfClient;
	for (int direction = 0; direction < 2; ++direction)
	{
		const bool readableDirection = direction == 0;
		const QStringList& names = readableDirection ? readable : writable;
		for (const QString& name : names)
		{
			const QJsonObject port = portJson(memory, name, readableDirection);
			ports->append(port);
			const QString client = port.value(QStringLiteral("client")).toString();
			int at = indexOfClient.value(client, -1);
			if (at < 0)
			{
				QJsonObject entry;
				entry.insert(QStringLiteral("name"), client);
				entry.insert(QStringLiteral("ports"), QJsonArray());
				entry.insert(QStringLiteral("port_count"), 0);
				clients.append(entry);
				at = clients.size() - 1;
				indexOfClient.insert(client, at);
			}
			QJsonObject entry = clients.at(at).toObject();
			QJsonArray held = entry.value(QStringLiteral("ports")).toArray();
			held.append(port);
			entry.insert(QStringLiteral("ports"), held);
			entry.insert(QStringLiteral("port_count"), held.size());
			clients.replace(at, entry);
		}
	}
	return clients;
}

void registerMidiReconnectStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.reconnect_status");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("reconnect_status");
	cmd.description = QStringLiteral("Read-only: what the engine remembers about MIDI "
		"controller assignments and what happened to them. A controller is remembered "
		"by IDENTITY - the ALSA-sequencer client's NAME and the port's NAME, "
		"\"<client name>:<port name>\" - and never by the address in front of them "
		"(\"<client>:<port>\"), because that number is handed out when a client opens "
		"the sequencer and is different after a replug. So a device that is unplugged "
		"and plugged back in is re-attached without user action, and this command is "
		"where that is visible: 'assignments' lists every remembered binding with its "
		"identity, the full name it currently holds, whether that identity is live, "
		"whether it was LOST (the device went away and has not been seen since) and how "
		"many times the engine has re-established it. 'reconnects' and 'lost' are the "
		"session's totals. 'notice' says whether the RUNNING client class publishes "
		"port-list changes at all - \"polled\" for the ALSA-sequencer client, whose "
		"one-second inventory poll is what a re-connection is driven by, and \"none\" "
		"for every client class this build does not consume changes from, in which case "
		"the memory still records the loss but nothing can re-attach automatically. "
		"'enabled' is the mode (midi.reconnect_arm) and 'persisted' is what the config "
		"file's midi/reconnect key holds.");
	// A13: no display, device or human is required - an offscreen instance reports
	// its (possibly empty) memory exactly like a visible one, which is why this
	// command is swept headlessly instead of being allowlisted, the
	// midi.retro_capture_status precedent.
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("persisted"), booleanProperty()},
		{QStringLiteral("notice"), enumProperty(noticeNames())},
		{QStringLiteral("assignments"), arrayProperty()},
		{QStringLiteral("assignment_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("live_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("lost_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("reconnects"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("lost"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		MidiClient* client = liveClient();
		MidiReconnect* memory = liveReconnect();
		if (client == nullptr || memory == nullptr) { return noClient(); }

		QJsonObject result;
		result.insert(QStringLiteral("client"),
			Engine::audioEngine() != nullptr ? Engine::audioEngine()->midiClientName()
				: QString());
		result.insert(QStringLiteral("enabled"), memory->isEnabled());
		result.insert(QStringLiteral("persisted"), midiReconnectPersistedEnabled());
		result.insert(QStringLiteral("notice"), noticeName(client));
		result.insert(QStringLiteral("assignments"), assignmentsJson(*memory));
		result.insert(QStringLiteral("assignment_count"),
			static_cast<int>(memory->assignments().size()));
		result.insert(QStringLiteral("live_count"), memory->liveCount());
		result.insert(QStringLiteral("lost_count"), memory->lostCount());
		result.insert(QStringLiteral("reconnects"), memory->reconnected());
		result.insert(QStringLiteral("lost"), memory->lost());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMidiClientsList(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.clients_list");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("clients_list");
	cmd.description = QStringLiteral("Read-only: every MIDI client and port the running "
		"client can see right now, gathered by the client's NAME, with each port's "
		"identity (the name without its address), its address, its direction, and "
		"whether an engine port holds a binding to it. This is the list a "
		"re-connection is resolved against: the device on a controller that vanished "
		"and came back appears under the SAME client name with a DIFFERENT address, and "
		"that is the pair of readings midi.reconnect_status reports the engine reacting "
		"to. 'engine_port' names the object that holds the binding (a trk-<n> id for a "
		"track's MIDI port), so a caller can see which track a device is bound to. With "
		"no MIDI backend open the client is the dummy one and the list is empty, which "
		"is reported as such rather than as an error.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("notice"), enumProperty(noticeNames())},
		{QStringLiteral("clients"), arrayProperty()},
		{QStringLiteral("client_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("ports"), arrayProperty()},
		{QStringLiteral("count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("readable_count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("writable_count"), integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		MidiClient* client = liveClient();
		MidiReconnect* memory = liveReconnect();
		if (client == nullptr || memory == nullptr) { return noClient(); }

		const QStringList readable = client->readablePorts();
		const QStringList writable = client->writablePorts();
		QJsonArray ports;
		const QJsonArray clients = clientsJson(*memory, readable, writable, &ports);

		QJsonObject result;
		result.insert(QStringLiteral("client"),
			Engine::audioEngine() != nullptr ? Engine::audioEngine()->midiClientName()
				: QString());
		result.insert(QStringLiteral("notice"), noticeName(client));
		result.insert(QStringLiteral("clients"), clients);
		result.insert(QStringLiteral("client_count"), clients.size());
		result.insert(QStringLiteral("ports"), ports);
		result.insert(QStringLiteral("count"), ports.size());
		result.insert(QStringLiteral("readable_count"), readable.size());
		result.insert(QStringLiteral("writable_count"), writable.size());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace


void registerMidiReconnectReadCommands(ControlRegistry& registry)
{
	registerMidiReconnectStatus(registry);
	registerMidiClientsList(registry);
}


} // namespace lmms
