/*
 * ControlReversibilityTableMeter.cpp - the `meter.*` group's rows of THE SPEC
 *                                       A16 classification table.
 *
 * A GROUP file, on the seam ControlReversibilityTable{Mastering,Vca,Routing,
 * TrackFolder,ScanAndCrash}.cpp already use: the block's CLASS comes from each
 * row, never from the file it happens to live in, so this feature's four rows
 * (one recorded-action true_inverse row plus the render-path half's row, and
 * two not_mutating inspectors) are joined into reversibilityRowTable() through
 * reversibilityMeterRowTable(). The rows are one file rather than four because
 * they are one feature (docs/FEATURE-LIST-0.3.0.md row 24, LUFS / loudness
 * metering), and a reviewer reads the whole contract for that feature in one
 * place.
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

/*! The loudness surface (feature row 24 of docs/FEATURE-LIST-0.3.0.md; the
 *  engine is include/LufsMeter.h and include/LoudnessReport.h, proven by
 *  LufsMeterTest, LoudnessReportTest and MasteringTest). One writer and two
 *  inspectors:
 *
 *   * the two inspections write nothing. `meter.get_state` reads the passive
 *     tap's published snapshot (atomics the audio thread stores into; reading
 *     them is a load, not a measurement), and `meter.measure_file` READS a file
 *     and hashes it;
 *   * `meter.arm` writes ONE bounded value - the tap's armed flag - on a
 *     subsystem that is not a JournallingObject (the tap lives on the
 *     AudioEngine), so its inverse is a recorded ACTION checkpoint, the
 *     mechanism clock.master_set and export.set_dither use for the same shape.
 *
 *  WHAT THE INVERSE IS NOT, stated here rather than left to be discovered: the
 *  READINGS are not restored, because they are not state. A measurement is
 *  surface memory - it is not project state, it is not saved with a project, it
 *  survives a save/reload only as "whatever the tap has measured since it was
 *  armed" - and arming a tap STARTS a fresh one by design, so there is no
 *  bounded value that could put a discarded measurement back. The row says this
 *  and so does the transaction record the command returns; a caller that reads
 *  either knows exactly what control.undo does (the flag) and does not (the
 *  numbers).
 *
 *  THE TRAP THIS GROUP DOES NOT HAVE. Nothing here writes a project file, a
 *  render or an audio byte: `meter.measure_file` reads a file it is given and
 *  `MasterLoudnessTap::feed()` hands the master mix to a measurement that takes
 *  it `const`. So the "the engine serialises it only when it is non-default"
 *  trap that a restore-on-pre-first-edit-XML hits cannot arise, and the
 *  passivity claim (a render measured is a render unchanged) is asserted twice
 *  on the tree: byte-identity of the fed buffer in
 *  tests/src/core/MeterTapTest.cpp, and the file's own sha256 before and after
 *  `meter.measure_file` in tests/control-meter-commands.py.
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

const ReversibilityRow kMeterRows[] = {
	R("meter.get_state", RC::NotMutating, false,
		"it reads the passive master tap's published snapshot - atomics an audio period stores "
		"into - plus the rate and channel count the meter was built with. Nothing is measured, "
		"started or written by the read itself",
		"no write",
		""),
	R("meter.measure_file", RC::NotMutating, false,
		"it READS a rendered file in bounded chunks, feeds the bytes to the BS.1770-4 meter and "
		"hashes the file. No render is started, no file is created, moved or written - the "
		"registered proof measures the file's sha256 before and after the call and compares",
		"no write",
		""),
	R("meter.arm", RC::TrueInverse, true,
		"it writes ONE bounded value, the live tap's armed flag, on a subsystem (the "
		"AudioEngine's MasterLoudnessTap) that is not a JournallingObject and owns no serialized "
		"state, so no live checkpoint can carry it. It writes no project state, no file and no "
		"audio byte: the tap only reads the master mix",
		"action checkpoint: the recorded undo step calls the tap's setEnabled() with the flag the "
		"before-state holds, so control.undo disarms an arm and re-arms a disarm, and the redo "
		"half re-applies exactly what the call applied. The READINGS ARE NOT PART OF THE INVERSE: "
		"a measurement is surface memory, not project state, and arming a tap starts a fresh "
		"measurement by design, so there is no bounded value that could restore a discarded one "
		"- a caller that wants a measurement re-arms and plays again",
		"the readings themselves (they are not state, so there is nothing to fall back to): "
		"re-send meter.arm with enabled true and let the section play again"),
	// The FOURTH row is the same feature's render-path half: the export group's
	// id that turns the `.loudness.txt` report on for the next render. It lives
	// here rather than in ControlReversibilityTableAction.cpp - which is where
	// export.set_dither's and export.set_src_quality's rows are - because that
	// file is at the 500-line file ratchet (495 lines) and this one is its
	// feature's own group file. The class still comes from the row.
	R("export.set_loudness_report", RC::TrueInverse, true,
		"ExportRenderSettings is not a JournallingObject, so there is no object checkpoint - but "
		"the selection is a bounded scalar (on/off) and the previous value is captured before the "
		"write, exactly as export.set_dither's is",
		"action checkpoint: the recorded undo step restores the previous selection through "
		"ExportRenderSettings::setLoudnessReport, exactly as the command sets the new one. The "
		"step goes on the engine's own stack, so a user's Ctrl+Z and control.undo are one history",
		""),
};

constexpr int kMeterRowCount = static_cast<int>(sizeof(kMeterRows) / sizeof(kMeterRows[0]));

} // namespace

const ReversibilityRow* reversibilityMeterRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kMeterRowCount; }
	return kMeterRows;
}

} // namespace control
} // namespace lmms
