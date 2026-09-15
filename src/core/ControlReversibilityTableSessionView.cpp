/*
 * ControlReversibilityTableSessionView.cpp - the SPEC A16 rows for the Session
 *                                            View's completion halves (board task
 *                                            #641, the #596 work): Follow Actions
 *                                            and Arrangement Record.
 *
 * This file is data, like the other blocks of THE classification table (see
 * ControlReversibilityTable.cpp for the join that assembles them). It is a GROUP
 * file on the seam the recording, folder, vca and routing files use, and it
 * exists because the two shared files the session.* rows lived in are at the
 * file-length ratchet: include/ControlRegistry.h is at 502 lines and
 * ControlReversibilityTablePassive.cpp is at its limit, so the six rows the
 * completion wave adds land here and are JOINED INTO the one table by a single
 * entry in ControlReversibilityTable.cpp's row-table list.
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

// lmmsconfig.h carries LMMS_HAVE_SESSION_VIEW. These six rows are guarded by the
// same switch that guards ControlRegistryRegistrations.cpp's two registration
// calls and ControlCommandsSession*.cpp's sources, so the table and the registry
// stay consistent in both directions - the rule
// ControlReversibilityTablePassive.cpp states for the launch rows.
#include "lmmsconfig.h"

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

#ifdef LMMS_HAVE_SESSION_VIEW
const ReversibilityRow kSessionViewRows[] = {
	// =====================================================================
	// Follow Actions (task #641, SPEC §4.1 / SPEC-zene-studio A3): the engine
	// evaluates the chain a cell carries while it plays.
	// =====================================================================
	R("session.follow_set", RC::NotMutating, false,
		"installs (or clears) one cell's plan in the audio thread's fixed plan table: "
		"requestFollowPlan queues one POD on the existing lock-free command queue, exactly "
		"as a launch request does, and edits no model. The PERSISTED chain is a ClipSlot "
		"attribute written by session.set_slot, whose own row carries the inverse",
		"nothing to reverse: the arm is engine state, and the cell is disarmed with "
		"session.follow_set enabled: false - the same distinction session.launch_slot "
		"draws between a launch and an edit",
		""),
	R("session.follow_get_state", RC::NotMutating, false,
		"reads the engine's published atomics (the armed-cell count and mask, the fire "
		"count, the newest fire) and the model's persisted chain; it writes nothing",
		"no write happens, so no transaction is recorded",
		""),

	// =====================================================================
	// Arrangement Record (task #641, SPEC §4.1): the performance's event ring,
	// and the one verb that writes it into the arrangement.
	// =====================================================================
	R("session.arrangement_record_arm", RC::NotMutating, false,
		"arms or disarms the recorder's tap. Mode state, never project state: the ring "
		"lives on the SessionScheduler and is deliberately not serialized, so nothing "
		"about the song changes and disarming keeps what is already recorded",
		"no write happens to anything a checkpoint holds, so no transaction is recorded "
		"and control.undo is not blocked by it",
		"the mode itself: call this command with armed: false"),
	R("session.arrangement_record_status", RC::NotMutating, false,
		"reads the tap's armed flag, its recorded and dropped counters and how many events "
		"are waiting to be landed; it writes nothing",
		"no write happens, so no transaction is recorded",
		""),
	R("session.back_to_arrangement", RC::NotMutating, false,
		"one atomic reset request on the SessionScheduler plus the recorder's disarm - the "
		"same engine operation session.stop_all makes. It edits no model, and the recorded "
		"performance is kept rather than dropped (the ring is deliberately not cleared by a "
		"reset), so nothing a checkpoint could restore is touched",
		"nothing to reverse: the session slots are relaunched from the model, which this "
		"did not touch, and the performance it ended is landed by "
		"session.arrangement_record_land",
		""),
	R("session.arrangement_record_land", RC::TrueInverse, true,
		"creates one arrangement clip per completed launch/stop pair on that column's song "
		"track, at the tick the transition fired on. The clip list is part of the Track's "
		"serialized state, which is why the inverse is the Track's own journal checkpoint "
		"- the clip.add and midi.retro_capture_to_clip shape - taken once per touched "
		"track before its first clip is created, plus (for a FOLLOW-ACTION pass) the two "
		"clips of every SwitchScene pair",
		"ProjectJournal (one Track checkpoint per touched track; Track::restoreState "
		"re-loads the track's serialized clips, which is how the GUI's own clip "
		"add/delete paths reverse themselves). The recorded inverse names the FIRST clip "
		"of the pass - clip.delete removes that one exactly - because a pass creates one "
		"clip per completed launch and a transaction carries one inverse; control.undo "
		"restores every touched track, which removes them all",
		"the clips: each one is named in the reply as clips are created "
		"(clip.delete on any of them removes it), and the pass is refused outright while "
		"a recorded launch is still open, so no clip is ever created for a span the "
		"engine did not observe"),

};

constexpr int kSessionViewRowCount =
	static_cast<int>(sizeof(kSessionViewRows) / sizeof(kSessionViewRows[0]));
#else
// No Session View: an EMPTY table, but a valid pointer - the join in
// ControlReversibilityTable.cpp walks [rows, rows + count).
const ReversibilityRow kSessionViewRows[1] = {};
constexpr int kSessionViewRowCount = 0;
#endif // LMMS_HAVE_SESSION_VIEW

} // namespace

const ReversibilityRow* reversibilitySessionViewRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kSessionViewRowCount; }
	return kSessionViewRows;
}

} // namespace control
} // namespace lmms
