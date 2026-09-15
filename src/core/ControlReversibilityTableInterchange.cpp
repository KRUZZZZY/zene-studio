/*
 * ControlReversibilityTableInterchange.cpp - the `interchange.*` group's rows of
 *                                           THE SPEC A16 classification table.
 *
 * A GROUP file on the seam include/ControlReversibility.h documents: the class
 * comes from each ROW and not from the file it lives in, and the block is joined
 * into the true_inverse half by reversibilityRowTable() (ControlReversibilityTable.cpp)
 * so every caller - ReversibilityTable's constructor, control.transactions and
 * tests/.../ReversibilityContractTest - still reads ONE block with ONE row count.
 * The rows land here rather than in either of the two big blocks because the file
 * length ratchet allows 500 lines and ControlReversibilityTable.cpp is at 499
 * (itself already split twice for the same reason).
 *
 * THREE of the four rows are not_mutating, and that is a claim about the session,
 * not about the filesystem: smf_export writes a file OUTSIDE the project and
 * smf_read/smf_convention only read, so there is no session state for an inverse
 * to restore - the same class project.save and render.stems carry, and for the
 * same reason. smf_import is the one that moves session state.
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

/*! The four rows. The reasoning that has to be written down, because it is the
 *  part a reviewer cannot re-derive from the code:
 *
 *  - the three readers/writers of the FILESYSTEM are not_mutating on the same
 *    ground project.save is: what they change is a file outside the session, and
 *    the session's own serialized state is untouched, so there is nothing an
 *    inverse could restore. The export's refusal rules (a relative path, an
 *    existing file without `overwrite`) are argument semantics, not undo.
 *
 *  - smf_import replaces the tempo map, which is exactly the state
 *    transport.tempo_map_add writes - and it is NOT inside the Song's own
 *    checkpoint, so the inverse is the same recorded ACTION checkpoint the map's
 *    own commands use (see ControlReversibilityTableAction.cpp's tempo-map rows
 *    for the full account) rather than a live object checkpoint. The captured map
 *    is written back through TempoMapPublisher::edit when the stack unwinds, so
 *    control.undo after an import restores the previous map exactly, and the
 *    before-state the transaction carries is the map itself.
 */
const ReversibilityRow kInterchangeRows[] = {
	R("interchange.smf_convention", RC::NotMutating, false,
		"it reports the tick/PPQ, tempo-unit and time-signature convention this build's "
		"Standard MIDI File interchange uses - compile-time constants - and touches no session "
		"state at all",
		"none needed: the command does not write anything (the `control.commands_list` and "
		"`app.version` shape)",
		""),
	R("interchange.smf_export", RC::NotMutating, false,
		"it writes a Standard MIDI File at the caller's own path and changes nothing inside the "
		"session: the tempo map, the tracks and the project file are all unread-and-unwritten by "
		"it, so there is no session state an inverse could restore",
		"none needed for the session; the file it wrote is the caller's own artefact, and the "
		"reply carries its path, size and hash so a caller can identify or delete it",
		""),
	R("interchange.smf_read", RC::NotMutating, false,
		"it reads a file from disk and reports its conductor events; the session is not touched, "
		"so a second identical call answers the same thing and there is nothing to take back",
		"none needed: a pure read",
		""),
	R("interchange.smf_import", RC::TrueInverse, true,
		"the tempo map is the session state it replaces, and the map is not a JournallingObject "
		"(it is not inside the Song's own checkpoint, which captures "
		"TrackContainer::saveSettings), so the inverse is a recorded ACTION checkpoint that "
		"carries the map as it was before the import",
		"ProjectJournal (action checkpoint: the captured TempoMap is written back through "
		"TempoMapPublisher::edit when the undo stack unwinds, which is the mechanism the "
		"transport.tempo_map_* commands use for the same object; the recorded inverse names no "
		"single command because the previous map is not any one file, and the transaction's "
		"before-state IS that map)",
		""),
};

constexpr int kInterchangeRowCount =
	static_cast<int>(sizeof(kInterchangeRows) / sizeof(kInterchangeRows[0]));

} // namespace

const ReversibilityRow* reversibilityInterchangeRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kInterchangeRowCount; }
	return kInterchangeRows;
}

} // namespace control
} // namespace lmms
