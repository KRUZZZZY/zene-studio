/*
 * ControlCommandsMidiTakes.cpp - track.set_midi_arm, record.midi_capture_status and
 *                                record.midi_capture_commit
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
/*! Q050-054.1: MIDI takes recorded on armed instrument tracks by transport.record (MidiTakeRecorder.h).
 *  - track.set_midi_arm: arm or disarm an instrument track's MIDI take capture - mode state, like the
 *    retrospective capture's arm, not project state;
 *  - record.midi_capture_status: what is armed, what the open session holds, and the last commit;
 *  - record.midi_capture_commit: the captured passes become arrangement clips (loop passes: take lanes of
 *    one take group, the newest active unless a take was chosen) - ONE undo step (a checkpoint of every
 *    track it changes). Stop runs it; it is callable for a capture a refusal left pending. */

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiTakeRecorder.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult setMidiArm(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	auto* instrument = dynamic_cast<InstrumentTrack*>(track);
	if (instrument == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not an instrument track: MIDI takes are recorded on instrument tracks (an audio "
				"track records through clip.set_record or track.set_arm)").arg(trackIdOf(track)));
	}
	const bool armed = args.value(QStringLiteral("armed")).toBool();
	MidiTakeCapture& capture = instrument->midiTakeCapture();
	const bool changed = capture.isArmed() != armed;
	capture.arm(armed);
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("armed"), capture.isArmed()},
		{QStringLiteral("changed"), changed}, {QStringLiteral("capturing"), capture.isCapturing()}});
}

ControlResult captureStatus(const QJsonObject&)
{
	return ControlResult::success(MidiTakeRecorder::instance()->status());
}

ControlResult captureCommit(const QJsonObject&)
{
	MidiTakeRecorder& recorder = *MidiTakeRecorder::instance();
	if (recorder.isRecording())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the transport is still recording; transport.stop ends the pass and commits its MIDI takes"));
	}
	const std::vector<InstrumentTrack*> tracks = recorder.pendingTracks();
	if (tracks.empty())
	{
		// Refused BEFORE a checkpoint: an undo step for a write that never happens would shadow the real edit.
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no captured MIDI is waiting: arm an instrument track (track.set_midi_arm), record a pass "
				"with transport.record and stop - Stop commits the takes"));
	}
	QVector<JournallingObject*> objects;
	QJsonArray ids;
	for (InstrumentTrack* track : tracks)
	{
		objects.append(track);
		ids.append(trackIdOf(track));
	}
	Engine::projectJournal()->addJournalCheckPoint(objects);
	QJsonObject result = recorder.commitPending();
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("tracks"), ids}},
		QStringLiteral("control.undo"), {}, true,
		QStringLiteral("ProjectJournal: one checkpoint of every track the commit changed - its clips, its notes and "
			"its take lanes - so one undo removes the recorded takes and one redo brings them back")));
	return ControlResult::success(result);
}

QJsonObject takeArraySchema()
{
	QJsonObject takes = arrayProperty();
	takes.insert(QStringLiteral("items"), objectSchema({
		{QStringLiteral("track"), stringProperty()}, {QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("position"), integerProperty()}, {QStringLiteral("length"), integerProperty()},
		{QStringLiteral("pass"), integerProperty()}, {QStringLiteral("notes"), integerProperty()},
		{QStringLiteral("lane"), integerProperty()}, {QStringLiteral("group"), integerProperty()},
		{QStringLiteral("active"), booleanProperty()}}));
	return takes;
}

QJsonObject takeSchema()
{
	return objectSchema({{QStringLiteral("loop_recording"), booleanProperty()}, {QStringLiteral("takes"), takeArraySchema()},
		{QStringLiteral("notes"), integerProperty()}, {QStringLiteral("dropped_events"), integerProperty()}});
}

QJsonObject armedTrackArraySchema()
{
	QJsonObject tracks = arrayProperty();
	tracks.insert(QStringLiteral("items"), objectSchema({
		{QStringLiteral("track"), stringProperty()}, {QStringLiteral("name"), stringProperty()},
		{QStringLiteral("capturing"), booleanProperty()}, {QStringLiteral("events"), integerProperty()},
		{QStringLiteral("dropped_events"), integerProperty()}}));
	return tracks;
}

} // namespace

void registerMidiTakeCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_midi_arm");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_midi_arm");
	cmd.description = QStringLiteral("Arm (armed: true) or disarm an instrument track for MIDI take recording: while "
		"transport.record runs, the MIDI played into an armed track - from a controller, the on-screen keyboard or "
		"any input routed to it - is captured, and Stop leaves it in the arrangement (record.midi_capture_commit): "
		"one MIDI clip for a plain pass, a take clip per pass on its own take lane for a loop recording, the newest "
		"pass active unless a take was chosen (comp.take_select). This is not track.set_arm, which records the "
		"audio input to a WAV file. Mode state, not saved with the project and not an undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("armed"), booleanProperty()}},
		{QStringLiteral("track"), QStringLiteral("armed")});
	cmd.resultSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("changed"), booleanProperty()}, {QStringLiteral("capturing"), booleanProperty()}});
	cmd.mutating = false;
	cmd.handler = setMidiArm;
	registry.registerCommand(cmd);

	cmd = ControlCommand();
	cmd.id = QStringLiteral("record.midi_capture_status");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("midi_capture_status");
	cmd.description = QStringLiteral("The MIDI take recording state: 'armed_tracks' (each armed instrument track, "
		"whether it is capturing now, the events captured this pass and the events dropped because the bounded "
		"capture queue was full), 'recording' (a transport.record pass is capturing), 'pending' (a stopped pass "
		"whose takes were not committed), 'loop_recording', 'capacity_events' (the queue bound per track) and "
		"'last_commit' (record.midi_capture_commit's last report: each take's track, clip, position, length, pass, "
		"notes, lane and take group). Reads only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("recording"), booleanProperty()},
		{QStringLiteral("pending"), booleanProperty()}, {QStringLiteral("loop_recording"), booleanProperty()},
		{QStringLiteral("capacity_events"), integerProperty()}, {QStringLiteral("armed_tracks"), armedTrackArraySchema()},
		{QStringLiteral("session_tracks"), arrayProperty()}, {QStringLiteral("last_commit"), takeSchema()}});
	cmd.mutating = false;
	cmd.handler = captureStatus;
	registry.registerCommand(cmd);

	cmd = ControlCommand();
	cmd.id = QStringLiteral("record.midi_capture_commit");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("midi_capture_commit");
	cmd.description = QStringLiteral("Leave the MIDI captured by the last transport.record pass in the arrangement. "
		"Stop runs this by itself - the header's Stop, the space bar or transport.stop - so a client calls it only "
		"for a capture a refusal left pending (record.midi_capture_status 'pending'). A plain pass becomes one MIDI "
		"clip on its instrument track from the bar of its first note; a loop recording (the transport wrapped at "
		"least once) makes every pass that holds notes a take clip over the loop region, each on a new take lane "
		"of one take group, and the newest pass is the active take unless a take of that group was chosen "
		"(comp.take_select): a later loop recording over the same region joins the group and keeps that choice. "
		"In Replace record mode a plain pass erases the earlier notes it overlaps (notes only, never controller "
		"lanes or automation). Notes keep the velocity they sounded with. One undo step: control.undo removes the "
		"takes, control.redo restores them. Refused while recording and when nothing is pending.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = takeSchema();
	cmd.mutating = true;
	cmd.handler = captureCommit;
	registry.registerCommand(cmd);
}

} // namespace lmms
