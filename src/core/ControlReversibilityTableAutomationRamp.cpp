/*
 * ControlReversibilityTableAutomationRamp.cpp - the sample-accurate automation
 *                                               rows of THE SPEC A16
 *                                               classification table.
 *
 * A GROUP file, on the seam ControlReversibilityTable{Mastering,Vca,Routing,
 * TrackFolder,Meter,ScanAndCrash}.cpp already use: the class of a row comes from
 * the row's own CLASS COLUMN, never from the file it happens to live in, so this
 * feature's two rows are joined into reversibilityRowTable() through
 * reversibilityAutomationRampRowTable(). They are one file rather than two
 * because they are one feature - docs/FEATURE-LIST-0.3.0.md row 9, sample-accurate
 * automation (board task #646) - and a reviewer reads the whole contract for that
 * feature in one place.
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

/*! Sample-accurate automation (feature-list row 9; the engine is
 *  include/AutomationRamp.h, include/AutomationClip.h's writeBlockRamp() and
 *  Song::buildAutomationRamps(), proven by SampleAccurateAutomationTest). One
 *  writer and one inspector:
 *
 *   * `automation.ramp_get` writes nothing at all. It reads the project's clips
 *     and the ramps the audio thread published to their parameters, which are
 *     atomically-visible fixed-capacity values the render thread owns; it does
 *     not render, measure or advance anything.
 *
 *   * `automation.ramp_set` writes ONE bounded boolean per clip - the clip's
 *     `sample_accurate` flag - on an object that IS a JournallingObject, so the
 *     inverse is a live checkpoint and `control.undo` is the engine's own undo
 *     stack rather than a second history.
 *
 *  WHERE SAMPLE ACCURACY CANNOT HOLD, in the row rather than in a code comment
 *  (charter 3.1 deliverable 4, and the reason the flag is per CLIP):
 *
 *   1. a parameter whose consumer reads only the block's single value and never
 *      its per-sample buffer gets the block's start value and the move inside
 *      the block never reaches its DSP. The ramp is in the model; the device has
 *      to ask for it - `AutomatableModel::valueBuffer()` is the door, and the
 *      mixer fader, the fx chains and the instrument tracks are the consumers
 *      that use it;
 *   2. a clip in a PATTERN has no single block timeline (its automation is
 *      re-read against the pattern's own tick grid), so `automation.ramp_set`
 *      refuses it typed rather than pretending;
 *   3. a CubicHermite (tangent) clip is interpolated as one straight segment
 *      per tick inside the block: the curve's stored shape inside a tick is
 *      cubic and a ramp is piecewise linear, so a tangent-edited curve is
 *      APPROXIMATED at sample precision, exactly, for Linear and Discrete;
 *   4. a block that needs more knots than AutomationRamp::MaxKnots holds (a very
 *      fast tempo with a very dense tick grid) refuses the surplus and COUNTS
 *      it: the block then interpolates over the knot it did keep. The count is
 *      reported per parameter (`refused_knots` in automation.ramp_get) so the
 *      fallback is observable, never silent;
 *   5. a transport jump - loop wrap or seek - that lands inside a block is read
 *      with the ramp built for the block's start, so that block's remaining
 *      frames follow the old position's curve. The next block is exact again.
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

const ReversibilityRow kAutomationRampRows[] = {
	R("automation.ramp_set", RC::TrueInverse, true,
		"it writes ONE bounded boolean - the automation clip's sample_accurate flag - and the "
		"clip is a JournallingObject, so the checkpoint taken before the write is the inverse "
		"the engine's own undo stack replays. The flag is serialized only when it is ON and "
		"AutomationClip::loadSettings RESETS it on absence (AutomationClip.cpp), which is what "
		"makes the checkpoint carry the OFF state too: without that reset the FIRST ramp_set "
		"could not be taken back, because the before-state XML omits the attribute entirely",
		"live checkpoint: clip->addJournalCheckPoint() before the flag moves, so control.undo "
		"restores the clip - flag, curve and all - through the same ProjectJournal that "
		"automation.add_point and clip.trim use. The redo half re-applies the mode the call "
		"applied",
		"none needed. The flag is not a render: a control.undo that lands while the transport "
		"runs takes effect on the next block, which is the same one-block granularity a curve "
		"edit has always had"),
	R("automation.ramp_get", RC::NotMutating, false,
		"it reads the project's automation clips and, for each, the ramp the audio thread "
		"published to the parameter it drives - the knot count, the frames it spans and the "
		"knots the fixed capacity refused. Nothing is written, rendered or advanced by the "
		"read, and the ramp it reports was built by the render thread, not by this call",
		"no write",
		""),
};

constexpr int kAutomationRampRowCount =
	static_cast<int>(sizeof(kAutomationRampRows) / sizeof(kAutomationRampRows[0]));

} // namespace

const ReversibilityRow* reversibilityAutomationRampRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kAutomationRampRowCount; }
	return kAutomationRampRows;
}

} // namespace control
} // namespace lmms
