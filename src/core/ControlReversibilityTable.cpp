/*
 * ControlReversibilityTable.cpp - THE SPEC A16 classification table's LIVE rows:
 *                                  one row per command whose inverse is the
 *                                  engine's own live checkpoint, and the reason
 *                                  for its class.
 *
 * This file is data. It is deliberately separate from
 * ControlReversibility.cpp (the machinery) so that the whole contract can be
 * read and reviewed as a table in one place, and so the anti-drift test has
 * exactly one thing to compare against the registry.
 *
 * It holds the true_inverse rows whose inverse is a LIVE checkpoint - a plain
 * object checkpoint, a COMPOSITE one (several objects restored as ONE step), or
 * a parameter/model checkpoint - and the not_mutating read-only inspector rows
 * that have lived in this file since before the split (24 of those 25 rows are
 * also stated in ControlReversibilityTablePassive.cpp; they are carried here
 * rather than dropped so that reversibilityRowTable()'s row count, and the
 * table's, do not move). The true_inverse rows whose inverse is a RECORDED
 * ACTION - a created or deleted object, a scalar outside the project, a
 * captured XML block the transaction replays - are in
 * ControlReversibilityTableAction.cpp, the snapshot rows in
 * ControlReversibilityTableSnapshot.cpp and the irreversible / not_mutating
 * rows in ControlReversibilityTablePassive.cpp.
 *
 * ReversibilityTable's constructor reads all six files through
 * reversibilityRowTable(), which JOINS this file's rows with the action block's
 * and the four group files': the block is still ONE table with ONE row count,
 * and the anti-drift test still compares the registry against every row. The
 * files are separate because the file-length ratchet measures a file as a unit,
 * not because the contract is five contracts.
 *
 * The blocks are split by WHAT THE INVERSE IS, not by command group, so each
 * file answers one question: "what does the engine actually put back?"
 *
 * A row that ends in a coalescing target (the RC() macro) declares that a run
 * of that command on ONE named thing is a single undo step - which is what
 * makes a 200-step drag one Ctrl+Z. See docs/UNDO-BOUNDS.md.
 *
 * Reconciled against docs/specs/A16-STATUS-MEASURED.md (the parent's measured
 * baseline of 36 exercised mutating commands, 17 reversible:true). The rows
 * where this table DISAGREES with that measurement are marked "DISAGREEMENT"
 * and the doc that ships beside this code (docs/A16-REVERSIBILITY.md) records
 * the argument for each.
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

#include <vector>

// lmmsconfig.h carries the ZENE_TELEMETRY_ENABLED packager-kill-switch define.
// ControlReversibility.h does not pull it in, and this file's rows are guarded
// by that switch, so it has to be included explicitly - without it the #ifdef
// below reads "off" even in a telemetry-enabled build and silently drops the
// two telemetry.* rows the registry does declare.
#include "lmmsconfig.h"

namespace lmms
{
namespace control
{

/*! The true_inverse block, in its SIX files JOINED: the LIVE-checkpoint rows
 *  (ControlReversibilityTableLive.cpp, this train's move - see that file's own
 *  header), then the recorded-ACTION rows (ControlReversibilityTableAction.cpp),
 *  then the GROUP files (ControlReversibilityTable{TrackFolder,Vca,Routing,
 *  ScanAndCrash,Mastering,NoteScale,Meter,ExportPreset,Recording,Interchange,
 *  Chord,Archive,HostChunking,WasmRender}.cpp). Every caller -
 *  ReversibilityTable's constructor, and through it control.transactions and
 *  tests/…/ReversibilityContractTest - still reads ONE block with ONE row count.
 *  The join is built once, on the first call; the rows are static data.
 */
const ReversibilityRow* reversibilityRowTable(int* rowCount)
{
	static const std::vector<ReversibilityRow> joined = [] {
		// The live-checkpoint rows come first, as they did when they seeded
		// this vector; live(1) open/close. A repeated id is OVERWRITTEN.
		std::vector<ReversibilityRow> all;
		// The group files, in the order ReversibilityTable's map wants them: a
		// repeated id is OVERWRITTEN, so the order is part of the contract.
		for (const ReversibilityRow* (*rowsFor)(int*) : {reversibilityLiveRowTable,
				reversibilityActionRowTable,
				reversibilityTrackFolderRowTable, reversibilityVcaRowTable, reversibilityRoutingRowTable,
				reversibilityVerbRowTable, reversibilityScanAndCrashRowTable, reversibilityMasteringRowTable, reversibilityNoteScaleRowTable, reversibilityMeterRowTable, reversibilityExportPresetRowTable, reversibilityRecordingRowTable, reversibilityInterchangeRowTable, reversibilityChordRowTable, reversibilityArchiveRowTable, reversibilityHostChunkingRowTable, reversibilityWasmRenderRowTable, reversibilityDawProjectRowTable, reversibilityMmpzGitRowTable, reversibilityAutomationModesRowTable, reversibilityControllerRowTable})
		{
			int count = 0;
			const ReversibilityRow* rows = rowsFor(&count);
			all.insert(all.end(), rows, rows + count);
		}
		return all;
	}();
	if (rowCount != nullptr) { *rowCount = static_cast<int>(joined.size()); }
	return joined.data();
}

} // namespace control

} // namespace lmms
