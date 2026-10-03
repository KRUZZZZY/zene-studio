/*
 * ControlCommandsTrackMidiOut.cpp - an instrument track's notes sent to a MIDI port
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
#include <QJsonArray>

#include "AudioEngine.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiPort.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject outputState(InstrumentTrack* track)
{
	QJsonArray ports;
	const MidiPort::Map& writable = track->midiPort()->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		if (it.value()) { ports.append(it.key()); }
	}
	return {{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("enabled"), track->midiPort()->isWritable()},
		{QStringLiteral("ports"), ports}, {QStringLiteral("channel"), track->midiPort()->outputChannel()}};
}

void stopOutput(MidiPort* port)
{
	const MidiPort::Map writable = port->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		if (it.value()) { port->subscribeWritablePort(it.key(), false); }
	}
	port->setWritable(false);
}

//! MX-062: an instrument track's notes sent out to a MIDI port - a hardware synth, with its audio coming back on an
//! audio track that monitors the input (record.input_set, track.set_monitor).
ControlResult midiOut(const QJsonObject& args)
{
	ControlResult error;
	Track* found = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (found == nullptr) { return error; }
	auto* track = dynamic_cast<InstrumentTrack*>(found);
	if (track == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("MIDI goes out from an instrument track")); }
	if (args.size() == 1) { return ControlResult::success(outputState(track)); }  // only the track: a report
	const QString port = args.value(QStringLiteral("port")).toString();
	if (!port.isEmpty() && !Engine::audioEngine()->midiClient()->writablePorts().contains(port))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no MIDI output port '%1' (midi.device_list lists them)").arg(port));
	}
	track->addJournalCheckPoint();
	MidiPort* midi = track->midiPort();
	if (!args.value(QStringLiteral("enabled")).toBool(true)) { stopOutput(midi); }
	else
	{
		if (!port.isEmpty()) { midi->subscribeWritablePort(port, true); }
		if (args.contains(QStringLiteral("channel"))) { midi->setOutputChannel(args.value(QStringLiteral("channel")).toInt()); }
		midi->setWritable(true);
	}
	return ControlResult::success(outputState(track));
}

} // namespace

void registerTrackMidiOutCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("track.midi_out"), QStringLiteral("Send an instrument track's notes out to the MIDI output "
		"'port' (midi.device_list's writable ports) on 'channel' (1..16; 0 keeps each note's own) - an external synth, whose "
		"audio comes back on an audio track monitoring the input. 'enabled' false stops it and leaves every port. With only "
		"'track' it reports the track's output. Reversible through the ProjectJournal (Track checkpoint)."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("port"), stringProperty()},
			{QStringLiteral("channel"), integerProperty(0, 16)}, {QStringLiteral("enabled"), booleanProperty()}},
		{QStringLiteral("track")}, true, midiOut);
}

} // namespace lmms
