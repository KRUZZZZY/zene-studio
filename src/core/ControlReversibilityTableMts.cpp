/*
 * ControlReversibilityTableMts.cpp - the A16 rows for the mts.* group
 *                                     (board card #712)
 *
 * Five snapshot rows (the session-wide table is a PROCESS-GLOBAL with no
 * JournallingObject to checkpoint: the brief's own wording - a bounded
 * recorded snapshot with a STATED CAP - which is exactly what each mutating
 * handler records into __transaction before it flips a buffer: 128 finite
 * frequencies + the active flag + a source of <= 256 characters, inverted by
 * mts.set_tuning replaying that state), one true_inverse row (mts.master_set's
 * flag pair, an action checkpoint the way clock's is), and the not_mutating
 * read. The table itself is include/SessionTuning.h /
 * src/core/SessionTuning.cpp; the group is src/core/ControlCommandsMts.cpp.
 *
 * The registerMtsCommands declaration lives in include/ControlRegistryGroups.h
 * (the cap-split seam the nine-touch-point skill names for a group whose
 * header cannot grow ControlRegistry.h), and THIS row table's declaration
 * lives beside its join in src/core/ControlReversibilityTable.cpp - the board
 * #706 precedent: include/ControlReversibility.h sits over the file-length
 * ratchet and one new group does not move it.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
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

//! What the five table mutators share, named once. TWO helpers, not one:
//! R(...) takes six SEPARATE macro arguments and a nested macro that expands
//! to two comma-separated pieces counts as one argument at collection time
//! ("R requires 6 arguments, but only 5 given" - measured, this lane).
#define MTS_REASON(reason) \
	reason "; the stated cap is 128 finite frequencies + the active flag + a " \
		"source of at most 256 characters - the whole mutable state of the " \
		"process-global, so the snapshot needs nothing else"

#define MTS_MECHANISM() \
	"control.undo runs the recorded engine step (control::addUndoStep) that " \
		"replays that state through SessionTuning's one-buffer flip - which " \
		"retunes the sounding notes - while the transaction names " \
		"mts.set_tuning with the same args as the inverse command, and " \
		"mts.get_state reads the table back"

const ReversibilityRow kMtsRows[] = {
	// =====================================================================
	// The mts.* group (board card #712) - the session-wide tuning table.
	// The engine half is include/SessionTuning.h.
	// =====================================================================
	R("mts.get_state", RC::NotMutating, false,
		"reads the active flag, the 128 frequencies, the source and the Scale/"
		"Keymap descriptors plus the MTS-ESP publication status; it writes no "
		"buffer, flips nothing and touches no engine state",
		"no write. There is no state change to invert: the audio thread may be "
		"reading the same table while this runs, but a read of a committed "
		"buffer changes nothing a second read would see", ""),
	R("mts.load_scale", RC::Snapshot, true,
		MTS_REASON("the handler records the table as it was BEFORE the .scl "
			"parse commits a new, ACTIVE one"),
		MTS_MECHANISM(), ""),
	R("mts.load_keymap", RC::Snapshot, true,
		MTS_REASON("the handler records the table as it was BEFORE the .kbm "
			"rebuild (the active flag is preserved by the engine and carried "
			"in the snapshot regardless)"),
		MTS_MECHANISM(), ""),
	R("mts.set_tuning", RC::Snapshot, true,
		MTS_REASON("the handler records the table BEFORE it writes - this "
			"command is also its own kind of inverse: every mutator's recorded "
			"inverse IS mts.set_tuning with that before-state, and a call that "
			"is a replay still snapshots what it replaced"),
		MTS_MECHANISM(), ""),
	R("mts.set_note", RC::Snapshot, true,
		MTS_REASON("the handler records the whole table BEFORE the one-note "
			"write (one shape of inverse for every verb, not a single entry)"),
		MTS_MECHANISM(), ""),
	R("mts.reset", RC::Snapshot, true,
		MTS_REASON("the handler records the table BEFORE it puts back inactive "
			"12-TET content, so one control.undo re-activates the previous "
			"table and the previous sound"),
		MTS_MECHANISM(), ""),
	R("mts.master_set", RC::TrueInverse, true,
		"the master-armed flag is engine state with no JournallingObject, so "
		"the inverse is a recorded ACTION checkpoint: the handler pushes one "
		"addUndoStep pair BEFORE the flag pair (was/enabled) is the "
		"before-state it carries into __transaction; arming with no MTS-ESP "
		"library is a typed refusal that returns before any write",
		"control.undo runs the recorded step, which calls "
		"SessionTuning::setMtsMaster with the before-state's flag - "
		"deregistering the MTS-ESP master this call registered, or "
		"re-registering the one it took down - and mts.get_state reads the "
		"flag and the connected client count back",
		""),
};

constexpr int kMtsRowCount =
	static_cast<int>(sizeof(kMtsRows) / sizeof(kMtsRows[0]));

} // namespace

const ReversibilityRow* reversibilityMtsRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMtsRowCount; }
	return kMtsRows;
}

} // namespace control
} // namespace lmms
