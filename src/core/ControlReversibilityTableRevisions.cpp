/*
 * ControlReversibilityTableRevisions.cpp - the SPEC A16 rows for the `revisions.*`
 *                                        group: the in-app revision timeline
 *                                        (feature-list row 76, OWNER-31 item 30).
 *
 * This file is data, like the other blocks of THE classification table (see
 * ControlReversibilityTable.cpp for the first block, the live-checkpoint rows).
 * It is a GROUP file on the seam the folder, vca, routing, scan-and-crash,
 * mastering, meter, export-preset and recording files use: the class of each row
 * comes from the row itself, not from the file, and the rows land here rather
 * than in the passive block or the true_inverse file because both sit at this
 * fork's 500-line file-length ratchet.
 *
 * The two reads are not_mutating and the one writer is true_inverse. There is NO
 * irreversible row here, and the one place this group could be irreversible is
 * the place it refuses instead: a restore whose live file is over the keep-3
 * policy's per-revision cap cannot be rotated, so it is refused, typed, BEFORE
 * anything is written (the reason and the mechanism both name it, so a reader of
 * the table does not have to find that in the handler).
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

const ReversibilityRow kRevisionsRows[] = {

	// =====================================================================
	// not_mutating - the two reads. The timeline LISTS what is already on
	// disk and COUNTS two documents; neither writes anything.
	// =====================================================================
	R("revisions.list", RC::NotMutating, false,
		"reads the revision artefacts a project already has - the keep-3 set, the "
		"interface save's <file>.bak, the autosave and its sidecar - plus the metadata "
		"of each, and where asked the project's own git history. It opens files for "
		"reading and starts one bounded `git log`; it writes nothing and creates no "
		"store",
		"no write happens, so no transaction is recorded and control.undo is not "
		"blocked by it. The one child process it can start (git, bounded by "
		"RevisionTimelineBounds::GitTimeoutMs) writes nothing either: it is a "
		"read-only history query",
		""),

	R("revisions.compare", RC::NotMutating, false,
		"reads two project documents - a revision and the live file, or two "
		"revisions - and counts each one's elements by tag. It reports a difference; "
		"it does not apply one",
		"no write happens, so no transaction is recorded. The comparison is a "
		"structural summary (element counts per tag), not a merge and not a "
		"semantic diff - mmpz-git owns that, outside this process",
		""),

	// =====================================================================
	// true_inverse - the restore, the group's ONE writer.
	// =====================================================================
	R("revisions.restore", RC::TrueInverse, true,
		"restores a listed revision OVER the project file on disk. The file is not a "
		"JournallingObject and is not project state, so the project journal holds "
		"nothing for it - the inverse is a recorded ACTION checkpoint, the same shape "
		"project.restore_revision and the export-preset writers carry",
		"action checkpoint: the live file is rotated into the keep-3 revision set "
		"BEFORE the staged revision replaces it, so the file this call replaced is the "
		"new revision 0 and the recorded undo step restores it (revisions.restore -> "
		"control.undo -> restoreProjectRevision(path, 0)); the redo half re-restores "
		"the revision this call chose, from its own artefact. Where the project file "
		"did not exist before the call, the recorded step REMOVES the file the restore "
		"created, because a file that never existed cannot be restored. NAMED "
		"REFUSAL, not an irreversible path: when the live file is over the policy's "
		"8 MiB per-revision cap its rotation is refused and this command FAILS typed "
		"before writing anything, so the file on disk is the one the user already has "
		"and the revision stays listed and readable",
		""),

};

constexpr int kRevisionsRowCount =
	static_cast<int>(sizeof(kRevisionsRows) / sizeof(kRevisionsRows[0]));

} // namespace

const ReversibilityRow* reversibilityRevisionsRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kRevisionsRowCount; }
	return kRevisionsRows;
}

} // namespace control
} // namespace lmms
