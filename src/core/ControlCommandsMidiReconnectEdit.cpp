/*
 * ControlCommandsMidiReconnectEdit.cpp - the write half of the MIDI controller
 *                                        auto-reconnection group: the mode
 *                                        switch, and binding (or detaching)
 *                                        one engine MIDI port to one live
 *                                        controller port
 *                                        (0.3.0 feature-list row 18, OWNER-31
 *                                        item 7, SPEC A11-A16).
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
#include <QString>
#include <QStringList>

#include "ControlCommandsMidiReconnectShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "MidiClient.h"
#include "MidiPort.h"
#include "MidiReconnect.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

using namespace midireconnectcontrol;

//! The two directions a binding can have, in the order the argument list reads:
//! the input direction first, because a controller is an input device.
const QStringList& directions()
{
	static const QStringList names{QStringLiteral("read"), QStringLiteral("write")};
	return names;
}

void registerMidiReconnectArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.reconnect_arm");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("reconnect_arm");
	cmd.description = QStringLiteral("Arm or disarm automatic MIDI controller "
		"re-connection. With no 'enabled' argument the mode is ENABLED (the default); "
		"'enabled': false is its disarm. While enabled the engine re-establishes every "
		"remembered controller assignment whose identity comes back at a new address - "
		"the device that was unplugged and plugged in again, or whose controlling "
		"process exited and restarted. While disarmed the loss is still recorded and "
		"reported by midi.reconnect_status, but nothing is re-attached automatically; "
		"re-enabling takes effect at the next port-list poll (the ALSA-sequencer "
		"client's one-second inventory read). The mode is engine state, not project "
		"state: no transaction is recorded and control.undo has nothing to reverse. It "
		"is persisted to the config file's midi/reconnect key (not the project file), "
		"so it survives a restart, and 'persisted' reports what was stored.");
	// A13: no display, device or human is required - an offscreen instance arms the
	// mode exactly like a visible one, which is why this command is swept headlessly
	// instead of being allowlisted (the midi.retro_capture_arm and midi.learn_toggle
	// precedent: mode state, no `requires`).
	cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()}});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("changed"), booleanProperty()},
		{QStringLiteral("persisted"), booleanProperty()},
		{QStringLiteral("notice"), enumProperty(noticeNames())},
	});
	// SPEC A16: engine/mode state, exactly the midi.retro_capture_arm shape - the
	// flag is not project state, so the write is a config key (reported in
	// 'persisted') and not a journal step.
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		MidiClient* client = liveClient();
		MidiReconnect* memory = liveReconnect();
		if (client == nullptr || memory == nullptr) { return noClient(); }

		const bool wasEnabled = memory->isEnabled();
		// No argument means "enable": the deterministic reading of a command named
		// reconnect_ARM. A caller that wants the other direction says so.
		const bool wanted = args.contains(QStringLiteral("enabled"))
			? args.value(QStringLiteral("enabled")).toBool() : true;
		memory->setEnabled(wanted);
		// Written only when the mode actually moved, so re-arming an armed engine
		// never rewrites the user's config file.
		if (wanted != wasEnabled) { setMidiReconnectPersistedEnabled(wanted); }

		QJsonObject result;
		result.insert(QStringLiteral("client"),
			Engine::audioEngine() != nullptr ? Engine::audioEngine()->midiClientName()
				: QString());
		result.insert(QStringLiteral("enabled"), memory->isEnabled());
		result.insert(QStringLiteral("changed"), memory->isEnabled() != wasEnabled);
		result.insert(QStringLiteral("persisted"), midiReconnectPersistedEnabled());
		result.insert(QStringLiteral("notice"), noticeName(client));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! What a midi.reconnect_set call names: the engine port to write to, in which
//! direction, and the live controller port to bind it to.
struct BindingTarget
{
	Track* track = nullptr;
	MidiPort* port = nullptr;
	bool readable = true;
	QString name;
};

//! Resolve the three arguments, or fill \a error with a typed refusal.
/*!
 * 'name' must be a port the running client lists right now, and 'identity' must
 * match exactly one: re-attaching a controller to a port the engine is not sure
 * about is the mistake this whole feature exists to avoid. A track that is not
 * an instrument track carries no MIDI port, which is a typed Refused rather than
 * a silent no-op - the track.folder_set shape.
 */
bool resolveBindingTarget(const QJsonObject& args, BindingTarget* out, ControlResult* error)
{
	Track* track = resolveTrack(args.value(QStringLiteral("port")).toString(), error);
	if (track == nullptr) { return false; }
	InstrumentTrack* instrument = qobject_cast<InstrumentTrack*>(track);
	if (instrument == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("track '%1' has no MIDI port to bind: only an instrument "
				"track carries one").arg(trackIdOf(track)));
		return false;
	}
	MidiClient* client = liveClient();
	const bool readable = args.value(QStringLiteral("direction")).toString()
		!= QStringLiteral("write");
	const QString name = resolveTargetName(readable ? client->readablePorts()
		: client->writablePorts(), args, error);
	if (name.isEmpty()) { return false; }

	out->track = track;
	out->port = instrument->midiPort();
	out->readable = readable;
	out->name = name;
	return true;
}

//! The binding set \a port holds in one direction, before a write.
bool portHolds(const MidiPort* port, bool readable, const QString& name)
{
	const MidiPort::Map& held = readable ? port->readablePorts() : port->writablePorts();
	return held.value(name, false);
}

//! Subscribe (or unsubscribe) one binding through the ONE call that maintains
//! the set, so the live subscription and the map the project serializes as
//! inports/outports move together.
void applyBinding(MidiPort* port, bool readable, const QString& name, bool subscribe)
{
	if (readable) { port->subscribeReadablePort(name, subscribe); }
	else { port->subscribeWritablePort(name, subscribe); }
}

