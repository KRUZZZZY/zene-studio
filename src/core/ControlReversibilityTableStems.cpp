/*
 * ControlReversibilityTableStems.cpp - the seven `stem.*` rows of THE SPEC A16
 *                                       classification table.
 *
 * Feature row 26 of docs/FEATURE-LIST-0.3.0.md (board task #653). The rows are
 * the group's contract and this file is their home, in the same shape the other
 * GROUP files of the table use (ControlReversibilityTableVca.cpp,
 * ControlReversibilityTableRouting.cpp, ...): the table's blocks are split by
 * WHAT THE INVERSE IS, and a file is split off a block when the file-length
 * ratchet says so, not when a command group asks for it.
 *
 * ALL SEVEN ARE `not_mutating`, for the reason `render.stems` is: the engine
 * they drive writes OUTPUT ARTEFACTS (four stem WAVs; a checksum-verified model
 * file) and holds its jobs in the instance's memory. A separation job is not a
 * document - it does not survive a reload - and a written stem is an output, so
 * there is nothing for a checkpoint to capture and nothing for control.undo to
 * take back.
 *
 * THE WHOLE BLOCK IS GUARDED, because the ids are: without LMMS_HAVE_STEM_SPLIT
 * there is no engine, so ControlRegistry does not register these commands
 * (src/core/ControlRegistryRegistrations.cpp) and rows that named them would
 * make ReversibilityContractTest's "every row names a registered command" fail -
 * which is the direction that test asserts. An empty block returns 0 rows, so
 * the join in src/core/ControlReversibility.cpp does not need its own guard and
 * the release configuration's row count is untouched (227 / 120 / 18 / 7 / 82,
 * docs/RELEASE-NOTES-v0.3.0-alpha.md).
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

// lmmsconfig.h carries LMMS_HAVE_STEM_SPLIT; included by name rather than
// relying on another header's chain, the way ControlReversibilityTablePassive.cpp
// includes it for its own option-gated rows.
#include "lmmsconfig.h"

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

#ifdef LMMS_HAVE_STEM_SPLIT
	// ---------- stem.* (the offline stem-separation engine; feature row 26) ----
	// Seven rows for the one group. All seven are not_mutating for the reason
	// render.stems is: the engine they drive writes OUTPUT ARTEFACTS and holds
	// its jobs in memory, and none of it is project state (a stem job does not
	// survive a reload, and a written stem WAV is an output, not a document).
const ReversibilityRow kStemRows[] = {
	R("stem.get_state", RC::NotMutating, false,
		"reads the engine's own facts: the backend, whether it can run (and the "
		"reason when it cannot), the model path the store resolves, the model "
		"contract's constants and the job counts",
		"no write",
		""),
	R("stem.job_start", RC::NotMutating, false,
		"it QUEUES a separation job and writes nothing: the mix is decoded from "
		"the file the caller names, the separation runs on the manager's own "
		"worker thread, and the resulting stems live in that job's memory until "
		"stem.job_result materialises them",
		"the project is unchanged - no track, clip or model in this instance is "
		"touched, and the job's state is the surface's own, not a document that "
		"could be checkpointed. Nothing to reverse: a job that is not wanted is "
		"stopped with stem.job_cancel",
		""),
	R("stem.job_status", RC::NotMutating, false,
		"reads the manager's own job state, progress and error",
		"no write",
		""),
	R("stem.job_cancel", RC::NotMutating, false,
		"it stops a background job; no project state and no file changes",
		"nothing to reverse: the job settles as cancelled and its partial stems "
		"are dropped with it",
		""),
	R("stem.job_result", RC::NotMutating, false,
		"it writes OUTPUT ARTEFACTS - one float32 WAV per stem (drums, bass, "
		"other, vocals) into a directory the caller names - and nothing else",
		"the project is unchanged; the stem files are outputs, not project state, "
		"and overwriting them is the caller's decision. The job keeps its stems "
		"in memory, so a caller that wants a second directory re-issues this verb "
		"instead of re-running the separation",
		""),
	R("stem.model_get_state", RC::NotMutating, false,
		"reads the model store's own facts: the resolved directory and path, "
		"whether the file is present, the spec it would download and whether that "
		"spec is pinned enough to be downloaded at all",
		"no write (an optional hash computes a checksum and writes nothing)",
		""),
	R("stem.model_download", RC::NotMutating, false,
		"it writes an ARTEFACT outside the project - a checksum-verified model "
		"file into the store's own directory, or one the caller names - and no "
		"project state",
		"the project is unchanged. A download is not a project action, so there "
		"is nothing for a checkpoint to capture, and no inverse removes a model "
		"a caller deliberately fetched. Its DECLARED BOUND is stated rather than "
		"hidden: the transfer runs on the control thread, so the surface does "
		"not answer - control.ping included - until it finishes or fails (the "
		"bound docs/RENDER-CHILD-WAIT.md:120-126 records for the child-process "
		"renders, docs/KNOWN-LIMITATIONS.md)",
		""),
};

constexpr int kStemRowCount = static_cast<int>(sizeof(kStemRows) / sizeof(kStemRows[0]));
#else
// No stem engine in this configuration: no `stem.*` id exists to classify, so
// this block contributes no row at all (and the join needs no guard).
constexpr int kStemRowCount = 0;
#endif // LMMS_HAVE_STEM_SPLIT

} // namespace


const ReversibilityRow* reversibilityStemRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kStemRowCount; }
#ifdef LMMS_HAVE_STEM_SPLIT
	return kStemRows;
#else
	return nullptr;   // with a count of 0, the caller never dereferences it
#endif
}

} // namespace control
} // namespace lmms
