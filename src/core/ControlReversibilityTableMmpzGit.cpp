/*
 * ControlReversibilityTableMmpzGit.cpp - the contract rows of the mmpz-git
 *                                       depth group (docs/FEATURE-LIST-0.3.0.md
 *                                       row 42, task #612).
 *
 * The engine half is tools/mmpz-git/mmpz_git.py; this file is the A16 table
 * that classifies the four control-surface wrappers in
 * ControlCommandsProjectMmpzGit.cpp.
 *
 * All four commands are NOT_MUTATING from the registry's point of view: they
 * operate on project FILES, not the running session, so they do not push an
 * undo step and they do not change what the engine is playing.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
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

#define R(id, cls, rev, reason, mechanism, fallback) 	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kMmpzGitRows[] = {
	// =====================================================================
	// The mmpz-git depth group (feature row 42, task #612).
	// =====================================================================
	R("project.merge", RC::NotMutating, false,
		"operates on project FILES (base, ours, theirs), not the running session. "
		"The merge result is written to the 'ours' path; the session is unchanged",
		"no session mutation. The merge driver is a Python tool that works on the "
		"file system; it does not load the project into the engine and it does not "
		"write into the song. A conflicted merge leaves conflict comments in the "
		"file that a human must resolve",
		""),
	R("project.diff", RC::NotMutating, false,
		"reads two project files and reports the semantic differences between them. "
		"Nothing is written",
		"no write. A pure inspector of two files, producing an operation list "
		"(added, removed, changed, moved elements). The running session is untouched",
		""),
	R("project.conflicts", RC::NotMutating, false,
		"reads a merged project file and reports the musical conflicts marked in it "
		"by the mmpz-git merge driver. Nothing is written",
		"no write. A pure inspector of one file, re-presenting the conflict "
		"comments the merge driver left behind",
		""),
	R("project.audible_diff", RC::NotMutating, false,
		"renders two project files to WAV (through the built binary in a child "
		"process) and compares them bar-by-bar.  Output artefacts are written to a "
		"temporary directory; the running session is unchanged",
		"no session mutation. The renders are child processes and the comparison is "
		"over the resulting WAV files. The session being played is not touched",
		""),
};

constexpr int kMmpzGitRowCount =
	static_cast<int>(sizeof(kMmpzGitRows) / sizeof(kMmpzGitRows[0]));

} // namespace

const ReversibilityRow* reversibilityMmpzGitRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMmpzGitRowCount; }
	return kMmpzGitRows;
}

} // namespace control
} // namespace lmms
