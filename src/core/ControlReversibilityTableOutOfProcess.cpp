/*
 * ControlReversibilityTableOutOfProcess.cpp - the A16 rows of the out-of-process
 *                                            hosting surface (feature row 80,
 *                                            board card #670)
 *
 * Five ids. The two reads (`oop.get_state`, `oop.list_families`) are not_mutating
 * and report a MEASUREMENT - what this build can host, what the session's client
 * processes have done - neither of which is project state. The three writers are
 * irreversible, and each says why in its own words and names the fallback where
 * there is one:
 *
 *   oop.set_mode       no checkpoint: the mode belongs to a plugin INSTANCE that
 *                      the switch itself destroys and re-creates, so there is
 *                      nothing stored to put back. FALLBACK: the same command
 *                      with the recorded mode_before, which re-hosts the
 *                      instance the other way (the patch is carried over by the
 *                      plugin's own reload - plugins/ZynAddSubFx/ZynAddSubFx.cpp
 *                      reloadPlugin()).
 *   oop.restart        an EVENT: the client process that was there is gone and
 *                      its pid cannot be brought back. No fallback, and none is
 *                      needed - the state before the call is recorded and the
 *                      client was already dead, which is why the call was made.
 *   oop.reset_crashes  a MEASUREMENT of this session, not project state; a count
 *                      that was cleared cannot be un-cleared because the crashes
 *                      really happened. The exits and the last exit code stay in
 *                      the record, so nothing is hidden by the clear.
 *
 * The file is a GROUP file on the same seam as the safestart, recording, routing
 * and mastering files - the class comes from each row, not from the file - and it
 * is joined into reversibilityRowTable() by ONE entry in
 * ControlReversibilityTable.cpp. It is new rather than appended because that file
 * and include/ControlRegistry.h sit at the file-length ratchet's limit
 * (LANE-BRIEF-2026-09-15.md section 5).
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

const ReversibilityRow kOutOfProcessRows[] = {
	// =====================================================================
	// The oop.* group (feature row 80, board card #670) - which plugin
	// families this build can host in a client process, what each device's
	// hosting is right now, and this session's record of the client
	// processes. The engine half is include/OutOfProcessHosting.h.
	// =====================================================================
	R("oop.get_state", RC::NotMutating, false,
		"reads the hosting of every device in the song and this session's client records: the family "
		"table (what each family's out-of-process story IS in this build, with the reason where it "
		"has none), each device's resolved state, the live client pid, and the starts/exits/crashes "
		"counters of every client executable. It changes nothing - the walk touches EffectChain "
		"lists and the tracker's own atomics-free accessors",
		"no write. Neither the family table (static data) nor the client record (a session-scoped "
		"measurement of processes this instance started) is project state, so there is no song, no "
		"journal step and no file behind what it reports",
		""),
	R("oop.list_families", RC::NotMutating, false,
		"reads the build's out-of-process family table on its own: for every family, its "
		"availability (client-available / always-separate / no-client), the client executable when "
		"it has one, the one-sentence reason when it does not, and whether this session has refused "
		"that client executable after a crash loop",
		"no write. The table is compiled in and the refusal flag is the tracker's own session "
		"record; nothing is loaded, started or stored by the call",
		""),
	R("oop.set_mode", RC::Irreversible, false,
		"chooses in-process / separate-process for ONE device by re-instantiating it through the "
		"plugin's own setHostingMode (plugins/ZynAddSubFx/ZynAddSubFx.cpp): the running instance is "
		"destroyed and a new one is created in the other mode with the patch carried over, so the "
		"instance that was there - and anything a live checkpoint would hold for it - is gone. The "
		"mode itself is a BoolModel the plugin stores, which is why save and reload keep the choice",
		"none. The transaction records mode_before (and the hosting object before the call is the "
		"same value read through the plugin's own hostingState()), so a caller can see exactly what "
		"moved",
		"oop.set_mode with 'mode': \"<the recorded mode_before>\" re-hosts the instance the other "
		"way; the plugin's reload carries the patch over, so the audible state is preserved while "
		"the instance identity is not. The project file's own `separateprocess` attribute is the "
		"same value, so a saved project can also be reloaded to return to it"),
	R("oop.restart", RC::Irreversible, false,
		"re-hosts ONE device: the plugin's own reloadPlugin() destroys the current instance (and, in "
		"separate-process mode, the client process with it) and creates a fresh one, client included. "
		"The client that was running is a dead process - its pid cannot be brought back, so no "
		"checkpoint can describe the state before the call",
		"none. The result records state_before, process_id_before and the client record after, which "
		"is the whole of what a caller needs to see what happened",
		"none, and none is needed: the client this call replaces was already gone (that is why the "
		"call is made), and the alternative for a slot whose client keeps dying is the in-process "
		"mode, reachable with oop.set_mode - which the crash-loop refusal names rather than blocks"),
	R("oop.reset_crashes", RC::Irreversible, false,
		"clears this session's crash count for one client executable (or all of them), which is what "
		"lifts a crash-loop refusal. The count is a measurement of processes this instance started - "
		"not a value anything stores - and clearing it does not un-happen the crashes it counted",
		"none: there is no state to restore, because the crashes really happened and the exits and "
		"the last exit code stay in the record after the clear",
		"none. If the refusal was lifted too early the client executable will simply be counted "
		"again the next time it dies, and the refusal returns at the same bound; the record's exit "
		"and crash history is never erased by this call"),
};

constexpr int kOutOfProcessRowCount =
	static_cast<int>(sizeof(kOutOfProcessRows) / sizeof(kOutOfProcessRows[0]));

} // namespace

const ReversibilityRow* reversibilityOutOfProcessRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kOutOfProcessRowCount; }
	return kOutOfProcessRows;
}

} // namespace control
} // namespace lmms
