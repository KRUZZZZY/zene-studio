/*
 * ControlReversibilityTableTrackFolder.cpp - the folder-track rows of THE SPEC
 *                                            A16 classification table: the
 *                                            true_inverse block joins them
 *                                            (owner items 3+20+21).
 *
 * This file is data, like the two halves it was split out of. It exists for one
 * reason: adding the folder group's rows pushed ControlReversibilityTable.cpp
 * and ControlReversibilityTableAction.cpp over the 500-line file ratchet, and
 * that ratchet is not moved for a new feature. The block is joined into the
 * true_inverse half by reversibilityRowTable() (ControlReversibilityTable.cpp),
 * so every caller - ReversibilityTable's constructor, control.transactions and
 * tests/.../ReversibilityContractTest - still reads ONE block with ONE row
 * count, and each row's class still comes from the row itself, never from the
 * file it happens to live in.
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

/*!
 * The folder-track group's seven true_inverse rows: TWO whose inverse is a LIVE
 * checkpoint - the folder's own <trackfolder> element carries `collapsed` and
 * `pinned`, and TrackFolder::loadTrackSpecificSettings RESETS both on absence,
 * which is what makes a checkpoint taken before the first edit able to take it
 * back off - and FIVE whose inverse is a RECORDED ACTION, because none of them
 * is one live object: the relation is an attribute on the CHILD, the mode
 * switch also rewrites every child's mixer channel, and a named visibility set
 * has no JournallingObject behind it at all.
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

const ReversibilityRow kTrackFolderRows[] = {
	R("track.folder_set_collapsed", RC::TrueInverse, true,
		"the collapse flag is part of the folder's own serialized state: the "
		"<trackfolder> child element carries `collapsed`, and "
		"TrackFolder::loadTrackSpecificSettings RESETS it on absence, which is "
		"what lets a checkpoint taken before the first collapse take it back off",
		"ProjectJournal (Track checkpoint on the folder; the restore re-loads the "
		"folder's own element)",
		""),
	R("track.set_pinned", RC::TrueInverse, true,
		"the pin flag is part of the folder's own serialized state (`pinned` on "
		"the <trackfolder> element), reset on absence by the same loader, so the "
		"checkpoint is a real inverse and not a state that survives its own "
		"absence",
		"ProjectJournal (Track checkpoint on the folder)",
		""),
	R("track.set_folder", RC::TrueInverse, true,
		"the relation lives on the CHILD's own <track> element as the `folder` "
		"attribute, and re-loading that element sets only the pending parent id - "
		"nothing runs the post-load resolution pass during a checkpoint restore, "
		"so a live checkpoint alone could not re-link the track",
		"action checkpoint: the recorded undo step re-parents the track to the "
		"folder it was in (or to the container root when it was in none) through "
		"the same Track::setParentFolder call the write uses. The track keeps its "
		"row in the flat container list - membership is the ONLY state this "
		"command changes - so no position has to be restored and the inverse is "
		"exact",
		""),
	R("track.set_routing", RC::TrueInverse, true,
		"the mode is in the folder's own <trackfolder> element, but the action is "
		"NOT one object: switching the mode on points every child's own mixer "
		"channel at the folder's channel, and those bindings belong to the "
		"children, so a folder checkpoint would restore half of the action and "
		"make one agent command cost more than one Ctrl+Z",
		"action checkpoint: the recorded undo step switches the mode back through "
		"the same call, which restores the mode AND every child's channel as ONE "
		"step - the folder's `prevch` attribute holds the binding recorded before "
		"the write. LIMIT: a channel released by the switch is deleted and "
		"switching back creates a fresh one, so a child can come back on a "
		"different channel INDEX (Mixer::deleteChannel renumbers) while still "
		"being routed through the folder, exactly as track.add's re-add takes a "
		"fresh trk-<n>",
		""),
	R("track.visibility_set_save", RC::TrueInverse, true,
		"a set is a NAMED selection in the container's own store, not a live "
		"object: there is no JournallingObject behind it, so no checkpoint can "
		"carry one",
		"action checkpoint: the recorded undo step writes the definition captured "
		"before the write back through the same call - and removes the set again "
		"when this was the save that created it",
		""),
	R("track.visibility_set_apply", RC::TrueInverse, true,
		"one apply writes the visible flag of EVERY track of the song plus the "
		"active-set name, and only the members' flags are in the set, so the "
		"recorded state - not the set - is what an inverse must put back",
		"action checkpoint: the recorded undo step writes every track's captured "
		"visible flag back and restores the previous active-set name, as ONE step "
		"(SPEC A16 deliverable 3: one agent command is one undo)",
		""),
	R("track.visibility_set_remove", RC::TrueInverse, true,
		"a deleted set has no live object behind it; the container's store holds "
		"the only copy of the definition",
		"action checkpoint: the recorded undo step writes the captured definition "
		"back through the same call and, when it was the active set, that fact "
		"too. The tracks' own visible flags are deliberately untouched in either "
		"direction - a set is a saved selection over them",
		""),
};

constexpr int kTrackFolderRowCount =
	static_cast<int>(sizeof(kTrackFolderRows) / sizeof(kTrackFolderRows[0]));

} // namespace

const ReversibilityRow* reversibilityTrackFolderRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kTrackFolderRowCount; }
	return kTrackFolderRows;
}

} // namespace control
} // namespace lmms
