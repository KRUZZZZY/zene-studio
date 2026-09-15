/*
 * ControlReversibilityTableChord.cpp - the `chord.*` command group's rows of
 *                                       THE SPEC A16 classification table
 *                                       (feature row 35).
 *
 * Nine rows, and BOTH classes are represented inside this one file on purpose:
 * the three reads are not_mutating and the six writers are true_inverse, and a
 * group file may carry whatever classes its commands have - the class comes
 * from each ROW, never from the file it happens to live in (the routing,
 * folder, vca and scan/crash group files are the precedent). Joined into
 * reversibilityRowTable() through reversibilityChordRowTable() so the contract
 * is still read, and tested, as ONE table with ONE row count.
 *
 * WHY THE WRITERS SPLIT INTO TWO MECHANISMS, stated once here because it is the
 * only thing about these six rows that is not obvious:
 *
 *   - chord.set / chord.remove / chord.clear / chord.detect_to_track edit the
 *     CHORD TRACK, which is project state the Song's journal checkpoint does
 *     not carry (it is not in the track container and is not a
 *     JournallingObject - the finding docs/GROOVE-POOL.md and
 *     docs/TEMPO-MAP.md record for the pool and the map). Their inverse is a
 *     recorded ACTION checkpoint holding the <chord-track> element as it was.
 *   - chord.track_write / chord.progression_generate write NOTES into a
 *     MidiClip, which IS a JournallingObject whose serialized state is its note
 *     list, so their inverse is the clip's own live checkpoint - the mechanism
 *     note.add, groove.apply and groove.quantize reverse with.
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

//! A row whose gesture COALESCES on one argument (the track's own position).
#define RCO(id, cls, rev, reason, mechanism, fallback, coalesce) \
	{ id, cls, reason, mechanism, fallback, rev, coalesce }

const ReversibilityRow kChordRows[] = {
	// ---- the three reads: nothing is written ----
	R("chord.get_state", RC::NotMutating, false,
		"reads the project's chord track - its events and its bounds - and computes "
		"nothing; no write",
		"no write",
		""),
	R("chord.detect", RC::NotMutating, false,
		"reads a clip's notes and names the chords they spell against the pre-existing "
		"vocabulary. The note pointers are read and never written, and no track, clip or "
		"model is touched; the write half is a SEPARATE command "
		"(chord.detect_to_track)",
		"no write",
		""),
	R("chord.progression_list", RC::NotMutating, false,
		"reads the catalogue and the vocabularies the group draws on (ChordTable's chords "
		"and scales, the pattern names and the bounds); no write",
		"no write",
		""),

	// ---- the four TRACK edits: the recorded ACTION is the inverse ----
	RCO("chord.set", RC::TrueInverse, true,
		"a chord track is project state the Song's journal checkpoint does not carry: it is "
		"not an element inside <trackcontainer> and it is not a JournallingObject, so no live "
		"checkpoint restores it (the finding docs/GROOVE-POOL.md records for the pool)",
		"action checkpoint: the <chord-track> element is captured BEFORE the write "
		"(ChordTrack::toXml) and the recorded step writes it back through "
		"ChordTrack::loadSettings - the project loader's own path - as ONE step. The "
		"recorded inverse op names chord.set with the previous event's own arguments at that "
		"position, or chord.remove when the position held nothing, so a reader can re-issue "
		"it by hand",
		"",
		"pos"),
	R("chord.remove", RC::TrueInverse, true,
		"a removed chord has no live object behind it: the event is a value in the track's "
		"own vector, and the track is not journalled",
		"action checkpoint: the whole <chord-track> element is captured before the removal "
		"and the recorded step writes it back, ONE step for the one command. The recorded "
		"inverse op names chord.set with the removed event's own arguments, which is what "
		"puts it back",
		""),
	R("chord.clear", RC::TrueInverse, true,
		"destroys every event of the track, which is not journalled - the empty track is "
		"reached through this command's own recorded step, never by re-running it",
		"action checkpoint: the captured <chord-track> element is written back by the "
		"recorded step, restoring every chord with its position, root, octave, length, name "
		"and key. The bound is the track's own MaxEvents (64 events of bounded integers), so "
		"the capture always fits the SPEC A16 record cap",
		""),
	R("chord.detect_to_track", RC::TrueInverse, true,
		"writes the detection onto the track and, by default, REPLACES what was there - so "
		"the previous chords are destroyed and the track is not journalled",
		"action checkpoint: the track's <chord-track> element is captured before the write; when "
		"NOTHING "
		"could be written the track is put back from that capture and the call is refused "
		"BEFORE any step is recorded, so a refused call leaves no undo entry behind",
		""),

	// ---- the two generators: the CLIP's own checkpoint is the inverse ----
	R("chord.track_write", RC::TrueInverse, true,
		"it writes notes into a MidiClip and, by default, first clears the notes the clip "
		"already had",
		"clip checkpoint: a MidiClip is a JournallingObject whose serialized state IS its "
		"note list, so the checkpoint taken before the write restores every note this "
		"command added and every note it cleared - the mechanism note.add and note.remove "
		"reverse with (SPEC A16 class true_inverse)",
		""),
	R("chord.progression_generate", RC::TrueInverse, true,
		"it generates notes into a MidiClip and, by default, first clears the notes the clip "
		"already had. The generated TAKE is not un-generated by generating it again (a "
		"seeded run on a moved clip is a different run), so the inverse is the clip's own "
		"state and not a second generation",
		"clip checkpoint: the checkpoint taken before the write restores the clip's note "
		"list exactly - positions, lengths, keys and velocities, generated notes and cleared "
		"notes alike",
		""),
};

#undef R
#undef RCO

constexpr int kChordRowCount =
	static_cast<int>(sizeof(kChordRows) / sizeof(kChordRows[0]));

} // namespace

const ReversibilityRow* reversibilityChordRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kChordRowCount; }
	return kChordRows;
}

} // namespace control
} // namespace lmms
