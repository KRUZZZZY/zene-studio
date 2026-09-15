/*
 * ChordVocabulary.h - a read-only VIEW over the chord and scale vocabulary
 *                     this product already has.
 *
 * THERE IS NO SECOND SCALE OR CHORD TABLE HERE, deliberately, and that is the
 * whole point of this file. The names, the intervals and the isScale() split
 * are InstrumentFunctionNoteStacking::ChordTable's - the SAME 95 entries the
 * piano roll's chord and scale selectors read (src/gui/editors/PianoRoll.cpp
 * uses ChordTable::getInstance() for both, and the scale entries are the ones
 * with more than six tones: Chord::isScale() is `size() > 6`). Everything in
 * this file is DERIVED from that table at call time - there is no pitch-class
 * array, no interval list and no name table of this fork's own anywhere below,
 * so a chord this engine names is a chord the piano roll would name the same
 * way, and adding an entry to the table adds it here.
 *
 * WHAT IT ADDS ON TOP. The table answers "give me the entry called X" and
 * "what are this entry's semitone offsets". A chord TRACK, a DETECTOR and a
 * PROGRESSION generator need three more questions answered over the same data:
 *
 *   - what pitch classes does entry E sound with its root at pitch class R;
 *   - which entry fits EXACTLY a set of sounding pitch classes (naming a
 *     detected chord);
 *   - which entry fits BEST when nothing fits exactly, and which scale entry
 *     covers a set of pitch classes (the key a chord run implies).
 *
 * All three are pure functions of the table, which is what makes detection
 * reproducible: the same note set always names the same entry, ties included.
 *
 * BOUNDS, stated: an exact fit is required for a name (a partial fit is
 * reported with its missing and extra tones rather than rounded to a name); a
 * single tone is not a chord (entry "octave" has one tone and is skipped by
 * the detector); and a name tie between two entries with the same tone set
 * resolves to the table's own order, never to iteration order.
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

#ifndef LMMS_CHORD_VOCABULARY_H
#define LMMS_CHORD_VOCABULARY_H

#include <vector>

#include <QString>
#include <QStringList>

#include "InstrumentFunctions.h" // the pre-existing chord + scale vocabulary
#include "lmms_export.h"

namespace lmms
{

namespace ChordVocabulary
{

//! The table's own entry type; this fork contributes no other.
using Chord = InstrumentFunctionNoteStacking::Chord;

// ---------------------------------------------------------------------------
// The table, filtered
// ---------------------------------------------------------------------------

/*! The names of the CHORD entries (isScale() == false), in the table's own
 *  order - the vocabulary a chord track event and a detected chord name are
 *  drawn from. */
LMMS_EXPORT QStringList chordNames();
//! The names of the SCALE entries (isScale() == true), in the table's order -
//! the key vocabulary the piano roll's scale selector reads.
LMMS_EXPORT QStringList scaleNames();

//! The chord entry called \a name (isScale() == false), or nullptr.
LMMS_EXPORT const Chord* chordByName(const QString& name);
//! The SCALE entry called \a name (isScale() == true), or nullptr.
LMMS_EXPORT const Chord* scaleByName(const QString& name);

// ---------------------------------------------------------------------------
// Sets of pitch classes
// ---------------------------------------------------------------------------

/*! The pitch classes (0..11) \a chord sounds with its root at \a root, unique
 *  and ascending. A tone above the octave (14, 17, 21) folds into its class,
 *  because a chord's identity is a set of classes and not a voicing. */
LMMS_EXPORT std::vector<int> pitchClassesOf(const Chord& chord, int root);

//! The chord's own tones as pitch classes relative to its root, unique, ascending.
LMMS_EXPORT std::vector<int> relativePitchClasses(const Chord& chord);

/*! The chord's own semitone offsets above its root, IN THE TABLE'S OWN ORDER
 *  and NOT folded into one octave: a tone above the octave stays above it, so
 *  a caller that builds sounding keys from them keeps the chord's register
 *  (the "9" entry's 14 is a ninth above the root, not a second). */
LMMS_EXPORT std::vector<int> toneOffsets(const Chord& chord);

//! \a pitchClasses with the range folded in, deduplicated and sorted.
LMMS_EXPORT std::vector<int> normalise(const std::vector<int>& pitchClasses);

/*! The name of the chord entry whose tone set is EXACTLY \a pitchClasses at
 *  \a root, or an empty string when nothing fits exactly (including when the
 *  set holds fewer than two distinct classes: one tone is not a chord).
 *  Entries with a single tone are skipped, and ties resolve to the table order -
 *  both rules are what make the returned name reproducible. */
LMMS_EXPORT QString exactChordName(const std::vector<int>& pitchClasses, int root);

/*! The closest chord entry to a set of sounding pitch classes.
 *
 *  \a preferredRoot is a tie-break only (the detector passes the slice's bass):
 *  fit is judged by the tones the chord MISSES first and the tones it ADDS
 *  second, so a C-E-G slice names the triad rather than a four-tone entry that
 *  contains it. `exact` is true only when both lists are empty. */
struct Fit
{
	const Chord* chord = nullptr;
	int root = -1;
	std::vector<int> missing; //!< chord tones the slice does not sound
	std::vector<int> extra;   //!< slice tones the chord does not contain
	bool exact = false;
};

LMMS_EXPORT Fit bestFit(const std::vector<int>& pitchClasses, int preferredRoot);

/*! The most specific SCALE entry rooted at \a root that contains every pitch
 *  class in \a pitchClasses: the fewest tones wins, so the key reported for a
 *  seven-note run is "Major" and not "Chromatic". nullptr when no scale entry
 *  rooted there covers the set (the chromatic entry always does, so this
 *  answers a real key whenever the vocabulary can name one). */
LMMS_EXPORT const Chord* coveringScale(const std::vector<int>& pitchClasses, int root);

// ---------------------------------------------------------------------------
// Keys and octaves
// ---------------------------------------------------------------------------

//! Octaves the vocabulary addresses; the engine's own (Note.h): C-1 .. G9.
constexpr int MinOctave = -1;
constexpr int MaxOctave = 9;

/*! The absolute MIDI key of \a pitchClass in \a octave, the engine's own
 *  convention (Note.h: DefaultMiddleKey is Octave_4 + Key::C == 60), so
 *  keyOf(0, 4) is middle C. Clamped to 0..127: octave 9 cannot hold every
 *  pitch class, and a key outside MIDI range is not a note. */
LMMS_EXPORT int keyOf(int pitchClass, int octave);

//! The pitch class (0..11) of a MIDI key.
inline int pitchClassOf(int key) { return ((key % 12) + 12) % 12; }

} // namespace ChordVocabulary

} // namespace lmms

#endif // LMMS_CHORD_VOCABULARY_H
