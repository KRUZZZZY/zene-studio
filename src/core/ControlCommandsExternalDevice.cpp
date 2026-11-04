/*
 * ControlCommandsExternalDevice.cpp - the External MIDI device of an instrument track (Q050-252.1)
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

/*! Stage 1 of the External Instrument device (MX-062, R-recording-44): ONE instrument track drives ONE piece
 *  of hardware, and this stage owns the MIDI half of it - the output port and channel, the bank and program
 *  the gear is told to select, and named presets of those settings. The settings are the track's own MidiPort
 *  models (MidiPort.h), so they are saved with the track, and every change is one Track journal checkpoint:
 *  one control.undo puts the whole device back, presets included. The audio return and the latency offset are
 *  later stages; nothing here has to be undone for them.
 *
 *  Presets live in the project (inside the track's <midiport>), not in a user-wide store: a preset is the
 *  settings of THIS device on THIS track, and keeping it in the track is what lets the one Track checkpoint
 *  cover it. */

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiPort.h"

namespace lmms
{

using namespace control;

namespace
{

QString selectedPort(const MidiPort* midi)
{
	const MidiPort::Map& writable = midi->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		if (it.value()) { return it.key(); }
	}
	return {};
}

QJsonObject presetJson(const MidiPort::DevicePreset& preset)
{
	return {{QStringLiteral("name"), preset.name}, {QStringLiteral("port"), preset.port}, {QStringLiteral("channel"), preset.channel},
		{QStringLiteral("bank_msb"), preset.bankMsb}, {QStringLiteral("bank_lsb"), preset.bankLsb}, {QStringLiteral("program"), preset.program}};
}

QJsonObject deviceState(InstrumentTrack* track)
{
	MidiPort* midi = track->midiPort();
	QJsonArray ports;
	QJsonArray available;
	const MidiPort::Map& writable = midi->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		available.append(it.key());
		if (it.value()) { ports.append(it.key()); }
	}
	QJsonArray presets;
	for (const MidiPort::DevicePreset& preset : midi->devicePresets()) { presets.append(presetJson(preset)); }
	return {{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("enabled"), midi->isWritable()},
		{QStringLiteral("port"), selectedPort(midi)}, {QStringLiteral("ports"), ports}, {QStringLiteral("available_ports"), available}, {QStringLiteral("channel"), midi->outputChannel()},
		{QStringLiteral("bank_msb"), midi->outputBankMsb()}, {QStringLiteral("bank_lsb"), midi->outputBankLsb()},
		{QStringLiteral("program"), midi->outputProgram()}, {QStringLiteral("presets"), presets}};
}

InstrumentTrack* resolveInstrument(const QJsonObject& args, ControlResult* error)
{
	Track* found = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (found == nullptr) { return nullptr; }
	auto* track = dynamic_cast<InstrumentTrack*>(found);
	if (track == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("an External MIDI device is on an instrument track"));
	}
	return track;
}

//! The MIDI output ports the track's own client offers.
bool portExists(MidiPort* midi, const QString& port)
{
	return midi->midiClient()->writablePorts().contains(port);
}

ControlResult noSuchPort(const QString& port)
{
	return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no MIDI output port '%1' (midi.device_list lists them)").arg(port));
}

//! The device sends to exactly one port: subscribe @a port, leave every other.
void selectPort(MidiPort* midi, const QString& port)
{
	const MidiPort::Map writable = midi->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		if (it.value() && it.key() != port) { midi->subscribeWritablePort(it.key(), false); }
	}
	midi->subscribeWritablePort(port, true);
	midi->setWritable(true);
}

void stopOutput(MidiPort* midi)
{
	const MidiPort::Map writable = midi->writablePorts();
	for (auto it = writable.constBegin(); it != writable.constEnd(); ++it)
	{
		if (it.value()) { midi->subscribeWritablePort(it.key(), false); }
	}
	midi->setWritable(false);
}

//! One optional whole-number argument.
struct IntArg
{
	bool present = false;
	int value = 0;
};

//! @a key of @a args in @a low..@a high into @a out (left absent when the key is not there); false with @a error when it is not a number in range.
bool readInt(const QJsonObject& args, const char* key, int low, int high, IntArg* out, ControlResult* error)
{
	const QString name = QString::fromLatin1(key);
	if (!args.contains(name)) { return true; }
	const QJsonValue value = args.value(name);
	const int number = value.toInt(low - 1);
	if (!value.isDouble() || number < low || number > high)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is a whole number from %2 to %3").arg(name).arg(low).arg(high));
		return false;
	}
	*out = {true, number};
	return true;
}

//! What a device.external_set call asks for.
struct SetRequest
{
	IntArg channel;
	IntArg bankMsb;
	IntArg bankLsb;
	IntArg program;
	bool hasPort = false;
	bool hasEnabled = false;
	bool enabled = true;
	QString port;

