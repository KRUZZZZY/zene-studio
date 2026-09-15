/*
 * ControlCommandsMidiReconnectShared.h - what the two halves of the
 *                                       midi.reconnect_* / midi.clients_list
 *                                       command group share (SPEC A11-A16,
 *                                       0.3.0 feature-list row 18).
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

/* The group is split in two files because the group plus its schemas does not
 * fit the 500-line file ratchet, which is not moved for convenience:
 *
 *   ControlCommandsMidiReconnect.cpp      midi.reconnect_status, midi.clients_list
 *   ControlCommandsMidiReconnectEdit.cpp  midi.reconnect_arm, midi.reconnect_set
 *
 * Both halves report the same two things - the running client's re-connection
 * memory, and the live ports that memory is matched against - through the same
 * helpers, so the helpers are the group's and live here rather than in either
 * half. `inline` definitions in a named namespace: every translation unit that
 * includes this header sees one source for each helper (ControlVocabulary.cpp's
 * header records the duplicate-symbol link failure that ends the other way).
 */

#ifndef LMMS_CONTROL_COMMANDS_MIDI_RECONNECT_SHARED_H
#define LMMS_CONTROL_COMMANDS_MIDI_RECONNECT_SHARED_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "AudioEngine.h"
#include "ControlEdit.h"      // control::resolveTrack(), control::trackIdOf()
#include "ControlRegistry.h"  // ControlResult, ControlErrorKind
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiPort.h"
#include "MidiReconnect.h"
#include "Song.h"

namespace lmms
{
namespace midireconnectcontrol
{

//! The group's own tiny vocabulary: does the RUNNING client publish port-list
//! changes at all? It is the client class's answer (MidiClient::
//! noticesPortChanges), never a table this file carries, so a backend whose
//! changes this build does not consume cannot be reported as one that does.
inline const QStringList& noticeNames()
{
	static const QStringList names{QStringLiteral("polled"), QStringLiteral("none")};
	return names;
}

inline QString noticeName(const MidiClient* client)
{
	return MidiReconnect::clientNoticesPortChanges(client)
		? QStringLiteral("polled") : QStringLiteral("none");
}

//! The audio engine's MIDI client, or nullptr when there is no engine at all.
inline MidiClient* liveClient()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->midiClient() : nullptr;
}

//! The client's re-connection memory, or nullptr.
inline MidiReconnect* liveReconnect()
{
	MidiClient* client = liveClient();
	return client != nullptr ? &client->reconnect() : nullptr;
}

//! The line every refusal about a missing client shares.
inline ControlResult noClient()
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this instance has no MIDI client: the re-connection memory "
			"and the port list are the client's own (include/MidiClient.h)"));
}

//! The surface id of the object a MidiPort belongs to: the instrument track's
//! trk-<n> when the port is a track's, else the port's own display name.
/*!
 * MidiPort carries no back-pointer to its owner, so the owner is found the way
 * the rest of the surface finds it - by walking the song's tracks and comparing
 * the port - and the id reported is the same trk-<n> every other command takes.
 */
inline QString enginePortId(const MidiPort* port)
{
	Song* song = Engine::getSong();
	if (song != nullptr)
	{
		for (Track* track : song->tracks())
		{
			InstrumentTrack* instrument = qobject_cast<InstrumentTrack*>(track);
			if (instrument != nullptr && instrument->midiPort() == port)
			{
				return control::trackIdOf(track);
			}
		}
	}
	return port != nullptr ? port->displayName() : QString();
}

//! One remembered assignment, in the wire shape both halves report.
inline QJsonObject assignmentJson(const MidiReconnectAssignment& assignment)
{
	QJsonObject out;
	out.insert(QStringLiteral("port"), enginePortId(assignment.port));
	out.insert(QStringLiteral("direction"),
		assignment.readable ? QStringLiteral("read") : QStringLiteral("write"));
	out.insert(QStringLiteral("identity"), assignment.identity);
	out.insert(QStringLiteral("name"), assignment.name);
	out.insert(QStringLiteral("address"), midiPortAddress(assignment.name));
	out.insert(QStringLiteral("live"), assignment.live);
	out.insert(QStringLiteral("lost"), assignment.lost);
	out.insert(QStringLiteral("reconnects"), assignment.reconnects);
	out.insert(QStringLiteral("identity_matches"), assignment.matches);
	return out;
}

//! Every assignment of the running client, oldest first.
inline QJsonArray assignmentsJson(const MidiReconnect& memory)
{
	QJsonArray out;
	for (const MidiReconnectAssignment& assignment : memory.assignments())
	{
		out.append(assignmentJson(assignment));
	}
	return out;
}

//! Whether some assignment holds \a name in \a direction, and for which port.
inline QString boundPortOf(const MidiReconnect& memory, bool readable, const QString& name)
{
	for (const MidiReconnectAssignment& assignment : memory.assignments())
	{
		if (assignment.readable == readable && assignment.name == name
			&& assignment.port != nullptr)
		{
			return enginePortId(assignment.port);
		}
	}
	return QString();
}

//! The live port a `midi.reconnect_set` call names: the exact `name` when one
//! was given, else the live port whose `identity` the caller asked for.
/*!
 * By identity, a name that matches no port and a name that matches several are
 * both refusals, typed and distinct: re-attaching a controller to a port the
 * engine is not sure about is exactly the mistake this feature exists to avoid.
 */
inline QString resolveTargetName(const QStringList& livePorts, const QJsonObject& args,
	ControlResult* error)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	if (!name.isEmpty())
	{
		if (!livePorts.contains(name))
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("'%1' is not a port the running client lists: see "
					"midi.clients_list for the names that exist right now").arg(name));
			return QString();
		}
		return name;
	}

	const QString identity = args.value(QStringLiteral("identity")).toString();
	if (identity.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("give either 'name' (a full live port name, "
				"\"<client>:<port> <client name>:<port name>\") or 'identity' (the name "
				"half alone, which is what survives unplugging the device)"));
		return QString();
	}

	QString found;
	int matches = 0;
	for (const QString& candidate : livePorts)
	{
		if (midiPortIdentity(candidate) != identity) { continue; }
		++matches;
		if (found.isEmpty()) { found = candidate; }
	}
	if (matches != 1)
	{
		*error = ControlResult::failure(matches == 0 ? ControlErrorKind::NotFound
			: ControlErrorKind::Refused,
			matches == 0
				? QStringLiteral("no live port has the identity '%1' (see "
					"midi.clients_list)").arg(identity)
				: QStringLiteral("%1 live ports share the identity '%2': name the one "
					"you mean with 'name'").arg(matches).arg(identity));
		return QString();
	}
	return found;
}

} // namespace midireconnectcontrol
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_MIDI_RECONNECT_SHARED_H
