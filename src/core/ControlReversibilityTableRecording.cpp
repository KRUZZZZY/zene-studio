/*
 * ControlReversibilityTableRecording.cpp - the SPEC A16 rows for the recording
 *                                          engine surface (0.3.0, feature rows
 *                                          14, 16 and 64).
 *
 * This file is data, like the other blocks of THE classification table (see
 * ControlReversibilityTable.cpp for the first block, the live-checkpoint rows).
 * It is a GROUP file on the seam the folder, vca, routing, scan-and-crash and
 * mastering files use: the class of each row comes from the row itself, not from
 * the file, and the rows land here rather than in the passive block because that
 * block is at 489 of the 500 lines the file-length ratchet allows.
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

#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kRecordingRows[] = {

	// =====================================================================
	// snapshot - the inverse is a bounded recorded state, and for these rows
	// that state is a COMMAND: control.undo dispatches the paired verb.
	// =====================================================================
	R("track.set_arm", RC::Snapshot, true,
		"arming a song track starts a capture on the record route its position maps "
		"to, and that capture writes a WAV and a take journal beside it. The TRACK is "
		"not written to: no attribute is added to the track's element, which is why "
		"the old refusal's objection (inventing a serialization field) no longer "
		"applies and why this row changed class from not_mutating",
		"action checkpoint: the recorded undo step is track.set_arm with armed: false, "
		"which stops the capture and retires the journal. DISARMING records no "
		"inverse - re-arming starts a NEW take rather than restoring the one that was "
		"stopped, and the registry records that honestly as 'this command recorded no "
		"inverse or snapshot'",
		"the take: delete it where the reply says it is (the default is "
		"zene-take-trk<N>.wav beside this instance's recovery file)"),
	// W1: the mixer's Arm All in one call.
	R("track.set_arm_many", RC::Snapshot, true,
		"the same capture as track.set_arm, for each named track: every armed route "
		"writes a WAV and a take journal beside it, and no track's element is "
		"written to",
		"action checkpoint: ONE recorded undo step - this command with armed: false "
		"over exactly the tracks the call armed (a track already armed, or one that "
		"refused, is not in it) - stops those captures and retires their journals. "
		"DISARMING records no inverse, as track.set_arm's does not: re-arming starts "
		"NEW takes",
		"the takes: delete them where the route replies say they are"),

	// =====================================================================
	// true_inverse - the inverse is a live checkpoint, reached as a command
	// because the thing written is the config file, not the project.
	// =====================================================================
	R("record.input_set", RC::TrueInverse, true,
		"writes the capture input path's four keys under the `audioinput` class of "
		"the config file (device, channel count, and the pair of captured channels "
		"the stereo bus carries). ConfigManager is not a JournallingObject, so there "
		"is no project checkpoint to restore - the settings.set precedent, and it "
		"records the same kind of inverse",
		"action checkpoint for the CONFIGURATION: the recorded undo step dispatches "
		"record.input_set with the previous plan, which writes those values back and "
		"saves the config file exactly as this command does",
		"the config file itself (audioinput/device, audioinput/channels, "
		"audioinput/left, audioinput/right): edit them back by hand if a client has "
		"lost the transaction record"),

	// =====================================================================
	// not_mutating - nothing is written that control.undo could reverse. The
	// read-only verbs, and the two writers whose write is a FILE the engine
	// cannot put back (the render.render and midi.retro_capture_arm shape).
	// =====================================================================
	R("record.get_state", RC::NotMutating, false,
		"reads every route's arm flag, input channel, take path and counters, plus "
		"the input path's own state; it writes nothing",
		"no write happens, so no transaction is recorded",
		""),
	R("record.disarm_track", RC::NotMutating, false,
		"stops one route's capture and retires its journal, and the take file stays "
		"on disk. What it changes is the recorder's live state, which is engine state "
		"that no project checkpoint holds - and the thing a caller might want back "
		"(the recording) is a file, not a state",
		"no state a checkpoint could restore is written: the journal it retires is a "
		"side file and re-arming writes a NEW take rather than restoring this one, so "
		"the record states that it recorded no inverse",
		"the take: it is on disk where the reply says, and re-arming the route writes "
		"a new one"),

	R("record.disarm_all", RC::NotMutating, false,
		"the same operation for every route at once: the captures stop and their "
		"journals are retired, and every take file stays where it is",
		"no state a checkpoint could restore is written; the record says so rather "
		"than claiming an inverse",
		"the take files: each reply names its route's path"),

	R("record.input_get_state", RC::NotMutating, false,
		"reads the configured plan, the published device state and the engine's two "
		"input stages; it writes nothing",
		"no write happens, so no transaction is recorded",
		""),

	R("transport.record", RC::NotMutating, false,
		"starts the transport in record mode: the run state is engine state, not "
		"project state (the transport.play precedent). What it RECORDS is written by "
		"the recording tracks and clips themselves, each through its own journalled path",
		"nothing to reverse: transport.stop is the operation a client calls",
		""),

	R("record.retro_capture_arm", RC::NotMutating, false,
		"arms or disarms the retrospective AUDIO window. Mode state, never project "
		"state: nothing about the song changes, and the window is not serialized with "
		"the project (the midi.retro_capture_arm precedent, verb for verb)",
		"no write happens to anything a checkpoint holds, so no transaction is "
		"recorded and control.undo is not blocked by it",
		"the mode itself: call this command with armed: false"),

	R("record.retro_capture_status", RC::NotMutating, false,
		"reads the retrospective window's armed flag, its capacity, and the frames it "
		"is holding, dropping and overwriting; it writes nothing",
		"no write happens, so no transaction is recorded",
		""),

	R("record.retro_capture_to_take", RC::NotMutating, false,
		"writes the retained window to a WAV file. The song is untouched - no clip, "
		"no track, no project state - which is the same classification render.render "
		"carries for the file it writes",
		"no project state is written, so there is nothing for a checkpoint to "
		"restore; the file is the deliverable, not a mutation of the session",
		"the take file: delete it where the reply says it is, or keep it - and 0.3.0 "
		"does not insert it into the session (docs/KNOWN-LIMITATIONS.md)"),

	// =====================================================================
	// irreversible - arming creates a take outside the project journal, and
	// disarming cannot remove its recorded bytes.
	// =====================================================================
	R("record.arm_track", RC::Irreversible, false,
		"arming opens a WAV outside the project journal and writes a take journal "
		"beside it. Disarming retires that journal but leaves every captured frame "
		"in the WAV, so the filesystem is not restored to its pre-arm state",
		"none: the engine cannot discard a take as an inverse; the transaction "
		"records no inverse",
		"explicitly call record.disarm_track to stop writing. The take remains at "
		"the file path in the arm reply; keep it or delete it manually"),

};

constexpr int kRecordingRowCount = static_cast<int>(sizeof(kRecordingRows) / sizeof(kRecordingRows[0]));

} // namespace

const ReversibilityRow* reversibilityRecordingRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kRecordingRowCount; }
	return kRecordingRows;
}

} // namespace control
} // namespace lmms
