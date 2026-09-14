/*
 * ControlReversibilityTableMastering.cpp - the `mastering.*` group's rows of THE
 *                                          SPEC A16 classification table.
 *
 * A GROUP file, on the seam ControlReversibilityTable{TrackFolder,Vca,Routing}.cpp
 * already use: the block's CLASS comes from each row, never from the file it
 * happens to live in, so a group's rows (here one recorded-action true_inverse
 * row and two not_mutating inspectors) are joined into reversibilityRowTable()
 * through reversibilityMasteringRowTable(). The rows are one file rather than
 * three because they are one feature (docs/FEATURE-LIST-0.3.0.md rows 25 and 72,
 * auto-mastering wave 1), and a reviewer reads the whole contract for that
 * feature in one place.
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

/*! The auto-mastering surface (feature rows 25 and 72; docs/AUTO-MASTERING.md,
 *  task #610). Two reads and one writer:
 *
 *   * the two reads write nothing - one publishes the engine's own candidate set
 *     (MasteringJob::defaultCandidates) and one publishes the last run's report
 *     plus the live, hashed state of the files it wrote;
 *   * `mastering.run` writes FILES in a directory the caller names, OUTSIDE the
 *     project. No Song checkpoint carries them and no live object restores them,
 *     so its inverse is a recorded ACTION checkpoint - the mechanism the
 *     chain-preset store (ControlReversibilityTableChain.cpp) and the groove
 *     pool use - which removes every file the run created and writes the
 *     revisions the directory already held back byte for byte, both captured
 *     before the first write and bounded (a run whose capture would exceed the
 *     bound is REFUSED rather than performed without an inverse).
 *
 *  THE TRAP THIS ROW NAMES. A checkpoint captures the state BEFORE the write, so
 *  an inverse that only RESTORES what it captured takes nothing back on a first
 *  run: the candidate files did not exist, so there is no revision to write and
 *  the run's outputs stay on disk. The removal half is the load-bearing one, and
 *  the registered transcript proves it (a run into an empty directory, then
 *  control.undo, then the directory is empty again). The other shape of the same
 *  trap - a feature the engine serialises only when it is non-default, so a
 *  restore of the pre-first-edit XML cannot put it back - does not arise here for
 *  a stated reason: this group writes NO project XML at all. The render runs in a
 *  child process on a serialised copy of the session, so no attribute, no element
 *  and no model of the running project is touched by any mastering command.
 */

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

const ReversibilityRow kMasteringRows[] = {
	R("mastering.list_candidates", RC::NotMutating, false,
		"it reads the engine's own candidate set (MasteringJob::defaultCandidates) and the "
		"chain settings each candidate implies. No render is started and nothing is written",
		"no write",
		""),
	R("mastering.get_state", RC::NotMutating, false,
		"it reads this instance's record of its own last mastering.run and then HASHES the "
		"files that run wrote. Hashing is a read; the record is surface memory, not project "
		"state (it is neither saved nor restored with a project)",
		"no write",
		""),
	R("mastering.run", RC::TrueInverse, true,
		"it renders the mix ONCE in a child process and writes N candidate wav files into a "
		"directory the caller names, outside the project: no Song checkpoint carries those "
		"files and no live object restores them. The session itself is NOT modified (the child "
		"renders a serialised copy), which is why nothing about the project is recorded here",
		"action checkpoint: the run's output directory is captured BEFORE the first write "
		"(held_before: the path, size and sha256 of every .wav it already contains, plus the "
		"bytes themselves in the recorded step) and the recorded undo removes every file the "
		"run created and writes every held revision back byte for byte. ONE-WAY: there is no "
		"redo half (a faithful redo would have to hold the run's own outputs), so control.redo "
		"has nothing to replay and re-issuing mastering.run is the way back. BOUND: the capture "
		"is bounded at 64 MiB of pre-existing wav files and a run that would exceed it is "
		"REFUSED rather than performed without an inverse",
		"the run's own candidate files are an OUTPUT artefact: an undo takes them off disk, and "
		"an agent that wants them back re-issues mastering.run (the settings and the file set "
		"are reproducible, the audio is not bit-reproducible - see docs/AUTO-MASTERING.md "
		"section 5)"),
};

constexpr int kMasteringRowCount =
	static_cast<int>(sizeof(kMasteringRows) / sizeof(kMasteringRows[0]));

} // namespace

const ReversibilityRow* reversibilityMasteringRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMasteringRowCount; }
	return kMasteringRows;
}

} // namespace control
} // namespace lmms
