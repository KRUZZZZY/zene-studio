/*
 * ChordDetect.cpp - what chords a clip's notes spell (include/ChordDetect.h)
 *
 * A loop over the note list and a lookup in the pre-existing vocabulary. No
 * Engine, no clip, no GUI, no audio: the arithmetic is testable on its own.
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
 *
 */

#include "ChordDetect.h"

#include <algorithm>

#include "ChordVocabulary.h"

namespace lmms
{

namespace ChordDetect
{

namespace
{

//! The notes in the order a detection walks them: position first, then the
//! LOWEST key first, so a slice's bass is the first note of its group.
bool noteOrder(const Note* a, const Note* b)
{
	if (a->pos().getTicks() != b->pos().getTicks())
	{
		return a->pos().getTicks() < b->pos().getTicks();
	}
	return a->key() < b->key();
}

//! The highest key <= \a bassKey whose pitch class is \a pitchClass.
int rootKeyAtOrBelow(int bassKey, int pitchClass)
{
	int key = bassKey - ((ChordVocabulary::pitchClassOf(bassKey) - pitchClass + 12) % 12);
	if (key < 0) { key += 12; }
	return key;
}

//! One slice of the detection: the notes that start within \a window of the
//! note at \a from, the span it covers, and the pitch classes it sounds.
struct Slice
{
	tick_t pos = 0;
	tick_t length = 0;
	int notes = 0;
	int bass = 0;
	std::vector<int> classes;
};

/*! Collects the slice beginning at \a from and returns the index of the first
 *  note it did NOT take. Split out of detectChords() so one function does not
 *  carry both the grouping and the naming (Gate 4's complexity ratchet). */
std::size_t collectSlice(const std::vector<const Note*>& ordered, std::size_t from,
	tick_t window, Slice* out)
{
	out->pos = ordered[from]->pos().getTicks();
	tick_t end = out->pos;
	out->bass = ordered[from]->key();
	std::size_t next = from;
	while (next < ordered.size() && ordered[next]->pos().getTicks() - out->pos <= window)
	{
		const Note* note = ordered[next];
		++out->notes;
		out->bass = std::min(out->bass, note->key());
		const tick_t noteEnd = note->pos().getTicks()
			+ std::max<tick_t>(1, note->length().getTicks());
		end = std::max(end, noteEnd);
		out->classes.push_back(ChordVocabulary::pitchClassOf(note->key()));
		++next;
	}
	out->length = end - out->pos;
	return next;
}

} // namespace


std::vector<ChordMatch> detectChords(const NoteVector& notes, const DetectOptions& options)
{
	std::vector<const Note*> ordered;
	ordered.reserve(notes.size());
	for (const Note* note : notes)
	{
		if (note != nullptr) { ordered.push_back(note); }
	}
	std::sort(ordered.begin(), ordered.end(), noteOrder);

	const int minClasses = options.minPitchClasses < 1 ? 1 : options.minPitchClasses;
	const tick_t window = options.windowTicks < 0 ? 0 : options.windowTicks;

	std::vector<ChordMatch> matches;
	std::size_t index = 0;
	while (index < ordered.size())
	{
		Slice slice;
		index = collectSlice(ordered, index, window, &slice);

		const std::vector<int> sounding = ChordVocabulary::normalise(slice.classes);
		if (static_cast<int>(sounding.size()) < minClasses) { continue; }

		ChordMatch match;
		match.pos = slice.pos;
		match.length = slice.length;
		match.notes = slice.notes;
		match.bass = slice.bass;
		const ChordVocabulary::Fit fit = ChordVocabulary::bestFit(sounding,
			ChordVocabulary::pitchClassOf(slice.bass));
		if (fit.chord != nullptr)
		{
			match.root = fit.root;
			match.rootKey = rootKeyAtOrBelow(slice.bass, fit.root);
			match.chord = fit.chord->getName();
			match.missing = static_cast<int>(fit.missing.size());
			match.extra = static_cast<int>(fit.extra.size());
			match.exact = fit.exact;
			const ChordVocabulary::Chord* scale = ChordVocabulary::coveringScale(sounding, fit.root);
			if (scale != nullptr) { match.scale = scale->getName(); }
		}
		matches.push_back(match);
	}
	return matches;
}


KeyEstimate detectKey(const NoteVector& notes)
{
	KeyEstimate estimate;
	std::vector<const Note*> ordered;
	ordered.reserve(notes.size());
	for (const Note* note : notes)
	{
		if (note != nullptr) { ordered.push_back(note); }
	}
	if (ordered.empty()) { return estimate; }
	std::sort(ordered.begin(), ordered.end(), noteOrder);

	// The root is the FIRST note's pitch class: a detection has no other
	// evidence for where the key starts, and stating the rule is what makes the
	// answer reproducible rather than a heuristic nobody can re-run.
	const int rootKey = ordered.front()->key();
	const int root = ChordVocabulary::pitchClassOf(rootKey);

	std::vector<int> classes;
	for (const Note* note : ordered) { classes.push_back(ChordVocabulary::pitchClassOf(note->key())); }
	const std::vector<int> sounding = ChordVocabulary::normalise(classes);

	estimate.root = root;
	estimate.rootKey = rootKey;
	estimate.pitchClasses = static_cast<int>(sounding.size());
	if (const ChordVocabulary::Chord* scale = ChordVocabulary::coveringScale(sounding, root))
	{
		estimate.scale = scale->getName();
		estimate.complete = true;
	}
	return estimate;
}

} // namespace ChordDetect

} // namespace lmms
