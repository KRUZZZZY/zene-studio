/*
 * ControlReversibilityTableArranger.cpp - the 0.5.0 L04 arranger commands' rows of the SPEC A16
 *     classification table (marker.*, range.*, clip edits), joined into reversibilityRowTable()
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
 *
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

const ReversibilityRow kArrangerRows[] = {
	R("marker.list", RC::NotMutating, false, "it reads the project's markers. Nothing is written", "no write", ""),
	R("marker.jump", RC::NotMutating, false,
		"it moves the play position to a marker (transport.seek): engine run state, not project state", "no write", ""),
	R("marker.add", RC::TrueInverse, true,
		"markers are saved with the project; the list is not a JournallingObject but it is bounded (512 markers)",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
	R("marker.set", RC::TrueInverse, true, "the same bounded marker list",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
	R("marker.remove", RC::TrueInverse, true, "the same bounded marker list",
		"action checkpoint: the recorded undo step puts the previous marker list back", ""),
};

#undef R

constexpr int kArrangerRowCount = static_cast<int>(sizeof(kArrangerRows) / sizeof(kArrangerRows[0]));

} // namespace

const ReversibilityRow* reversibilityArrangerRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kArrangerRowCount; }
	return kArrangerRows;
}

} // namespace control
} // namespace lmms
