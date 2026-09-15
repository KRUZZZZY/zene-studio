/*
 * ControlReversibilityTableWasmRender.cpp - the A16 rows of CODE-5
 *                                           (wasm.pool, wasm.render_offline)
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

// The rows travel with the group exactly as the rest of the wasm.* rows do
// (ControlReversibilityTablePassive.cpp:370): a build without the wasmtime C API
// compiles no wasm.* command, so it must carry no row for an id it cannot
// answer.
#ifdef LMMS_HAVE_WASM
const ReversibilityRow kWasmRenderRows[] = {
	// =====================================================================
	// CODE-5, feature row 73: the shared WASM worker pool, and the
	// deterministic offline render with its measured tolerance verdict.
	// =====================================================================
	R("wasm.pool", RC::NotMutating, false,
		"reads the shared worker pool's own counters: lanes and the bound on "
		"them, registered workers, lane passes, processed blocks, wake-ups, "
		"wake-ups suppressed and lane parks",
		"no write: relaxed-atomic snapshots of counters that the pool's lanes "
		"and the audio path increment. Nothing is queued, no project state and "
		"no journal step is touched",
		""),
	R("wasm.render_offline", RC::NotMutating, false,
		"renders a module offline in a sandbox of its own (one block in flight, "
		"on a fresh worker of the shared pool) and reports the measurands: the "
		"pooled runs, the two inline control runs the run-to-run floor is taken "
		"from, the pairwise differences and the verdict",
		"no write to anything the engine owns: the render creates and destroys "
		"its OWN WasmWorker/WasmSandbox instances and writes no project state, "
		"no parameter and no journal step. Loading the module it names is done "
		"in that private sandbox, so the hosted module's sandbox - the one "
		"wasm.load owns - is untouched",
		""),
};

constexpr int kWasmRenderRowCount =
	static_cast<int>(sizeof(kWasmRenderRows) / sizeof(kWasmRenderRows[0]));
#else
// No wasmtime: an EMPTY table, but a valid pointer - the join in
// ControlReversibilityTable.cpp walks [rows, rows + count).
const ReversibilityRow kWasmRenderRows[1] = {};
constexpr int kWasmRenderRowCount = 0;
#endif // LMMS_HAVE_WASM

} // namespace

const ReversibilityRow* reversibilityWasmRenderRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kWasmRenderRowCount; }
	return kWasmRenderRows;
}

} // namespace control
} // namespace lmms
