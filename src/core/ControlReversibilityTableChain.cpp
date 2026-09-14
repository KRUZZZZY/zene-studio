/*
 * ControlReversibilityTableChain.cpp - the chain-preset group's rows of THE SPEC
 *                                       A16 classification table: the
 *                                       true_inverse block joins them.
 *
 * This file is data, like the halves it was split out of, and it exists for the
 * reason include/ControlReversibility.h records for the folder group: the
 * true_inverse block had grown past this fork's 500-line file-length ratchet.
 * The chain rows arrived in ControlReversibilityTableAction.cpp and the
 * MIDI-clock rows landed beside them in the SAME file; each lane was under the
 * limit on its own tree and the union is not, and the ratchet is not moved for
 * a merge. The four rows are recorded-ACTION rows - the same class the action
 * half is - so reversibilityActionRowTable() joins them, and through it
 * reversibilityRowTable(), which is still ONE block with ONE row count for
 * every caller (ReversibilityTable's constructor, control.transactions and
 * tests/.../ReversibilityContractTest). Each row's class still comes from the
 * row itself, never from the file it happens to live in.
 *
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
 * The chain-preset store (OWNER-31 item 2). These four are true_inverse for
 * ONE shared reason, and it is not the project: the store is a file tree
 * OUTSIDE the project (the user preset tree's chainpresets/, so a preset is
 * usable across projects), which no Song checkpoint carries and no live
 * object restores. Each command therefore records an ACTION checkpoint that
 * undoes its own file operation - the groove pool's four edits' mechanism.
 * The two read-only ids are in ControlReversibilityTablePassive.cpp.
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

const ReversibilityRow kChainRows[] = {
	// The chain-preset store (OWNER-31 item 2). These four are true_inverse for
	// ONE shared reason, and it is not the project: the store is a file tree
	// OUTSIDE the project (the user preset tree's chainpresets/, so a preset is
	// usable across projects), which no Song checkpoint carries and no live
	// object restores. Each command therefore records an ACTION checkpoint that
	// undoes its own file operation - the groove pool's four edits' mechanism.
	// The two read-only ids are in ControlReversibilityTablePassive.cpp.
	R("chain.save", RC::TrueInverse, true,
		"captures the target's chain - the ordered device list plus each "
		"device's own state document - and writes ONE file in the preset store. "
		"The file is outside the project, so the project's own journal does not "
		"carry it",
		"action checkpoint: the recorded step removes the preset file this "
		"command created, or writes the revision it replaced (before.previous_"
		"sha256) back to the preset's own path; the redo half re-writes the "
		"captured document",
		""),
	R("chain.apply", RC::TrueInverse, true,
		"REPLACES the target's effect chain (devices and their settings) with "
		"the preset's. The device list is not a live object a checkpoint "
		"restores, and the devices' state is not journalled",
		"action checkpoint: the chain's own <fxchain> XML is captured before the "
		"write (EffectChain::saveSettings) and the recorded step writes it back "
		"through EffectChain::loadSettings, the project loader's own path. A chain "
		"too large to capture within the bounded snapshot is REFUSED rather than "
		"replaced without an inverse",
		""),
	R("chain.rename", RC::TrueInverse, true,
		"renames one preset file in the store. The name is the key - the file name "
		"IS the store's index - and the store is outside the project",
		"action checkpoint: the recorded step renames the file back to "
		"before.path, the same operation chain.rename performs, and the redo half "
		"renames it forward again",
		""),
	R("chain.remove", RC::TrueInverse, true,
		"deletes one preset file from the store, outside the project",
		"action checkpoint: the preset's bytes are captured before the removal and "
		"the recorded step writes them back to the same path, byte for byte, so the "
		"preset returns exactly as it was",
		""),
};

constexpr int kChainRowCount =
	static_cast<int>(sizeof(kChainRows) / sizeof(kChainRows[0]));

} // namespace

const ReversibilityRow* reversibilityChainRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kChainRowCount; }
	return kChainRows;
}

} // namespace control
} // namespace lmms
