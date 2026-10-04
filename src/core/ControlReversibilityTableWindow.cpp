/*
 * ControlReversibilityTableWindow.cpp - the `window.*` group's rows of THE SPEC
 *                                        A16 classification table.
 *
 * A GROUP file on the same seam as ControlReversibilityTableMeter.cpp: the class
 * comes from each row, and the block is joined into reversibilityRowTable()
 * through reversibilityWindowRowTable() (declared beside the join, as the mts.*
 * and sample.* blocks are - include/ControlReversibility.h sits over the
 * file-length ratchet). Both rows are not_mutating: an editor window's
 * visibility is interface state, never project state, so there is nothing to
 * journal and nothing to undo - the rule midi.learn_toggle's row states.
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

const ReversibilityRow kWindowRows[] = {
	R("window.toggle", RC::NotMutating, false,
		"it shows or hides one shell panel (switching view when the current one does not offer "
		"the panel) or one floating window. Panel and window visibility is interface state, kept "
		"in the ui/shell.layout setting: no model, clip, track or project data is written, and the "
		"project file does not change",
		"no write to the project (interface state only; toggle again to restore)",
		""),
	R("window.get_state", RC::NotMutating, false,
		"it reads the shell's view and every panel's and floating window's visibility. Nothing is "
		"written",
		"no write",
		""),
	R("window.note_editor_set", RC::NotMutating, false,
		"it sets the note editor's view - the clip it shows, its tool, grid, fold, lane, ghosts and "
		"zoom. That is interface state: the notes are edited through note.*, and nothing in the "
		"project changes",
		"no write to the project (interface state only)",
		""),
	R("window.sample_editor", RC::NotMutating, false,
		"it opens the Sample Editor on an audio clip and sets its view - the selected source range, the fields' unit "
		"and the waveform's zoom. That is interface state: the audio is edited through sample.edit_range, and nothing "
		"in the project changes",
		"no write to the project (interface state only)",
		""),
	R("window.select", RC::NotMutating, false,
		"it sets which track and clip the Inspector and the Device panel show. A selection is "
		"interface state: no model, clip, track or project data is written",
		"no write to the project (interface state only; select the previous track to restore)",
		""),
	R("window.pin", RC::NotMutating, false, "it pins a panel open across the views: interface state kept with the layout setting", "no write", ""),
	R("window.set_view", RC::NotMutating, false,
		"it switches the shell between its Arrange, Mix and Edit views. A view is interface state, "
		"kept in the ui/shell.layout setting: no model, clip, track or project data is written",
		"no write to the project (interface state only; set the previous view to restore)",
		""),
};

#undef R

constexpr int kWindowRowCount = static_cast<int>(sizeof(kWindowRows) / sizeof(kWindowRows[0]));

} // namespace

const ReversibilityRow* reversibilityWindowRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kWindowRowCount; }
	return kWindowRows;
}

} // namespace control
} // namespace lmms
