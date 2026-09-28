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
		"it shows or hides one editor's sub-window through the main window's own toggle slot. A "
		"window's visibility is interface state: no model, clip, track or setting is written, and "
		"the project file does not change",
		"no write (interface state only; toggle again to restore)",
		""),
	R("window.get_state", RC::NotMutating, false,
		"it reads whether each editor's sub-window is hidden. Nothing is written",
		"no write",
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
