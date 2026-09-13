/*
 * ControlReversibilityTableSnapshot.cpp - THE SPEC A16 classification table's
 *                                        SNAPSHOT block: one row per command
 *                                        whose inverse is a bounded recorded
 *                                        state rather than a live object.
 *
 * This file is data. It is the THIRD of the table's three literal blocks and it
 * carries no logic of its own:
 *
 *   ControlReversibilityTable.cpp         true_inverse - a live checkpoint on
 *                                        the engine's own ProjectJournal.
 *   THIS FILE                            snapshot - a bounded recorded state
 *                                        (a container snapshot, a file
 *                                        revision, a scalar) replayed by an
 *                                        inverse command, or named as the
 *                                        documented manual fallback.
 *   ControlReversibilityTablePassive.cpp  irreversible + not_mutating - the
 *                                        commands that have no inverse, or
 *                                        write nothing at all.
 *
 * ReversibilityTable's constructor reads all three and assembles ONE table, so
 * the contract is still read (and tested, by ReversibilityContractTest in both
 * directions) as a whole. The blocks are split by WHAT THE INVERSE IS, not by
 * command group, and the files are separate because this fork's file-length
 * ratchet measures a file as a unit: the true_inverse block alone had grown
 * past the 500-line limit, and splitting a table by the mechanism of its
 * inverse keeps each block readable as what it is.
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

const ReversibilityRow kSnapshotRows[] = {
	// =====================================================================
	// snapshot - no live object can be restored. The inverse is a bounded
	// recorded state: a container snapshot, a file revision, or a scalar.
	// reversible=false means the recorded state is a MANUAL fallback: an undo
	// attempt is refused, typed, and names the fallback rather than
	// pretending.
	// =====================================================================
	R("project.save", RC::Snapshot, true,
		"the engine writes the project file in place and keeps no previous "
		"revision by default. DISAGREEMENT with A16-STATUS-MEASURED.md, which "
		"records this as reversible:false with no file-level inverse",
		"file revision: before the write, the existing file is rotated into a "
		"named retention set (keep-3: <file>.rev0, .rev1, .rev2, each capped at "
		"8 MiB, 24 MiB per project). The recorded inverse is the command "
		"project.restore_revision, which control.undo dispatches. File-level "
		"commands are deliberately NOT put on the GUI undo stack - see "
		"docs/A16-REVERSIBILITY.md - because a file is not project state",
		""),
	R("plugin.load", RC::Snapshot, false,
		"two branches: appending an EFFECT has an exact inverse (the instance "
		"is new and carries only defaults), but REPLACING an instrument does "
		"not - the replaced instrument and its parameter values are gone",
		"the effect branch pushes an action checkpoint that unloads the device "
		"it created (plugin.load of an effect is therefore one undoable step). "
		"The instrument branch records the replaced plugin NAME only and is "
		"NOT reversible: replacing an instrument destroys its state",
		"the instrument branch: reload the previous instrument by dev-<n> "
		"(the transaction's before.plugin names it); its parameter values are "
		"NOT restored, so treat an instrument replacement as destructive"),
	R("mixer.remove_channel", RC::Snapshot, false,
		"the channel's full state is in the transaction's before-state, but "
		"there is no command that recreates a channel WITH state: "
		"mixer.add_channel always makes a default channel",
		"bounded container snapshot: before-state holds the removed channel's "
		"name, gain, mute/solo, sends and effects, so the state is not lost - "
		"only the automatic replay is missing. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md in one respect: the doc lists this command "
		"under \"not exercised\", so its class was unmeasured; this row is the "
		"measurement",
		"recreate the channel with mixer.add_channel and re-apply name, gain, "
		"mute/solo and sends from before; the effects must be loaded again "
		"with plugin.load and their state XML re-applied"),
	R("plugin.state_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the project is "
		"untouched, and the file's previous revision is recorded in "
		"before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content, bounded like every other state snapshot",
		"write before.previous_content back with plugin.state_save, or "
		"delete the file when before.replaced was false"),
	R("plugin.preset_save", RC::Snapshot, false,
		"the command writes a file OUTSIDE the project; the replaced revision "
		"is recorded in before.previous_content (bounded)",
		"file-level snapshot: the replaced revision is in "
		"before.previous_content",
		"write before.previous_content back with plugin.preset_save, or "
		"delete the file when before.replaced was false"),

	R("rack.remove_chain", RC::Snapshot, false,
		"the removed chain's effects and their settings are captured in the "
		"transaction's before-state, but recreating a chain WITH its effects is "
		"not one operation the registry can run - the same defect class "
		"mixer.remove_channel records for a channel, and for the same reason: "
		"the chain carries devices whose instantiation is a plugin load",
		"bounded container snapshot: before-state holds the chain's own state "
		"XML - EffectChain::saveState, the call the project file's <fxchain> is "
		"written by, capped like every other state snapshot at 64 KiB - or, "
		"when it exceeds that bound, only its size and the fact that it was "
		"oversized",
		"rack.add_chain, then plugin.load each device the captured XML names "
		"onto the new chain and plugin.state_load its state. A rack with more "
		"than one parallel chain does NOT get the removed chain's position "
		"back, so a selection that named a later chain has to be set again with "
		"rack.set_selected"),
	R("control.set_undo_depth", RC::Snapshot, true,
		"the depth cap is not project state, but LOWERING it evicts undo steps, and an evicted step is gone. The previous cap is a bounded scalar, so the change itself is reversible - what it dropped is not",
		"snapshot: the transaction's before-state holds the previous cap, and the recorded inverse is the command itself (control.undo dispatches control.set_undo_depth with steps/bytes set back). The journal's byte budget is not stored twice: it is COMPUTED from the steps the stack retains (ProjectJournal::retainedBytes) and reported by control.undo_depth",
		"the steps evicted by a cap change are NOT recoverable: control.undo restores the CAP, not the history it dropped. Raise the cap before a long session, or re-drive the edits"),

	// The browser's tag commands (030/w10-browser): restored after a merge
	// resolution took the other side wholesale and dropped them. The A16 contract
	// test caught it - it requires a row for every registered command.
	//
	// Row order note: this block is scanned before true_inverse, so these
	// appear first in the assembled table. Nothing depends on it.
	R("browser.tag.add", RC::Snapshot, true,
		"the tag store is a user-config file, not a JournallingObject: there "
		"is no object checkpoint that holds a path's tag set, and the project "
		"journal is deliberately not used for state outside the project",
		"the recorded inverse is the paired COMMAND browser.tag.remove with "
		"this path and tag (`applies: command`), which control.undo dispatches "
		"through the registry; the tag set the edit started from is in the "
		"transaction's before-state, bounded by the store's own "
		"distinct-tag cap",
		""),
	R("browser.tag.remove", RC::Snapshot, true,
		"the same store and the same absence of a checkpoint; removing the "
		"LAST tag of a file drops its entry from the store, which a "
		"re-add recreates exactly",
		"the recorded inverse is the paired COMMAND browser.tag.add with this "
		"path and tag (`applies: command`), dispatched by control.undo; the "
		"tag set the edit started from is in the transaction's before-state",
		""),
};

constexpr int kSnapshotRowCount = static_cast<int>(sizeof(kSnapshotRows) / sizeof(kSnapshotRows[0]));

} // namespace

const ReversibilityRow* reversibilitySnapshotRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kSnapshotRowCount; }
	return kSnapshotRows;
}

} // namespace control
} // namespace lmms
