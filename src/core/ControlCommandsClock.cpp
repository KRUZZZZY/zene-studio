/*
 * ControlCommandsClock.cpp - the `clock.*` command group (SPEC A11-A16): MIDI
 *                              clock, the master and the slave, as commands.
 *
 * THE ITEM THIS CLOSES. MIDI clock / MTC was an unboarded Bar-2 gap with no
 * code (PLANNED-WORK-MASTER-LIST-2026-09-13.md, "MIDI clock / MTC - unboarded
 * gap, no code"; agent-surface inventory Group 11). D12 puts it in 0.3.0
 * because it is engine work with no architectural gate, and the four-part scope
 * contract wants three things besides the engine: an id per action with schemas
 * and an A16 row, a registered proof, and the UI-absence line in both docs. The
 * engine half is include/MidiClock.h and its three sources; this file is what
 * makes any of it reachable, and it is the ONLY way to reach it - there is no
 * interface for a MIDI clock in this release.
 *
 * A16, honestly, because this is the part that is easy to overclaim:
 *   clock.get_state   not_mutating  - it reads the live state and writes nothing
 *   clock.master_set  true_inverse  - an action checkpoint restores the enabled
 *                                     flag AND the port subscription
 *   clock.slave_set   snapshot      - see the file's own comment at that command
 *
 * MTC, stated: `clock.get_state` reports `mtc: "absent"` and this group has no
 * timecode command, because the engine has no frame rate or SMPTE offset to
 * generate one from. That absence is declared in docs/KNOWN-LIMITATIONS.md; a
 * command that pretended otherwise would be a claim, not a feature.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "MidiClock.h"
#include "Song.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The clock's live state, which is what every verb here answers with: a caller
//! that has just set a mode reads the same object back, so "what I asked for"
//! and "what the engine is doing" cannot be two different stories.
QJsonObject withTransaction(const QJsonObject& state, const QJsonObject& transaction)
{
	QJsonObject out = state;
	out.insert(QStringLiteral("__transaction"), transaction);
	return out;
}

QJsonObject masterState(const MidiClock& clock)
{
	return clock.stateJson().value(QStringLiteral("master")).toObject();
}

QJsonObject slaveState(const MidiClock& clock)
{
	return clock.stateJson().value(QStringLiteral("slave")).toObject();
}

ControlResult getState()
{
	return ControlResult::success(MidiClock::instance()->stateJson());
}

//! clock.master_set: enable or disable the master clock, optionally naming the
//! writable client port it subscribes to. Enabling without a client to send
//! through is a typed refusal - the engine does not pretend to emit.
ControlResult masterSet(const QJsonObject& args)
{
	const bool enabled = args.value(QStringLiteral("enabled")).toBool();
	const QString port = args.value(QStringLiteral("port")).toString();
	MidiClock* clock = MidiClock::instance();
	const bool wasEnabled = clock->masterEnabled();
	const QString wasPort = clock->masterPort();

	QString error;
	if (!clock->setMasterEnabled(enabled, port, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	// SPEC A16: the clock's mode is engine state and not a JournallingObject, so
	// there is no object checkpoint - but the inverse is a bounded pair (a flag
	// and a port name), so it becomes ONE recorded action step on the engine's
	// own undo stack, the same shape transport.seek uses.
	control::addUndoStep(
		[wasEnabled, wasPort]() { MidiClock::instance()->restoreMaster(wasEnabled, wasPort); },
		[enabled, port]() { MidiClock::instance()->restoreMaster(enabled, port); });

	QJsonObject before;
	before.insert(QStringLiteral("enabled"), wasEnabled);
	before.insert(QStringLiteral("port"), wasPort);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("enabled"), wasEnabled);
	inverseArgs.insert(QStringLiteral("port"), wasPort);
	QJsonObject transaction = transactionPayload(before, QStringLiteral("clock.master_set"),
		inverseArgs, true,
		QStringLiteral("action checkpoint: the recorded undo step calls MidiClock::restoreMaster "
			"with the enabled flag and the port name the before-state holds, and unsubscribes the "
			"port this call subscribed when the before-state named none, so both halves of the "
			"change come back"));
	return ControlResult::success(withTransaction(masterState(*clock), transaction));
}

//! clock.slave_set: enable or disable following, choose whether the measured
//! tempo is written to the Song, name the source port and set the drift bound.
ControlResult slaveSet(const QJsonObject& args)
{
	MidiClock* clock = MidiClock::instance();
	Song* song = Engine::getSong();
	const bool enabled = args.value(QStringLiteral("enabled")).toBool();
	const bool follow = args.value(QStringLiteral("follow_tempo")).toBool(
		clock->slaveFollowTempo());
	const QString source = args.value(QStringLiteral("source_port")).toString();
	const double bound = args.value(QStringLiteral("drift_bound_ms")).toDouble(
		clock->slaveDriftBoundMs());

	const bool wasEnabled = clock->slaveEnabled();
	const bool wasFollow = clock->slaveFollowTempo();
	const QString wasSource = clock->slaveSourcePort();
	const double wasBound = clock->slaveDriftBoundMs();

	clock->setSlaveDriftBoundMs(bound);
	if (!source.isEmpty()) { clock->setSlaveSourcePort(source); }
	clock->setSlaveEnabled(enabled);
	clock->setSlaveFollowTempo(enabled && follow);
	if (enabled && follow) { clock->startFollowTimer(); }
	else { clock->stopFollowTimer(); }

	const bool recordedEnabled = enabled;
	const bool recordedFollow = enabled && follow;
	const QString recordedSource = clock->slaveSourcePort();
	const double recordedBound = bound;
	control::addUndoStep(
		[wasEnabled, wasFollow, wasSource, wasBound]() {
			MidiClock* restore = MidiClock::instance();
			restore->setSlaveDriftBoundMs(wasBound);
			restore->setSlaveSourcePort(wasSource);
			restore->setSlaveEnabled(wasEnabled);
			restore->setSlaveFollowTempo(wasFollow);
			if (wasEnabled && wasFollow) { restore->startFollowTimer(); }
			else { restore->stopFollowTimer(); }
		},
		[recordedEnabled, recordedFollow, recordedSource, recordedBound]() {
			MidiClock* redo = MidiClock::instance();
			redo->setSlaveDriftBoundMs(recordedBound);
			redo->setSlaveSourcePort(recordedSource);
			redo->setSlaveEnabled(recordedEnabled);
			redo->setSlaveFollowTempo(recordedFollow);
			if (recordedEnabled && recordedFollow) { redo->startFollowTimer(); }
			else { redo->stopFollowTimer(); }
		});

	QJsonObject before;
	before.insert(QStringLiteral("enabled"), wasEnabled);
	before.insert(QStringLiteral("follow_tempo"), wasFollow);
	before.insert(QStringLiteral("source_port"), wasSource);
	before.insert(QStringLiteral("drift_bound_ms"), wasBound);
	// The tempo the slave may have overwritten while it was following. It is NOT
	// restored by the inverse and the transaction says so where a caller will
	// read it, because a follower's tempo writes are a trajectory (one
	// Song::setTempo per poll) and no bounded recorded state covers a trajectory.
	before.insert(QStringLiteral("tempo"), song != nullptr ? song->getTempo() : 0);

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("enabled"), wasEnabled);
	inverseArgs.insert(QStringLiteral("follow_tempo"), wasFollow);
	inverseArgs.insert(QStringLiteral("source_port"), wasSource);
	inverseArgs.insert(QStringLiteral("drift_bound_ms"), wasBound);
	QJsonObject transaction = transactionPayload(before, QStringLiteral("clock.slave_set"),
		inverseArgs, true,
		QStringLiteral("action checkpoint for the CONFIGURATION only: the recorded undo step "
			"restores the mode, the tempo-follow flag, the source port and the drift bound, and "
			"stops the follower's poll. The tempo a FOLLOWING slave wrote is not part of the "
			"inverse - it is a trajectory of Song::setTempo writes, not one state - and the value "
			"the before-state reports is what a caller restores it with (transport.set_tempo)"));
	return ControlResult::success(withTransaction(slaveState(*clock), transaction));
}

} // namespace

void registerClockCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("clock.get_state");
		cmd.group = QStringLiteral("clock");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("What this engine's MIDI clock is doing: the master's "
			"enabled flag, the port it sends on and whether that port is subscribed to a client "
			"destination, the message counters it has emitted and a bounded monitor of the last "
			"messages in order; and the slave's enabled flag, its source port, whether it "
			"follows the measured tempo, whether it is LOCKED, the measured tempo, the drift of "
			"the last interval from the window's mean, the drift bound, the tempo error the pulse "
			"jitter can produce, the window it measures over, the pulse count and the last song "
			"position and time-code values received. Read-only. `mtc` reports \"absent\": this "
			"release generates no MIDI time code (see docs/KNOWN-LIMITATIONS.md).");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("master"), objectProperty()},
			{QStringLiteral("slave"), objectProperty()},
			{QStringLiteral("ticks_per_pulse"), integerProperty(0, 64)},
			{QStringLiteral("ticks_per_beat"), integerProperty(0, 512)},
			{QStringLiteral("pulses_per_quarter"), integerProperty(0, 128)},
			{QStringLiteral("lock_timeout_ms"), integerProperty(0, 60000)},
			{QStringLiteral("mtc"), stringProperty()},
			{QStringLiteral("tempo"), integerProperty(MinTempo, MaxTempo)},
			{QStringLiteral("last_pulse_age_ms"), integerProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) { return getState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("clock.master_set");
		cmd.group = QStringLiteral("clock");
		cmd.verb = QStringLiteral("master_set");
		cmd.description = QStringLiteral("Enable or disable the DAW as a MIDI clock MASTER: 24 "
			"clock pulses to the quarter note, START/STOP/CONTINUE and a Song Position Pointer "
			"on the transport's own edges, emitted from the audio thread through this engine's "
			"MIDI output. `port` names the writable client destination to subscribe to (see "
			"midi.device_list for the names); an empty one keeps the current subscription. "
			"Enabling when the engine has no MIDI client to send through is refused, typed, "
			"rather than reported as a success that emits nothing. Reversible: the enabled flag "
			"AND the port subscription are restored by control.undo.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("port"), stringProperty()},
		}, {QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("port"), stringProperty()},
			{QStringLiteral("port_ready"), booleanProperty()},
			{QStringLiteral("emitted"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return masterSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("clock.slave_set");
		cmd.group = QStringLiteral("clock");
		cmd.verb = QStringLiteral("slave_set");
		cmd.description = QStringLiteral("Enable or disable the DAW as a MIDI clock SLAVE: "
			"follow an incoming clock, measure its tempo over one quarter note of pulses and, "
			"when follow_tempo is on, write that tempo to the Song once it has left the dead "
			"band. A slave that is enabled with NO clock arriving never locks, writes nothing and "
			"does not move the transport - clock.get_state reports locked:false. `source_port` "
			"names the readable client port to follow (informational: the engine's MIDI readers "
			"deliver a clock from wherever it arrives). `drift_bound_ms` is how far one pulse "
			"interval may stray from the window's mean and still count as locked. Reversible for "
			"the CONFIGURATION: the tempo a following slave wrote is a trajectory of writes, not "
			"one state, and control.undo does not restore it (the transaction names the value). "
			"Receiving START/STOP/CONTINUE/SONG POSITION is counted and reported; moving the "
			"transport FROM the incoming clock is not in this release (see "
			"docs/KNOWN-LIMITATIONS.md).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("follow_tempo"), booleanProperty()},
			{QStringLiteral("source_port"), stringProperty()},
			// In whole milliseconds: the engine's bound is a real number but the
			// schema subset has no bounded-number property, and a drift bound is
			// not a sub-millisecond quantity.
			{QStringLiteral("drift_bound_ms"),
				integerProperty(0, static_cast<int>(MidiClockTracker::MaxDriftBoundMs))},
		}, {QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("follow_tempo"), booleanProperty()},
			{QStringLiteral("locked"), booleanProperty()},
			{QStringLiteral("tempo_bpm"), numberProperty()},
			{QStringLiteral("drift_ms"), numberProperty()},
			{QStringLiteral("drift_bound_ms"), numberProperty()},
			{QStringLiteral("source_port"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return slaveSet(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
