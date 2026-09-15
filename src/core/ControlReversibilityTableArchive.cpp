/*
 * ControlReversibilityTableArchive.cpp - the contract rows of the
 *                                        project.missing_assets /
 *                                        project.hash_assets / project.relink
 *                                        group (feature row 38).
 *
 * This file is part of THE classification table (ControlReversibilityTable.cpp
 * joins it into the true_inverse block through reversibilityRowTable()), and it
 * is a GROUP file on the same seam as ControlReversibilityTableTrackFolder.cpp,
 * ControlReversibilityTableVca.cpp and ControlReversibilityTableRouting.cpp: the
 * class of each row comes from the row, not from this file.
 *
 * TWO of the group's three verbs write nothing at all, and the third writes ONE
 * project file - so the group has one reversible row and two not_mutating ones.
 * The relink row is true_inverse with a RECORDED ACTION checkpoint, the same
 * shape chain.save and mastering.run use for their file writes: the engine's
 * own ProjectJournal carries the step, so an agent's relink and a user's Ctrl+Z
 * are one history.
 *
 * The portable-bundle half of the feature row is NOT here because it is not
 * implemented: it is Bar 3 (docs/KNOWN-LIMITATIONS.md, release notes).
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

const ReversibilityRow kArchiveRows[] = {
	// =====================================================================
	// The project.* reference group (feature row 38). Detection and hashing
	// are inspectors of a FILE; the rewrite is a recorded action.
	// =====================================================================
	R("project.missing_assets", RC::NotMutating, false,
		"reads the named project file and reports the files it references and "
		"which of them are not on disk. Nothing is written - not the project, "
		"not the media, not the session",
		"no write. It is deliberately not the OPEN session's view: the answer "
		"comes from the document, so the verb works on a project that cannot be "
		"loaded and on a project that is not the one open, and it neither loads "
		"nor mutates anything",
		""),
	R("project.hash_assets", RC::NotMutating, false,
		"reads the same reference list and hashes the media that is on disk at "
		"it. Reads only",
		"no write. The one bound is stated rather than silent: a reference over "
		"the 1 GiB per-file cap comes back with hashed=false and the cap in its "
		"error, and the aggregated digest covers the reference set, not the "
		"session",
		""),
	R("project.relink", RC::TrueInverse, true,
		"rewrites the reference values of ONE project file so they point at the "
		"file that was found. The file is outside the project's own journal, "
		"the same way a chain preset and a mastering run are, so the inverse is "
		"a recorded action and not an object checkpoint",
		"action checkpoint: the project file's previous bytes are captured "
		"before the write and the recorded step writes them back, so one "
		"control.undo restores the file byte for byte; the redo half re-writes "
		"the document this command wrote. A dry run records nothing and says so "
		"(reversible=false, 'dry_run preview: nothing was changed'), and an "
		"instance with no project journal REFUSES the write rather than make an "
		"unrecordable change to a project file",
		""),
};

constexpr int kArchiveRowCount =
	static_cast<int>(sizeof(kArchiveRows) / sizeof(kArchiveRows[0]));

} // namespace

const ReversibilityRow* reversibilityArchiveRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kArchiveRowCount; }
	return kArchiveRows;
}

} // namespace control
} // namespace lmms
