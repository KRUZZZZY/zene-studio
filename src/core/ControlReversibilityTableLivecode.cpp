/*
 * ControlReversibilityTableLivecode.cpp - the A16 rows for the livecode.* group
 *                                          (board card #708)
 *
 * Two snapshot rows and one read. `livecode.schedule` and `livecode.unschedule`
 * are reversible because each handler writes its snapshot into __transaction
 * BEFORE the clock moves (the definition that was under the id, or that there
 * was none), and the recorded inverse is the other command with that
 * definition - one control.undo puts the clock back and livecode.get_state
 * reads it back. `livecode.get_state` reads the registry, the counters and the
 * transport observation; it writes nothing.
 *
 * The file is a GROUP file on the same seam as the safestart, recording,
 * routing, mastering and out-of-process files - the class comes from each row,
 * not from the file - and it is joined into reversibilityRowTable() by ONE
 * entry in ControlReversibilityTable.cpp. It is new rather than appended
 * because include/ControlReversibility.h sits at the file-length ratchet's
 * baseline and this lane's three rows would move it.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program (see COPYING); if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

const ReversibilityRow kLivecodeRows[] = {
	// =====================================================================
	// The livecode.* group (board card #708) - scheduled Lua evaluation:
	// the hooks the Lua engine did not have (bar, beat and transport
	// boundaries) and the surface that arms them. The engine half is
	// include/ScriptClock.h.
	// =====================================================================
	R("livecode.schedule", RC::Snapshot, true,
		"the handler snapshots the schedule that was under the id - or that none was - into "
		"__transaction BEFORE the clock changes; the recorded inverse is livecode.schedule with "
		"the previous definition (or livecode.unschedule for a brand-new id)",
		"control.undo replays the recorded inverse, which writes the previous source, hook and "
		"budget back through this same handler (livecode.schedule compile-checks and replaces "
		"in place), and livecode.get_state reads the definition back - both halves are visible "
		"on the surface",
		""),
	R("livecode.unschedule", RC::Snapshot, true,
		"the handler snapshots the removed definition into __transaction BEFORE the clock drops "
		"the id; the recorded inverse is livecode.schedule with that definition",
		"control.undo replays the recorded inverse, which rearms the id with the source, hook "
		"and budget it held (its fire counters start over: a snapshot restores the definition, "
		"not the history), and livecode.get_state reads the schedule back",
		""),
	R("livecode.get_state", RC::NotMutating, false,
		"reads the schedule registry, every schedule's fire/error/budget-exceeded counters, the "
		"aggregate counters and the transport observation the grid works from; it schedules "
		"nothing, fires nothing and touches no engine state",
		"no write. There is no state change to invert: the poll it reports is a control-thread "
		"read of the same Song accessors transport.get_state already reports, and a fire it "
		"reports happened happened whether or not this read was made",
		""),
};

constexpr int kLivecodeRowCount =
	static_cast<int>(sizeof(kLivecodeRows) / sizeof(kLivecodeRows[0]));

} // namespace

const ReversibilityRow* reversibilityLivecodeRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kLivecodeRowCount; }
	return kLivecodeRows;
}

} // namespace control
} // namespace lmms
