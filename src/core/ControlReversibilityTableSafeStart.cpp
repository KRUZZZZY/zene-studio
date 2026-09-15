/*
 * ControlReversibilityTableSafeStart.cpp - the safestart.* group's rows of the
 * SPEC A16 classification table.
 *
 * This file is data. It holds ONE GROUP's rows whatever their class, and the
 * class comes from each row rather than from the file - the same arrangement
 * ControlReversibilityTableScanAndCrash.cpp uses for the crash-reporter and
 * scan-cache groups, and for the same reason: the live block
 * (ControlReversibilityTable.cpp) and the passive block
 * (ControlReversibilityTablePassive.cpp) are both at the 500-line file-length
 * ratchet, so a group's rows land together in their own translation unit and
 * reversibilityRowTable() joins them into the one block every consumer reads
 * (src/core/ControlReversibilityTable.cpp).
 *
 * What each row argues:
 *   * safestart.get_state writes nothing. It reads the marker and the
 *     acknowledgement (as files: path, existence, size, mtime), the record the
 *     marker holds, the module's own predicates and the session's own skipped
 *     instances - all of it session-scoped state that no project checkpoint
 *     describes.
 *   * safestart.acknowledge is IRREVERSIBLE: it writes the acknowledgement the
 *     NEXT launch consumes, and nothing in this engine removes it except
 *     safestart.clear, which removes the marker with it - so it is not an
 *     inverse. The fallback is the file to delete, which is the crash group's
 *     acknowledge row in the same shape.
 *   * safestart.clear is IRREVERSIBLE: the marker describes ONE crashed session
 *     (its pid, its time, the project that was open) and nothing writes a
 *     marker from a caller's bytes - beginSession() composes one from the
 *     process it is running in. The fallback names what still holds the crash.
 *   * safestart.set_skip is IRREVERSIBLE for the reason the two above are
 *     session-scoped and this one is process-scoped: it flips the load-time
 *     switch, and instances already skipped were never created, so setting the
 *     switch back is not an inverse of what happened. It is a mode switch, not
 *     project state - the midi.retro_capture_arm shape - and it is declared
 *     mutating because it DOES write module state, which is why it records a
 *     transaction at all.
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

const ReversibilityRow kSafeStartRows[] = {
	// =====================================================================
	// The safestart.* group (feature row 77, board task #666) - the crash
	// marker, the decision it carries, and the load-time predicate that skips
	// third-party plugin INSTANCES for the session after an unclean exit.
	// =====================================================================
	R("safestart.get_state", RC::NotMutating, false,
		"reads safe-start mode's whole state through the module's own accessors: "
		"the marker and the acknowledgement as files (path, existence, size, "
		"last-written time), the record the marker holds (the crashed session's "
		"pid, time and project), the module's predicates "
		"(markerExists/acknowledged/previousRunExitedCleanly/safeStartActive), "
		"the session-scoped skip switch, every instance this session skipped "
		"with its reason, and the directories the third-party classification "
		"treats as this build's own",
		"no write. Nothing it reads is project state: the marker, the "
		"acknowledgement and the skip switch are all outside the project file, "
		"and the plugin instances are reported from the module's own record "
		"rather than by looking at a song",
		""),
	R("safestart.acknowledge", RC::Irreversible, false,
		"accepting the offer of a normal start writes the acknowledgement file "
		"the NEXT launch consumes (reads it, deletes it, loads with third-party "
		"plugins enabled); nothing in this engine removes that file except "
		"safestart.clear, which removes the marker with it - so there is no "
		"inverse, and the decision cannot be un-taken once the next launch has "
		"read it",
		"none. The transaction records the marker's and the acknowledgement's "
		"state before the write (path, existence, size, mtime) and the mode's "
		"before-state, so the caller can see exactly what it changed",
		"delete the acknowledgement file in the working directory "
		"(zene-safe-start.acknowledged, the name include/SafeStart.h declares) "
		"and the next launch starts safe again; this command never touches the "
		"marker, which is why the crash stays visible either way"),
	R("safestart.clear", RC::Irreversible, false,
		"the marker describes ONE crashed session - its process id, its time "
		"and the project that was open - and the file is gone. Nothing in this "
		"engine writes a marker from a caller's bytes: beginSession() composes "
		"one from the process it is actually running in, which is what makes "
		"the file evidence rather than a claim",
		"none. The transaction records both paths that were removed and the "
		"marker's own size, mtime and parsed record, so the caller can see what "
		"it cleared",
		"the crash itself is still recoverable: crash.list_reports names the "
		"report the signal handler wrote (when a catchable signal was the "
		"cause) and the crash reporter's own session marker still says the last "
		"run was unclean. What is gone is this module's record of WHICH session "
		"died, which a clean exit would have removed anyway"),
	R("safestart.set_skip", RC::Irreversible, false,
		"flips the session-scoped half of the load-time predicate: with it off, "
		"a project loaded afterwards loads its third-party plugin instances. "
		"Instances skipped while it was on were never created, so setting the "
		"switch back is not an inverse of what already happened - it is a mode "
		"switch on this module's process state, and the next session re-arms it",
		"none. The transaction records the switch's previous value and the "
		"mode's before-state, so a caller can see what it moved",
		"pass 'enabled': false and load the project again: the instances load "
		"then. The marker is untouched by this command, so the crash stays "
		"visible and the next launch still starts safe unless "
		"safestart.acknowledge or safestart.clear is called"),
};

constexpr int kSafeStartRowCount =
	static_cast<int>(sizeof(kSafeStartRows) / sizeof(kSafeStartRows[0]));

} // namespace

const ReversibilityRow* reversibilitySafeStartRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kSafeStartRowCount; }
	return kSafeStartRows;
}

} // namespace control
} // namespace lmms
