/*
 * ChordDetect.h - what chords a clip's notes spell
 *
 * WHAT IT IS. A pure function over a note list: the notes are grouped into
 * SLICES (notes that sound together), each slice's sounding pitch classes are
 * looked up in the pre-existing vocabulary (include/ChordVocabulary.h, which is
 * a view over ChordTable - this file adds no table), and the nearest entry is
 * reported with the tones it misses and the tones it adds. Nothing here touches
 * the Engine, a track or a clip: it is a loop over a std::vector<Note*>, which
 * is what makes a detection reproducible and testable headlessly.
 *
 * WHAT IT DOES NOT DO, stated so nothing is overclaimed: it detects CHORDS
 * from notes that sound together, not a chord track, not a key change over
 * time, and not inversions by name (an inversion is reported as the positions
 * of the notes, not as a "/G" suffix). It does not guess a tuning, it does not
 * read audio, and it does not invent a name: when nothing fits EXACTLY the
 * reported chord is the closest entry with `exact: false` and the caller can
 * see how far off it is.
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

#ifndef LMMS_CHORD_DETECT_H
#define LMMS_CHORD_DETECT_H

#include <vector>

#include <QString>

#include "LmmsTypes.h" // tick_t
#include "Note.h"      // NoteVector

namespace lmms
{

namespace ChordDetect
{

/*! How the notes are grouped into slices.
 *
 *  `windowTicks` is how far after a slice's FIRST note a later note may start
 *  and still belong to it: 0 (the default) means "the same tick", which is what
 *  a chord written in a piano roll is. A strummed or humanised performance
 *  needs a window, and it is measured from the slice's first note rather than
 *  from the previous one, so a run of sixteenths cannot chain into one enormous
 *  slice. */
struct DetectOptions
{
	tick_t windowTicks = 0;
	//! A slice with fewer distinct pitch classes is not a chord and is not
	//! reported (the default skips single notes).
	int minPitchClasses = 2;
};

//! One detected slice.
struct ChordMatch
{
	tick_t pos = 0;        //!< where the slice starts
	tick_t length = 0;     //!< the slice's span (its last note's end minus pos)
	int notes = 0;         //!< how many notes the slice holds
	int bass = -1;         //!< the lowest sounding key, or -1
	int root = -1;         //!< the chord's root as a pitch class, or -1
	int rootKey = -1;      //!< the root's absolute MIDI key, or -1
	QString chord;         //!< the vocabulary's name; empty when nothing fits
	QString scale;         //!< the covering scale entry rooted at `root`; empty
	                       //!< when the vocabulary cannot name one
	int missing = 0;       //!< chord tones the slice does not sound
	int extra = 0;         //!< slice tones the chord does not contain
	bool exact = false;    //!< missing == 0 && extra == 0
};

/*! Every chord the notes spell, in ascending position order.
 *
 *  Pure: the note pointers are read and never written, and the result depends
 *  only on the notes and the options. */
LMMS_EXPORT std::vector<ChordMatch> detectChords(const NoteVector& notes,
	const DetectOptions& options = DetectOptions());

//! The key the whole note list implies.
struct KeyEstimate
{
	QString scale;        //!< a SCALE entry's name, empty when none covers the notes
	int root = -1;        //!< the key's pitch class
	int rootKey = -1;     //!< the key's absolute MIDI key
	int pitchClasses = 0; //!< how many distinct classes the notes use
	//! True when a scale entry covers every class the notes use. The chromatic
	//! entry covers everything, so a false here means the note list is empty.
	bool complete = false;
};

/*! The key of a note list: the ROOT is the first note's pitch class (lowest
 *  position, then lowest key) and the scale is the most specific vocabulary
 *  entry rooted there that covers every class in the list - so a run of seven
 *  notes in C major reports "Major", not "Chromatic". */
LMMS_EXPORT KeyEstimate detectKey(const NoteVector& notes);

} // namespace ChordDetect

} // namespace lmms

#endif // LMMS_CHORD_DETECT_H
