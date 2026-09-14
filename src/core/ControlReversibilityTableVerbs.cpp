/*
 * ControlReversibilityTableVerbs.cpp - the 0.3.0 verb wave's LIVE-checkpoint rows
 *                                      of THE SPEC A16 classification table:
 *                                      clip.trim, clip.slip and
 *                                      note.probability_set.
 *
 * This file is data, like the blocks it belongs to. It exists for the same reason
 * ControlReversibilityTableTrackFolder.cpp does: the three rows are true_inverse
 * rows whose inverse is the engine's own LIVE checkpoint, so by the split's rule
 * they belong in ControlReversibilityTable.cpp - and that file sits at the file
 * ratchet (495 lines, 500 limit), which is not moved for a new feature.
 *
 * The block is joined into the true_inverse half by reversibilityRowTable()
 * (ControlReversibilityTable.cpp), so every caller - ReversibilityTable's
 * constructor, control.transactions and tests/.../ReversibilityContractTest - still
 * reads ONE block with ONE row count, and each row's class still comes from the row
 * itself, never from the file it happens to live in. `render.stems` is NOT here: it
 * is a not_mutating row and lives with the other not_mutating rows in
 * ControlReversibilityTablePassive.cpp, where the split puts it.
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

/*! The three rows, one per verb, each naming the SAME mechanism in its own words
 *  because each depends on a different serialisation fact:
 *
 *  - clip.trim and clip.slip write only attributes SampleClip::saveSettings and
 *    MidiClip::exportToXML write UNCONDITIONALLY ('pos', 'len', 'off', plus
 *    'autoresize') and the matching loadSettings read unconditionally. That is what
 *    makes a checkpoint taken before a FIRST edit reversible - the opposite case is
 *    the authored source window ('srcin'/'srcout'), written only when it is not the
 *    whole buffer and applied only `if (hasAttribute(...))`, so the same checkpoint
 *    could NOT take a first window edit back. Both verbs are deliberately authored
 *    outside the window for exactly that reason (see ControlCommandsClipTrim.cpp).
 *
 *  - note.probability_set writes a note's OPTIONAL 'prob' attribute, written only when
 *    the value is not the default 1 - but Note::loadSettings reads it as
 *    attribute("prob", "1"), an unconditional assignment whose absent-attribute value
 *    IS the default, so the checkpoint restores a first edit back to 1 exactly.
 */
const ReversibilityRow kVerbRows[] = {
	R("clip.trim", RC::TrueInverse, true,
		"the start, the length, the source offset and the auto-resize flag are all part "
		"of the Clip's own serialized state ('pos', 'len', 'off', 'autoresize'), and the "
		"Clip is a JournallingObject",
		"ProjectJournal (Clip checkpoint: SampleClip::saveSettings and "
		"MidiClip::exportToXML write all four attributes unconditionally and the matching "
		"loadSettings read them unconditionally, so the checkpoint taken before a FIRST "
		"trim already carries the pre-edit values and the recorded inverse op is the same "
		"clip.trim with the before-edges)",
		""),
	R("clip.slip", RC::TrueInverse, true,
		"slip writes exactly one attribute of the Clip's own serialized state - 'off', the "
		"source offset - and writes it even when it is 0, so the value the checkpoint "
		"carries is the whole of the state the command can change",
		"ProjectJournal (Clip checkpoint: the same live checkpoint as clip.trim; the "
		"recorded inverse op is clip.slip with the previous offset, and the mechanism does "
		"not depend on a reset-on-absence rule because 'off' is never omitted)",
		""),
	R("note.probability_set", RC::TrueInverse, true,
		"the probability is per-note state on the Note, which the owner MidiClip "
		"serializes as part of its note list",
		"ProjectJournal (MidiClip checkpoint: MidiClip::loadSettings clears and re-loads "
		"the clip's note list and Note::loadSettings reads the optional 'prob' attribute "
		"with a default of 1, so restoring a checkpoint taken before a FIRST probability "
		"edit brings the note back to 'always plays' rather than leaving the edit in place)",
		""),
};

constexpr int kVerbRowCount = static_cast<int>(sizeof(kVerbRows) / sizeof(kVerbRows[0]));

} // namespace

const ReversibilityRow* reversibilityVerbRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kVerbRowCount; }
	return kVerbRows;
}

} // namespace control
} // namespace lmms