	bool patchAsked() const { return bankMsb.present || bankLsb.present || program.present; }
};

//! Reads the four numbers of @a args into @a req.
bool readNumbers(const QJsonObject& args, SetRequest* req, ControlResult* error)
{
	return readInt(args, "channel", 0, 16, &req->channel, error) && readInt(args, "bank_msb", -1, 127, &req->bankMsb, error)
		&& readInt(args, "bank_lsb", -1, 127, &req->bankLsb, error) && readInt(args, "program", 1, 128, &req->program, error);
}

//! The request in @a args, checked against its ranges and shape; false with @a error when it is not a usable one.
bool parseSetRequest(const QJsonObject& args, SetRequest* req, ControlResult* error)
{
	if (!readNumbers(args, req, error)) { return false; }
	req->hasPort = args.contains(QStringLiteral("port"));
	req->hasEnabled = args.contains(QStringLiteral("enabled"));
	req->enabled = args.value(QStringLiteral("enabled")).toBool(true);
	req->port = args.value(QStringLiteral("port")).toString();
	const bool nothing = !req->hasPort && !req->hasEnabled && !req->channel.present && !req->patchAsked();
	if (nothing || (req->hasPort && req->port.isEmpty()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, nothing
			? QStringLiteral("name something to set: port, channel, bank_msb, bank_lsb, program or enabled (device.external_get_state reads it)")
			: QStringLiteral("'port' names a MIDI output port; 'enabled' false stops the output"));
		return false;
	}
	return true;
}

//! The output half of @a req: the port, the output on or off, the channel.
void applyOutput(MidiPort* midi, const SetRequest& req)
{
	if (req.hasEnabled && !req.enabled)
	{
		stopOutput(midi);
		return;
	}
	if (req.hasPort) { selectPort(midi, req.port); }
	else if (req.hasEnabled) { midi->setWritable(true); }
	if (req.channel.present) { midi->setOutputChannel(req.channel.value); }
}

//! The patch half of @a req, the values it leaves out taken from the port; whether the output was handed it.
bool applyPatch(MidiPort* midi, const SetRequest& req)
{
	if (!req.patchAsked()) { return false; }
	const auto pick = [](const IntArg& asked, int current) { return asked.present ? asked.value : current; };
	return midi->setOutputPatch(pick(req.bankMsb, midi->outputBankMsb()), pick(req.bankLsb, midi->outputBankLsb()), pick(req.program, midi->outputProgram()));
}

ControlResult getState(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrument(args, &error);
	if (track == nullptr) { return error; }
	return ControlResult::success(deviceState(track));
}

ControlResult setDevice(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrument(args, &error);
	if (track == nullptr) { return error; }
	MidiPort* midi = track->midiPort();
	SetRequest req;
	if (!parseSetRequest(args, &req, &error)) { return error; }
	if (req.hasPort && !portExists(midi, req.port)) { return noSuchPort(req.port); }

	track->addJournalCheckPoint();
	applyOutput(midi, req);
	const bool sent = applyPatch(midi, req);
	QJsonObject state = deviceState(track);
	state.insert(QStringLiteral("patch_sent"), sent && midi->isOutputEnabled());
	return ControlResult::success(state);
}

//! A preset name from @a args: trimmed, 1..MaxDevicePresetName characters.
bool presetName(const QJsonObject& args, QString* name, ControlResult* error)
{
	*name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name->isEmpty() || name->size() > MidiPort::MaxDevicePresetName)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'name' is 1 to %1 characters").arg(MidiPort::MaxDevicePresetName));
		return false;
	}
	return true;
}

ControlResult presetSave(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrument(args, &error);
	QString name;
	if (track == nullptr || !presetName(args, &name, &error)) { return error; }
	MidiPort* midi = track->midiPort();
	MidiPort::DevicePreset preset;
	preset.name = name;
	preset.port = selectedPort(midi);
	preset.channel = midi->outputChannel();
	preset.bankMsb = midi->outputBankMsb();
	preset.bankLsb = midi->outputBankLsb();
	preset.program = midi->outputProgram();
	const bool replaces = midi->findDevicePreset(name) != nullptr;
	if (!replaces && midi->devicePresets().size() >= MidiPort::MaxDevicePresets)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a device keeps at most %1 presets").arg(MidiPort::MaxDevicePresets));
	}
	track->addJournalCheckPoint();
	midi->setDevicePreset(preset);
	QJsonObject state = deviceState(track);
	state.insert(QStringLiteral("saved"), presetJson(preset));
	state.insert(QStringLiteral("replaced"), replaces);
	return ControlResult::success(state);
}

