/*
 * ControlReversibilityTableW3Library.cpp - the 0.5.0 L11 / L12 (W3) library and project commands' rows of the SPEC A16 table
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

const ReversibilityRow kW3LibraryRows[] = {
	// L12 (MX-170): mixer snapshots.
	R("mixer.snapshot_store", RC::TrueInverse, true, "it stores the mixer's state under a name in the project (<mixer-snapshots>)",
		"action checkpoint: the recorded undo step puts the previous snapshot list back", ""),
	R("mixer.snapshot_recall", RC::TrueInverse, true, "it writes a snapshot's faders, pans, mutes, solos and send settings onto the "
		"channels and sends that still exist",
		"action checkpoint: the recorded undo step puts the whole mixer back as it was captured just before the recall", ""),
	R("mixer.snapshot_list", RC::NotMutating, false, "it reads the stored snapshots. Nothing is written", "no write", ""),
	R("mixer.snapshot_remove", RC::TrueInverse, true, "it removes a stored snapshot",
		"action checkpoint: the recorded undo step puts the previous snapshot list back", ""),
	// L12 (WF-043 / WF-047 / WF-048 / WF-044): the project as a whole.
	R("project.settings", RC::TrueInverse, true, "it writes the tempo, time signature, master volume and pitch, pan law and "
		"document type", "action checkpoint: the recorded undo step puts every previous setting back", ""),
	R("project.metadata", RC::TrueInverse, true, "it writes the project's metadata fields (<project-info>)",
		"action checkpoint: the recorded undo step puts the previous fields back", ""),
	R("track.set_locked", RC::TrueInverse, true, "it locks or unlocks a track against editing (<edit-locks>)",
		"action checkpoint: the recorded undo step puts the previous lock back", ""),
	R("clip.set_locked", RC::TrueInverse, true, "it locks or unlocks a clip against editing (<edit-locks>)",
		"action checkpoint: the recorded undo step puts the previous lock back", ""),
	R("project.locks", RC::NotMutating, false, "it reads the locked tracks and clips. Nothing is written", "no write", ""),
};

#undef R

constexpr int kW3LibraryRowCount = static_cast<int>(sizeof(kW3LibraryRows) / sizeof(kW3LibraryRows[0]));

} // namespace

const ReversibilityRow* reversibilityW3LibraryRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kW3LibraryRowCount; }
	return kW3LibraryRows;
}

} // namespace control
} // namespace lmms
