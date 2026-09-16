/*
 * ControlReversibilityTableClapInstrument.cpp - the A16 rows of the CLAP
 *                                              instrument path (feature row 79)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

const ReversibilityRow kClapInstrumentRows[] = {
	// =====================================================================
	// Feature row 79, board task #669: the CLAP note path and the CLAP
	// instrument's audio-output configuration, observable through
	// `plugin.host_notes`. The two ids the feature makes drivable are
	// plugin.list (format=clap lists the CLAP classes, instruments included)
	// and plugin.load (loads one onto an instrument track); both already have
	// A16 rows of their own - plugin.list's not_mutating row in
	// ControlReversibilityTableLive.cpp and plugin.load's snapshot row in
	// ControlReversibilityTableSnapshot.cpp - so this file carries the one
	// id the feature ADDS, and it does not duplicate either.
	// =====================================================================
	R("plugin.host_notes", RC::NotMutating, false,
		"reads the CLAP host path's note route as data: the note input ports "
		"(clap.note-ports) and the audio layout of the CLAP plug-in loaded last, and the "
		"note events the MIDI route pushed, the ones the bounded queue (or a plug-in "
		"with no note input port) refused, the ones put into a plug-in's input event "
		"list and the note-ONs among them",
		"no write: two relaxed-atomic snapshots of counters the audio path increments, "
		"plus the port facts load() recorded in the same counters. Nothing is queued, "
		"nothing is stored, no project state and no journal step is touched, and the "
		"counters are process-wide and reset only when the process exits",
		""),
};

constexpr int kClapInstrumentRowCount =
	static_cast<int>(sizeof(kClapInstrumentRows) / sizeof(kClapInstrumentRows[0]));

} // namespace

const ReversibilityRow* reversibilityClapInstrumentRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kClapInstrumentRowCount; }
	return kClapInstrumentRows;
}

} // namespace control
} // namespace lmms
