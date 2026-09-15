/*
 * ControlReversibilityTableExportPresets.cpp - the render/export preset store's
 *                                              rows of THE SPEC A16 classification
 *                                              table (feature rows 70 and 71).
 *
 * A GROUP file on the same seam as ControlReversibilityTableChain.cpp: the rows
 * are one group's, whatever their class, and the class comes from each row
 * rather than from the file it happens to live in. It is its own translation
 * unit because ControlReversibilityTable.cpp joins ONE block with ONE row count
 * for every caller (ReversibilityTable's constructor, control.transactions and
 * tests/.../ReversibilityContractTest) and its files sit at this fork's 500-line
 * file-length ratchet.
 *
 * REVERSIBILITY OF THE FOUR VERBS, stated here rather than left to be inferred:
 *
 *   export.preset_list    not_mutating - it reads the store and reports the
 *                         selection in force, so there is nothing to reverse.
 *   export.preset_add     reversible (true_inverse, recorded ACTION checkpoint).
 *   export.preset_apply   reversible (true_inverse, recorded ACTION checkpoint).
 *   export.preset_remove  reversible (true_inverse, recorded ACTION checkpoint).
 *
 * NONE of the four is typed-irreversible, and that is a property of where the
 * state lives rather than an optimistic claim: a preset is a document in a file
 * tree OUTSIDE the project, and the applied selection is a process-wide scalar,
 * so both a document's bytes and the selection's previous value can be captured
 * before the write and put back afterwards. Nothing here destroys state the
 * engine cannot capture.
 *
 * The ONE case where the recorded inverse is unavailable is the bounded undo
 * stack, where a step can be evicted by later edits (docs/UNDO-BOUNDS.md); an
 * undo attempt on an evicted step must REFUSE, typed, and the named manual
 * fallback is the verb that undoes it by hand:
 *   - after export.preset_add      export.preset_remove "<name>"
 *   - after export.preset_apply    export.preset_apply "<the previous preset>",
 *                                  or export.preset_apply with no name for the
 *                                  render path's own defaults (the previous
 *                                  preset's name is in the transaction's
 *                                  before.preset)
 *   - after export.preset_remove   export.preset_add with the values the
 *                                  transaction's inverse carries (the document's
 *                                  own sample_rate / bit_depth / stereo_mode),
 *                                  which rebuilds the preset as it was
 * A render performed under an applied preset is NOT undone by the apply's
 * inverse: the file it wrote stays where it was written. The fallback there is
 * to apply the right preset and render again, and render.render's row says the
 * rendered file is an output rather than project state.
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

const ReversibilityRow kExportPresetRows[] = {
	// The render/export preset store (feature row 70). Three of the four rows
	// are true_inverse for ONE shared reason, and it is not the project: the
	// store is a file tree OUTSIDE the project (the user preset tree's
	// renderpresets/, so a preset is the same one whichever project is open),
	// which no Song checkpoint carries and no live object restores, while the
	// applied selection is a process-wide scalar the project's journal does not
	// hold either. Each command therefore records an ACTION checkpoint that
	// undoes its own operation - the groove pool's and the chain store's
	// mechanism.
	R("export.preset_add", RC::TrueInverse, true,
		"writes ONE document into the preset store. The store is outside the "
		"project, so the project's own journal does not carry it",
		"action checkpoint: the recorded step removes the document this command "
		"created, or writes the revision it replaced (before.previous_sha256) "
		"back to the preset's own path; the redo half re-writes the document",
		""),
	R("export.preset_apply", RC::TrueInverse, true,
		"puts the render path on a stored preset: the sample rate, bit depth and "
		"stereo mode the NEXT render is started with. The selection is "
		"process-wide, not project state, and the values a render must obey are "
		"read at both ends of one process",
		"action checkpoint: the recorded step restores the selection this command "
		"replaced - the preset applied before it (carried by value, so a preset "
		"removed in between still comes back) or the render path's own defaults "
		"when none had been applied",
		""),
	R("export.preset_remove", RC::TrueInverse, true,
		"deletes one document from the preset store, outside the project",
		"action checkpoint: the document's bytes are captured before the removal "
		"and the recorded step writes them back to the same path, byte for byte, "
		"so the preset returns exactly as it was",
		""),
	// The read half, in this group file because the table's blocks are split by
	// WHAT THE INVERSE IS and not by command group (the class comes from the row).
	R("export.preset_list", RC::NotMutating, false,
		"it READS the store and reports the settings the next render will use. It "
		"writes no file, changes no project state and moves no selection",
		"nothing to reverse: no document, no model and no selection changes",
		""),
};

constexpr int kExportPresetRowCount =
	static_cast<int>(sizeof(kExportPresetRows) / sizeof(kExportPresetRows[0]));

} // namespace

const ReversibilityRow* reversibilityExportPresetRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kExportPresetRowCount; }
	return kExportPresetRows;
}

} // namespace control
} // namespace lmms
