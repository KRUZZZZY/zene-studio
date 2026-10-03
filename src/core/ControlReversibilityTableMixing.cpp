/*
 * ControlReversibilityTableMixing.cpp - the 0.5.0 L08 / L09 mixing and routing commands' rows of the reversibility table
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

const ReversibilityRow kMixingRows[] = {
	R("mixer.send_many", RC::TrueInverse, true, "it makes the same send or routing from several channels",
		"each send's action step, merged by the registry into one", ""),
	R("vca.set_parent", RC::TrueInverse, true, "it nests a group inside another (saved with the project)",
		"action checkpoint: the recorded undo step puts the previous nesting and links back", ""),
	R("vca.set_links", RC::TrueInverse, true, "it chooses which of a group's attributes reach its members",
		"action checkpoint: the recorded undo step puts the previous nesting and links back", ""),
	R("mixer.send_pan", RC::TrueInverse, true, "it pans one send (saved with the project)",
		"action checkpoint: the recorded undo step puts the send's previous pan back", ""),
	R("window.mixer_view", RC::NotMutating, false, "the mixer view's modes and options are interface state (the options "
		"are kept in the ui/mixerview setting), not the project", "no project write (set the previous options to restore)", ""),
	R("mixer.set_many", RC::TrueInverse, true, "it mutes or solos several channels",
		"action checkpoint: the recorded undo step puts every channel's mute and solo back", ""),
	R("mixer.set_strip", RC::TrueInverse, true, "it changes a channel's own settings (saved with the project)",
		"action checkpoint: the recorded undo step puts the channel's previous strip settings back", ""),
	R("mixer.strip_get", RC::NotMutating, false, "it reads a channel's settings and meters. Nothing is written", "no write", ""),
	R("mixer.solo_mode", RC::TrueInverse, true,
		"it writes the solo mode, a preference in the config file (ConfigManager is not a JournallingObject) - a bounded "
		"scalar", "action checkpoint: the recorded undo step writes the previous mode back, as settings.set does", ""),
	R("mixer.clear_clips", RC::NotMutating, false, "the clip lights are meter state, not the project", "no project write", ""),
	R("mixer.listen", RC::NotMutating, false, "the listen bus is a monitor switch: interface state, not the project",
		"no project write: mixer.listen with the opposite 'on' is the inverse", ""),
	R("mixer.listen_mode", RC::NotMutating, false, "the listen bus's mode is interface state, not the project",
		"no project write", ""),
};

#undef R

constexpr int kMixingRowCount = static_cast<int>(sizeof(kMixingRows) / sizeof(kMixingRows[0]));

} // namespace

const ReversibilityRow* reversibilityMixingRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMixingRowCount; }
	return kMixingRows;
}

} // namespace control
} // namespace lmms