//! The recorded inverse of one midi.reconnect_set call.
/*!
 * The subscription set is a property of the MidiPort, and
 * MidiPort::subscribeReadablePort is the only call that maintains it - which is
 * why the recorded step goes through that same call rather than through a
 * restore of the pre-write XML: MidiPort::loadSettings only ever SUBSCRIBES the
 * ports a saved element names and never detaches one it does not, so a
 * checkpoint alone could not take a binding back off.
 */
void recordBindingInverse(MidiPort* port, bool readable, const QString& name, bool detach)
{
	control::addUndoStep(
		[port, readable, name, detach]() { applyBinding(port, readable, name, detach); },
		[port, readable, name, detach]() { applyBinding(port, readable, name, !detach); });
}

QJsonObject bindingJson(const BindingTarget& target, const MidiReconnectAssignment* binding,
	bool heldBefore, bool changed, bool detached)
{
	QJsonObject result;
	result.insert(QStringLiteral("port"), trackIdOf(target.track));
	result.insert(QStringLiteral("track"), trackIdOf(target.track));
	result.insert(QStringLiteral("direction"),
		target.readable ? QStringLiteral("read") : QStringLiteral("write"));
	result.insert(QStringLiteral("name"), target.name);
	result.insert(QStringLiteral("identity"), midiPortIdentity(target.name));
	result.insert(QStringLiteral("address"), midiPortAddress(target.name));
	result.insert(QStringLiteral("held_before"), heldBefore);
	result.insert(QStringLiteral("changed"), changed);
	result.insert(QStringLiteral("detached"), detached);
	result.insert(QStringLiteral("live"), binding != nullptr && binding->live);
	return result;
}

} // namespace


void registerMidiReconnectEditCommands(ControlRegistry& registry)
{
	registerMidiReconnectArm(registry);

	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.reconnect_set");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("reconnect_set");
	cmd.description = QStringLiteral("Bind the MIDI port of the track named by 'port' "
		"(a trk-<n> id) to a live MIDI controller port, named by 'name' (the exact full "
		"name midi.clients_list reports) or by 'identity' (the name half alone, "
		"\"<client name>:<port name>\", which is what survives the device being "
		"unplugged); 'direction' is \"read\" (the default, a controller feeding the "
		"track) or \"write\". 'detach': true removes the binding instead. This is the "
		"manual half of auto-reconnection: it establishes the binding the engine then "
		"remembers and re-establishes by itself, which midi.reconnect_status reports as "
		"an assignment. A binding is a property of the engine port and is serialized "
		"into the project as the <midiport> element's inports/outports attribute, so "
		"this command writes PROJECT state: one journal step is recorded and ONE "
		"control.undo puts the binding set back, through the same "
		"MidiPort::subscribeReadablePort call the write uses, so the live subscription "
		"and the serialized attribute are restored together. Refused typed when 'name' "
		"is not a port the running client lists, when 'identity' matches no port or "
		"more than one, when the track named by 'port' is not an instrument track, and "
		"when 'detach' names a binding the port does not hold.");
	// A13: no display, device or human is required. Bound, the write reaches the
	// client's own subscribe path; with no MIDI backend the client is the dummy one
	// and the port list is empty, which is a typed NotFound rather than a crash -
	// so the headless sweep exercises it.
	cmd.argsSchema = objectSchema({
		{QStringLiteral("port"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("identity"), stringProperty()},
		{QStringLiteral("direction"), enumProperty(directions())},
		{QStringLiteral("detach"), booleanProperty()},
	}, {QStringLiteral("port")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("port"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("direction"), enumProperty(directions())},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("identity"), stringProperty()},
		{QStringLiteral("address"), stringProperty()},
		{QStringLiteral("held_before"), booleanProperty()},
		{QStringLiteral("changed"), booleanProperty()},
		{QStringLiteral("detached"), booleanProperty()},
		{QStringLiteral("live"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		MidiClient* client = liveClient();
		MidiReconnect* memory = liveReconnect();
		if (client == nullptr || memory == nullptr) { return noClient(); }

		BindingTarget target;
		ControlResult error;
		if (!resolveBindingTarget(args, &target, &error)) { return error; }

		const bool detach = args.value(QStringLiteral("detach")).toBool();
		const bool heldBefore = portHolds(target.port, target.readable, target.name);
		if (detach && !heldBefore)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("'%1' is not bound to %2 in the %3 direction: there is "
					"nothing to detach").arg(target.name, trackIdOf(target.track),
						target.readable ? QStringLiteral("read") : QStringLiteral("write")));
		}
		recordBindingInverse(target.port, target.readable, target.name, !detach);
		applyBinding(target.port, target.readable, target.name, !detach);

		const MidiReconnectAssignment* binding =
			memory->assignmentOf(target.port, target.readable);
		const bool changed = detach ? heldBefore : !heldBefore;
		QJsonObject result = bindingJson(target, binding, heldBefore, changed, detach);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("port"), trackIdOf(target.track)},
					{QStringLiteral("direction"),
						target.readable ? QStringLiteral("read") : QStringLiteral("write")},
					{QStringLiteral("name"), target.name},
					{QStringLiteral("held_before"), heldBefore}},
				QStringLiteral("midi.reconnect_set"),
				QJsonObject{{QStringLiteral("port"), trackIdOf(target.track)},
					{QStringLiteral("direction"),
						target.readable ? QStringLiteral("read") : QStringLiteral("write")},
					{QStringLiteral("name"), target.name},
					{QStringLiteral("detach"), !detach}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step binds or detaches "
					"through MidiPort::subscribeReadablePort - the same call the write uses, "
					"and the call that maintains the inports/outports attribute the project "
					"serializes - one step for one command")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


} // namespace lmms
