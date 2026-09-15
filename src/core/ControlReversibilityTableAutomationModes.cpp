/*
 * ControlReversibilityTableAutomationModes.cpp - the automation-mode rows of
 *                                               THE SPEC A16 classification
 *                                               table.
 *
 * A GROUP file, on the same seam as ControlReversibilityTable{Mastering,Vca,
 * Routing,TrackFolder,Meter,ScanAndCrash}.cpp: the class of a row comes from
 * the row's own CLASS COLUMN, never from the file it happens to live in, so
 * this feature's two rows are joined into reversibilityRowTable() through
 * reversibilityAutomationModesRowTable().
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

/*! Automation modes (feature-list rows 10 and 63; board task #647): the
 *  off/read/touch/latch/write mode state machine and the per-clip record flag.
 *
 *  The engine half is include/AutomatableModel.h (the mode enum - Off included,
 *  appended last - the atomic state, the decision function automationWantsWrite
 *  and the note that Off's read-path half lives in Song's apply pass) and
 *  src/core/AutomatableModel.cpp (the state-machine implementation), proven
 *  by AutomationModesTest: the no-destruction property (ride a control in Read
 *  and assert the recorded automation is bit-identical, paired with a Touch run
 *  that must observe a change so the comparison cannot pass by being blind), the
 *  Off-vs-Read difference in the READ path (with a Read leg as its sensitivity
 *  control), and the mode-decision table. The command surface's own proof is
 *  ControlAutomationScriptTest::modeReadRideThroughTheSocketCannotTouchTheRecordedAutomation
 *  (the same property through automation.add_point / automation.mode_set /
 *  plugin.param_set, with a write-mode leg that must change the clip).
 *
 *  WHERE THE MODES CANNOT HOLD, in the row rather than in a code comment:
 *
 *   1. The mode is RUNTIME STATE: it is not saved in the project file and it
 *      is not journalled, so a mode change has no undo and a reload resets
 *      every control to Read with no trim. Persistence is the first follow-up.
 *   2. Only the mixer fader is wired to a touch gesture (press/move/release).
 *      Pan, sends and plugin-parameter knobs would each need widget hooks - so
 *      Touch and Latch have nothing to take hold of through the socket yet;
 *      Write needs no gesture and is fully drivable. The mode state machine is
 *      per-AutomatableModel, so the missing piece is the widget hook, not the
 *      semantics.
 *   3. Write mode does not erase the un-passed remainder of the clip: it
 *      overwrites where the playhead reaches and leaves the automation ahead
 *      of it untouched. That is the safe direction.
 *   4. An offline render reports "not running" to the transport observer, so
 *      an export can never modify automation, even if a control was left in
 *      Write mode.
 *   5. The trim offset is applied where the automation is read out to the
 *      control (Song's apply pass) and is never written back into the clip,
 *      but the trim itself is also runtime state with no persistence.
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

const ReversibilityRow kAutomationModesRows[] = {
	R("automation.mode_set", RC::Irreversible, false,
		"it writes ONE atomic scalar - the AutomatableModel's automation mode (off / read / touch / "
		"latch / write) - and that "
		"model does NOT call addJournalCheckPoint() on a mode change: the mode is runtime "
		"state, not persisted in the project file and not reversable through the undo stack. "
		"The design decision (docs/AUTOMATION-MODES.md) is that the alpha's project files "
		"must keep loading unchanged, so saveSettings/loadSettings were deliberately not "
		"touched, and a mode change is therefore irreversible in this engine. The mode is never "
		"a way to edit automation either: off, read and an idle touch/latch pass leave the clip "
		"bit-identical (AutomationModesTest; ControlAutomationScriptTest through the socket), and "
		"the clip itself has its own row below",
		"no inverse: the mode is runtime state. Re-set it to the desired mode with a second "
		"automation.mode_set call. The mode a parameter is IN is reported by "
		"automation.get_state, so the state to restore is readable",
		"none needed: the mode is runtime state and can be changed again at any time"),
	R("automation.record_mode_set", RC::TrueInverse, true,
		"it writes ONE bounded boolean - the AutomationClip's isRecording flag - and the "
		"clip IS a JournallingObject, so the checkpoint taken before the write is the "
		"inverse the engine's own undo stack replays",
		"live checkpoint: clip->addJournalCheckPoint() before the flag moves, so "
		"control.undo restores the clip - flag, curve and all - through the same "
		"ProjectJournal that automation.add_point uses",
		"none needed. The flag takes effect on the next rendered block, which is the same "
		"one-block granularity a curve edit has always had"),
};

constexpr int kAutomationModesRowCount =
	static_cast<int>(sizeof(kAutomationModesRows) / sizeof(kAutomationModesRows[0]));

} // namespace

const ReversibilityRow* reversibilityAutomationModesRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kAutomationModesRowCount; }
	return kAutomationModesRows;
}

} // namespace control
} // namespace lmms
