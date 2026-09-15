/*
 * ControlReversibilityTableDawProject.cpp - the `dawproject.*` group's rows of
 *                                           THE SPEC A16 classification table.
 *
 * A GROUP file on the seam include/ControlReversibility.h documents: the class
 * comes from each ROW and not from the file it lives in, and the block is joined
 * into the true_inverse half by reversibilityRowTable() (ControlReversibilityTable.cpp)
 * so every caller - ReversibilityTable's constructor, control.transactions and
 * tests/.../ReversibilityContractTest - still reads ONE block with ONE row count.
 * The rows land here rather than in ControlReversibilityTable.cpp, which is
 * itself already split three times for the file-length ratchet.
 *
 * THREE of the four rows are not_mutating, and that is a claim about the SESSION,
 * not about the filesystem: dawproject.export writes a file OUTSIDE the project
 * and dawproject.read/dawproject.convention only read, so there is no session
 * state for an inverse to restore - the same class project.save and
 * interchange.smf_export carry, and for the same reason. dawproject.import is
 * the one that moves session state, and it moves ALL of it.
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
 *  - dawproject.import replaces the WHOLE session - every track, the tempo map,
 *    the global tempo and metre and each track's mixer strip - because importing
 *    a project file means importing the project. Nothing of that is inside the
 *    Song's own checkpoint in a form an inverse could restore (the checkpoint
 *    captures TrackContainer::saveSettings, which does NOT carry the mixer's
 *    strips and does not survive a track being destroyed), so the inverse is a
 *    recorded ACTION checkpoint carrying the captured document: every track's own
 *    XML taken while the track was still alive (the shape
 *    ControlReversibilityTableAction.cpp's track.remove row uses), the mixer
 *    strips, the tempo map and the two globals. The undo recreates the tracks
 *    through control::restoreTrackFromXml - the project loader's own path - and
 *    writes the strips and the map back, so control.undo after an import restores
 *    the previous session exactly, and the before-state the transaction carries
 *    is that captured document's own summary. An import that CANNOT capture the
 *    session is refused rather than performed unreversibly.
 */
const ReversibilityRow kDawProjectRows[] = {
	R("dawproject.convention", RC::NotMutating, false,
		"it reports the DAWproject version, the container and time conventions this build "
		"implements - compile-time constants - and touches no session state at all",
		"none needed: the command does not write anything (the `control.commands_list` and "
		"`interchange.smf_convention` shape)",
		""),
	R("dawproject.export", RC::NotMutating, false,
		"it writes a .dawproject container at the caller's own path and changes nothing inside the "
		"song: the tracks, the clips, the tempo map and the mixer are all read and never written "
		"by it, so there is no session state an inverse could restore",
		"none needed for the session; the file it wrote is the caller's own artefact, and the "
		"reply carries its path, size, hash and model digest so a caller can identify or delete it",
		""),
	R("dawproject.read", RC::NotMutating, false,
		"it reads a container from disk and reports its model; the session is not touched, so a "
		"second identical call answers the same thing and there is nothing to take back",
		"none needed: a pure read",
		""),
	R("dawproject.import", RC::TrueInverse, true,
		"an import REPLACES the session - every track, the tempo map, the global tempo and metre "
		"and each track's mixer strip - and none of that is inside the Song's own checkpoint in a "
		"form an inverse could restore (the checkpoint captures "
		"TrackContainer::saveSettings, which carries neither the mixer strips nor a destroyed "
		"track), so the inverse is a recorded ACTION checkpoint that carries the whole captured "
		"session as it was before the import",
		"ProjectJournal (action checkpoint: every track's own XML is captured while the track is "
		"still alive and recreated through control::restoreTrackFromXml - the same "
		"Track::create(element, container) the project loader uses - and the mixer strips, the "
		"tempo map and the two globals are written back when the stack unwinds; the recorded "
		"inverse names no single command because the previous session is not any one file, and the "
		"transaction's before-state IS the summary of that captured document. An import whose "
		"session cannot be captured is REFUSED, typed, before anything is replaced)",
		""),
};

constexpr int kDawProjectRowCount =
	static_cast<int>(sizeof(kDawProjectRows) / sizeof(kDawProjectRows[0]));

} // namespace

const ReversibilityRow* reversibilityDawProjectRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kDawProjectRowCount; }
	return kDawProjectRows;
}

} // namespace control
} // namespace lmms