ControlResult presetRecall(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrument(args, &error);
	QString name;
	if (track == nullptr || !presetName(args, &name, &error)) { return error; }
	MidiPort* midi = track->midiPort();
	const MidiPort::DevicePreset* found = midi->findDevicePreset(name);
	if (found == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no device preset '%1' (device.external_get_state lists them)").arg(name));
	}
	const MidiPort::DevicePreset preset = *found;  // setting the port must not move what we read from
	const bool portKnown = preset.port.isEmpty() || portExists(midi, preset.port);

	track->addJournalCheckPoint();
	if (preset.port.isEmpty()) { stopOutput(midi); }
	else if (portKnown) { selectPort(midi, preset.port); }
	midi->setOutputChannel(preset.channel);
	// forced: recalling a preset tells the gear its patch, also when the values were already what the preset holds
	const bool sent = midi->setOutputPatch(preset.bankMsb, preset.bankLsb, preset.program, true);
	QJsonObject state = deviceState(track);
	state.insert(QStringLiteral("recalled"), presetJson(preset));
	state.insert(QStringLiteral("patch_sent"), sent && midi->isOutputEnabled());
	// A preset's port can be a device that is not plugged in now: the rest is applied and this says what was not
	state.insert(QStringLiteral("port_applied"), portKnown);
	if (!portKnown) { state.insert(QStringLiteral("missing_port"), preset.port); }
	return ControlResult::success(state);
}

ControlResult presetRemove(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = resolveInstrument(args, &error);
	QString name;
	if (track == nullptr || !presetName(args, &name, &error)) { return error; }
	MidiPort* midi = track->midiPort();
	if (midi->findDevicePreset(name) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no device preset '%1' (device.external_get_state lists them)").arg(name));
	}
	track->addJournalCheckPoint();
	midi->removeDevicePreset(name);
	QJsonObject state = deviceState(track);
	state.insert(QStringLiteral("removed"), name);
	return ControlResult::success(state);
}

} // namespace

void registerExternalDeviceCommands(ControlRegistry& registry)
{
	const QJsonObject onlyTrack{{QStringLiteral("track"), stringProperty()}};
	const QJsonObject named{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("name"), stringProperty()}};
	defineCommand(registry, QStringLiteral("device.external_get_state"), QStringLiteral("Read an instrument track's External MIDI device: whether "
		"its output is on, the MIDI output 'port' it sends to (and 'available_ports', the ones it could), the 'channel' (0 keeps each note's own, 1..16), the bank select 'bank_msb' / "
		"'bank_lsb' (CC 0 / CC 32; -1 sends none) and the 'program' (1..128, as the PROG box counts; sent as 0..127), and the saved device "
		"'presets'. Read-only."),
		onlyTrack, {QStringLiteral("track")}, false, getState);
	defineCommand(registry, QStringLiteral("device.external_set"), QStringLiteral("Configure an instrument track's External MIDI device. "
		"'port' (midi.device_list's writable ports) is the ONE output the device sends to - any other it was sending to is left - and turns the "
		"output on; 'enabled' false stops it; 'channel' is 0..16 (0 keeps each note's own); 'bank_msb' / 'bank_lsb' (-1: none) and 'program' "
		"(1..128) are the patch the gear is told to select: when any of them changes the output gets bank select CC 0 / CC 32, then the program "
		"change, once ('patch_sent' says it reached an enabled output). Changing only the port or channel sends no patch. Name at least one "
		"setting. One control.undo puts every setting back (Track checkpoint) and tells the gear the patch that returns."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("port"), stringProperty()}, {QStringLiteral("channel"), integerProperty(0, 16)},
			{QStringLiteral("bank_msb"), integerProperty(-1, 127)}, {QStringLiteral("bank_lsb"), integerProperty(-1, 127)},
			{QStringLiteral("program"), integerProperty(1, 128)}, {QStringLiteral("enabled"), booleanProperty()}},
		{QStringLiteral("track")}, true, setDevice);
	defineCommand(registry, QStringLiteral("device.external_preset_save"), QStringLiteral("Save the device's current port, channel, bank and "
		"program as the preset 'name' (1..64 characters; a saved name is replaced). A device keeps up to 64, saved with the project. "
		"Reversible through the ProjectJournal (Track checkpoint)."),
		named, {QStringLiteral("track"), QStringLiteral("name")}, true, presetSave);
	defineCommand(registry, QStringLiteral("device.external_preset_recall"), QStringLiteral("Recall the device preset 'name': its port (when "
		"the port is present now - 'port_applied' and 'missing_port' say otherwise), channel, bank and program, and send the patch to the "
		"output ('patch_sent'), also when the settings were already those. One control.undo puts the previous settings back."),
		named, {QStringLiteral("track"), QStringLiteral("name")}, true, presetRecall);
	defineCommand(registry, QStringLiteral("device.external_preset_remove"), QStringLiteral("Delete the device preset 'name'. Reversible "
		"through the ProjectJournal (Track checkpoint)."),
		named, {QStringLiteral("track"), QStringLiteral("name")}, true, presetRemove);
}

} // namespace lmms
