/*
 * ControlReversibilityTableSample.cpp - the sample.* group's A16 rows (#706)
 *
 * Copyright (2026) Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
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

const ReversibilityRow kSampleRows[] = {
	// =====================================================================
	// The sample.* group - the destructive waveform editor's first slice
	// (board card #706). Every row is a TRUE INVERSE, and none of them
	// carries a buffer in the transaction record: decision §2.1 measured why
	// (src/core/ControlReversibilityTableStructure.cpp:112/:128 and
	// include/ControlReversibility.h:437/:439 - a per-record cap of 64 KiB is
	// 1/5.4 of ONE second of stereo 32-bit float at 44.1 kHz, so an in-place
	// operator would be reversible:false by construction), and this group
	// routes every operator through the clip model instead.
	// =====================================================================
	R("sample.generate", RC::TrueInverse, true,
		"the generator hands its buffer to a NEW SampleClip it creates through "
		"Track::createClip - it never overwrites a source an existing clip plays "
		"(decision §3.1) - and the Track checkpoint taken before the creation "
		"carries every clip the track holds, so one undo removes the clip and its "
		"buffer together; the recorded clip.delete names the manual equivalent",
		"ProjectJournal (Track checkpoint: Track::restoreState re-loads the "
		"track's serialized clips, which is how the GUI's own clip paths reverse "
		"themselves; applies defaults to the journal, ControlEditSupport)",
		""),
	R("sample.amplify", RC::TrueInverse, true,
		"the bake computes a transformed COPY of the frames (sampleops::amplify) "
		"and swaps it in with SampleClip::setSampleBuffer - the previous buffer is "
		"never written, so a shared source keeps its audio - and the Clip "
		"checkpoint taken first captured the clip's serialized source (its src "
		"file or data attribute), which is what one undo re-loads; the "
		"transaction's before-state is bounded metadata (id/frames/rate), never "
		"the samples (decision §2.1)",
		"ProjectJournal (Clip checkpoint: SampleClip::saveSettings writes the "
		"source into the clip's own state and loadSettings re-reads it, so one "
		"control.undo - and one Ctrl+Z - restores the previous buffer)",
		""),
	R("sample.normalize", RC::TrueInverse, true,
		"same route as sample.amplify with the scale factor measured from the "
		"source's own peak (sampleops::peakOf): a transformed COPY swapped in "
		"through SampleClip::setSampleBuffer against the Clip checkpoint taken "
		"before the write, so one undo re-loads the previous buffer; a silent "
		"source is refused before any checkpoint with the peak that stopped it",
		"ProjectJournal (Clip checkpoint: the clip's serialized source - src or "
		"data - captured before SampleClip::setSampleBuffer replaced it; the "
		"transaction's before-state is bounded metadata, never the samples)",
		""),
	R("sample.reverse", RC::TrueInverse, true,
		"the bake writes a frame-reversed COPY (sampleops::reverse) back through "
		"SampleClip::setSampleBuffer, never the frames in place, and the Clip "
		"checkpoint taken first carries the previous source, so the bake is its "
		"own documented inverse - one undo re-loads the old order, no second "
		"verb and no flag machinery (decision §2.1, §3.2)",
		"ProjectJournal (Clip checkpoint: the clip's serialized source - src or "
		"data - captured before SampleClip::setSampleBuffer replaced it; the "
		"transaction's before-state is bounded metadata, never the samples)",
		""),
	R("sample.fade", RC::TrueInverse, true,
		"the bake writes a linearly ramped COPY (sampleops::fade, whole source) "
		"back through SampleClip::setSampleBuffer rather than touching the frames "
		"a clip already plays, and the Clip checkpoint taken first carries the "
		"previous source, so one undo re-loads it; clip.set_fade remains the "
		"non-baking window fade and is not this verb (decision §3.2)",
		"ProjectJournal (Clip checkpoint: the clip's serialized source - src or "
		"data - captured before SampleClip::setSampleBuffer replaced it; the "
		"transaction's before-state is bounded metadata, never the samples)",
		""),
};

constexpr int kSampleRowCount = static_cast<int>(sizeof(kSampleRows) / sizeof(kSampleRows[0]));

} // namespace

const ReversibilityRow* reversibilitySampleRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kSampleRowCount; }
	return kSampleRows;
}

} // namespace control
} // namespace lmms
