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
	R("record.arm_track", RC::Snapshot, true,
		"arming starts a real capture: a take file is opened and a take journal is "
		"written beside it, so the state this command changes is on disk rather than "
		"in the project - the project journal holds nothing for a side file in a take "
		"directory (record.journal_begin's own reason, and its own class)",
		"action checkpoint: the recorded undo step is the paired command "
		"record.disarm_track, which stops the capture, drains the disk-writer and "
		"RETIRES the journal. The take file itself stays on disk, exactly as a clean "
		"stop leaves it; a recording is not something the engine can un-happen",
		"the take file: delete it where it was written (the reply names the path), or "
		"keep it and use it - the recording is the point of the command"),
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
