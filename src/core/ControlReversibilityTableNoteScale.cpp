/*
 * ControlReversibilityTableNoteScale.cpp - the SPEC A16 contract rows of the 0.3.0
 *                                          note/scale/device wave (board task #648;
 *                                          feature-list rows 11, 66 and 81).
 *
 * A GROUP's rows, whatever their class - the class comes from each ROW, not from the
 * file, exactly as reversibilityRoutingRowTable's and
 * reversibilityScanAndCrashRowTable's do. Joined into reversibilityRowTable()
 * (ControlReversibilityTable.cpp), so control.transactions, control.undo and the
 * anti-drift test (tests/src/core/ReversibilityContractTest.cpp, which asserts BOTH
 * directions - every row names a registered command, every registered command has a
 * row) still read ONE table.
 *
 * The three mechanisms these rows name, in one place:
 *
 *  - a LIVE MidiClip checkpoint (seven rows): a clip's note list IS its serialized
 *    state, so the checkpoint taken before the edit restores every key, velocity,
 *    position and slide flag the command moved. Two facts make it exact where an
 *    attribute is OPTIONAL - Note::loadSettings reads attribute("slide").toInt()
 *    unconditionally (src/core/Note.cpp:322-323), so an absent "slide" MEANS "regular
 *    note", and it reads attribute("prob", "1") the same way.
 *  - a recorded ACTION step (four rows): the Song's MIDI seed, the scale group's
 *    root/scale context and the MPE input flag are all process or Song state that is
 *    NOT a JournallingObject, so no live checkpoint covers them. The recorded step
 *    writes the bounded before-state back - the shape clock.master_set uses.
 *  - nothing at all (four rows): the commands that only read.
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

const ReversibilityRow kNoteScaleRows[] = {
	// =====================================================================
	// Feature-list row 11 - note randomisation, note transforms, slide notes.
	// =====================================================================
	R("note.random_seed_get", RC::NotMutating, false,
		"a read of the project's MIDI seed (Song::midiSeed, the header's "
		"\"midiseed\" attribute): it writes nothing, so there is no "
		"transaction and nothing for control.undo to reverse or to be blocked by",
		"nothing to inverse. note.random_seed_set is the writer, and it carries "
		"a recorded action checkpoint",
		""),
	R("note.random_seed_set", RC::TrueInverse, true,
		"the seed is the Song's own value - read from and written to the project "
		"header (\"midiseed\") - and a Song is not a JournallingObject, so no "
		"object checkpoint covers it, while recording a whole project for one "
		"integer would cost far more than the edit",
		"recorded ACTION checkpoint: the recorded undo step calls "
		"Song::setMidiSeed with the seed the transaction's before-state holds, "
		"and the redo half re-applies the requested one, so control.undo restores "
		"the exact roll every later note.randomize would draw from",
		""),
	R("note.randomize", RC::TrueInverse, true,
		"the roll writes each note's velocity and position - both part of the "
		"clip's serialized note list - and it is multiplicatively applied to what "
		"the note already carries, so no pair of values in the command's own "
		"arguments could describe the state before it",
		"ProjectJournal (MidiClip checkpoint: MidiClip::loadSettings clears and "
		"re-loads the clip's note list, so the checkpoint taken before the roll "
		"restores every velocity and every position it moved - and a re-run is NOT "
		"the inverse, because the second roll draws from the positions the first "
		"one produced)",
		""),
	R("note.transpose", RC::TrueInverse, true,
		"the keys are part of the clip's serialized note list, and the transpose "
		"CLAMPS at the MIDI range, so a note that hit the bound would not come "
		"back from the opposite transpose",
		"ProjectJournal (MidiClip checkpoint: the mechanism note.velocity_set "
		"reverses with; the clip's own order (position, then key) is restored by "
		"the same call, and the recorded inverse op is control.undo on the clip)",
		""),
	R("note.velocity_offset", RC::TrueInverse, true,
		"the velocity is part of the clip's serialized note list, and the delta is "
		"added to the CURRENT velocity and clamped at 0..200, so the opposite delta "
		"is not an inverse at either bound",
		"ProjectJournal (MidiClip checkpoint: the checkpoint carries each note's "
		"velocity, so one control.undo puts every one back, including the notes the "
		"clamp left at a bound)",
		""),
	R("note.velocity_scale", RC::TrueInverse, true,
		"the velocity is part of the clip's serialized note list, and the scale is "
		"multiplicative on the current value with rounding and clamping, so "
		"dividing the argument back would not reproduce the previous values",
		"ProjectJournal (MidiClip checkpoint: the same live checkpoint as "
		"note.velocity_offset; the recorded inverse op is control.undo on the clip)",
		""),
	R("note.slide_set", RC::TrueInverse, true,
		"the slide flag is per-note state the owning MidiClip serializes as part of "
		"its note list",
		"ProjectJournal (MidiClip checkpoint: 'slide' is written only when it is "
		"set, but Note::loadSettings assigns attribute(\"slide\").toInt() "
		"UNCONDITIONALLY - an absent attribute means 0, a regular note - so a "
		"checkpoint taken before a FIRST slide edit restores it exactly, and the "
		"recorded inverse op is note.slide_set with the before-value)",
		""),
	R("note.slide_clear", RC::TrueInverse, true,
		"each cleared flag is per-note state in the clip's serialized note list, "
		"and the scope decides how many of them moved in one step",
		"ProjectJournal (MidiClip checkpoint: the same live checkpoint and the same "
		"reset-on-absence rule as note.slide_set, so ONE control.undo restores every "
		"flag the call cleared, in the scope it cleared them)",
		""),
	// =====================================================================
	// Feature-list row 66 - the scale group.
	// =====================================================================
	R("scale.list", RC::NotMutating, false,
		"a read of the engine's scale vocabulary (ChordTable's own entries, "
		"resolved against a root) and of this group's context: it writes nothing",
		"nothing to inverse. scale.set and scale.root_set are the two writers of "
		"the context, and both carry a recorded action checkpoint",
		""),
	R("scale.get_state", RC::NotMutating, false,
		"a read of the context (or of a root and scale the call names) plus, when "
		"a clip is named, a count of its in-scale and out-of-scale notes: it writes "
		"nothing",
		"nothing to inverse. scale.snap_notes is the group's one clip-editing verb, "
		"and its inverse is the clip's checkpoint",
		""),
	R("scale.root_set", RC::TrueInverse, true,
		"the root/scale context is this group's own process state - deliberately "
		"not serialized, like MpeExpression::isEnabled() - so it is not a "
		"JournallingObject and no checkpoint covers it",
		"recorded ACTION checkpoint: the recorded undo step writes the before-state's "
		"root and scale back and the redo half re-applies the requested pair (the "
		"clock.master_set shape, for the same reason)",
		""),
	R("scale.set", RC::TrueInverse, true,
		"the scale name is part of the same non-serialized context: it lives in "
		"memory for the life of the instance and is not project state",
		"recorded ACTION checkpoint: the same recorded step as scale.root_set - one "
		"pair of values (root, scale) is the whole of the state either writer can "
		"change, so one step restores both whatever the caller changed",
		""),
	R("scale.snap_notes", RC::TrueInverse, true,
		"the snap moves notes' KEYS, which are part of the clip's serialized note "
		"list",
		"ProjectJournal (MidiClip checkpoint: the checkpoint restores the keys the "
		"snap moved. The snap itself is NOT an inverse - a note it moved is now in "
		"the scale, so re-running it would move nothing and would not bring the old "
		"key back)",
		""),
	// =====================================================================
	// Feature-list row 81 - the device group's MPE verb.
	// =====================================================================
	R("device.mpe_get_state", RC::NotMutating, false,
		"a read of the MPE input mode and of the engine's own MPE constants: it "
		"writes nothing",
		"nothing to inverse. device.mpe_set is the writer, and it carries a "
		"recorded action checkpoint",
		""),
	R("device.mpe_set", RC::TrueInverse, true,
		"the MPE input mode is a process-wide std::atomic_bool on MpeExpression "
		"(src/core/midi/MpeExpression.cpp:35), deliberately not serialized - with "
		"it off the MIDI input path is exactly what it was before MPE existed, so "
		"it is not project state and no checkpoint holds it",
		"recorded ACTION checkpoint: the recorded undo step stores the before-value "
		"back with MpeExpression::setEnabled and the redo half re-applies the "
		"requested one - one relaxed atomic store either way, so the inverse is as "
		"cheap and as realtime-safe as the edit (the clock.master_set shape)",
		""),
};

constexpr int kNoteScaleRowCount = static_cast<int>(sizeof(kNoteScaleRows)
	/ sizeof(kNoteScaleRows[0]));

#undef R

} // namespace

const ReversibilityRow* reversibilityNoteScaleRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kNoteScaleRowCount; }
	return kNoteScaleRows;
}

} // namespace control
} // namespace lmms
