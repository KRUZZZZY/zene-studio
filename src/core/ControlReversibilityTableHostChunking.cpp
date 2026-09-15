/*
 * ControlReversibilityTableHostChunking.cpp - the A16 rows of CODE-4
 *                                             (plugin.host_chunking)
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

const ReversibilityRow kHostChunkingRows[] = {
	// =====================================================================
	// CODE-4, feature row 82: both plugin host paths process exactly the
	// frames they are asked for, in chunks of at most the prepared block.
	// =====================================================================
	R("plugin.host_chunking", RC::NotMutating, false,
		"reads the chunking contract and this process's own chunking counters "
		"for the two host paths - process() calls, frames asked for, plug-in "
		"calls they became, multi-chunk requests, frames beyond the prepared "
		"block, the largest request and the block size the last one was "
		"prepared with",
		"no write: two relaxed-atomic snapshots of counters the audio path "
		"increments. Nothing is queued, nothing is stored, no project state and "
		"no journal step is touched, and the counters are process-wide and "
		"reset only when the process exits",
		""),
};

constexpr int kHostChunkingRowCount =
	static_cast<int>(sizeof(kHostChunkingRows) / sizeof(kHostChunkingRows[0]));

} // namespace

const ReversibilityRow* reversibilityHostChunkingRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kHostChunkingRowCount; }
	return kHostChunkingRows;
}

} // namespace control
} // namespace lmms
