/*
 * ChordVocabulary.cpp - the derived views over the pre-existing chord and
 *                       scale vocabulary (include/ChordVocabulary.h)
 *
 * Every function here is a loop over InstrumentFunctionNoteStacking::ChordTable
 * ::getInstance().chords(). There is no table of this file's own: the entries,
 * their semitone offsets and their isScale() split are the piano roll's.
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

#include "ChordVocabulary.h"

#include <algorithm>

#include "Note.h" // NumKeys, and the engine's own octave convention

namespace lmms
{

namespace ChordVocabulary
{

namespace
{

//! The one table: the piano roll's, by reference. Never copied, never filtered
//! into a member of this fork's own.
const InstrumentFunctionNoteStacking::ChordTable& table()
{
	return InstrumentFunctionNoteStacking::ChordTable::getInstance();
}

const std::vector<InstrumentFunctionNoteStacking::Chord>& entries()
{
	return table().chords();
}

int foldToClass(int semitone)
{
	return ((semitone % 12) + 12) % 12;
}

} // namespace


QStringList chordNames()
{
	QStringList names;
	for (const auto& entry : entries())
	{
		if (!entry.isScale()) { names.append(entry.getName()); }
	}
	return names;
}


QStringList scaleNames()
{
	QStringList names;
	for (const auto& entry : entries())
	{
		if (entry.isScale()) { names.append(entry.getName()); }
	}
	return names;
}


namespace
{

//! The first entry of \a is_scale's kind called \a name, or nullptr.
const InstrumentFunctionNoteStacking::Chord* namedEntry(const QString& name, bool is_scale)
{
	for (const auto& entry : entries())
	{
		if (entry.isScale() == is_scale && entry.getName() == name) { return &entry; }
	}
	return nullptr;
}

} // namespace


const Chord* chordByName(const QString& name)
{
	return namedEntry(name, false);
}


const Chord* scaleByName(const QString& name)
{
	return namedEntry(name, true);
}


std::vector<int> normalise(const std::vector<int>& pitchClasses)
{
	std::vector<int> out;
	for (const int value : pitchClasses)
	{
		const int folded = foldToClass(value);
		if (std::find(out.begin(), out.end(), folded) == out.end()) { out.push_back(folded); }
	}
	std::sort(out.begin(), out.end());
	return out;
}


std::vector<int> toneOffsets(const Chord& chord)
{
	std::vector<int> offsets;
	for (int tone = 0; tone < chord.size(); ++tone)
	{
		offsets.push_back(chord[tone]);
	}
	return offsets;
}


std::vector<int> relativePitchClasses(const Chord& chord)
{
	std::vector<int> classes;
	for (const int offset : toneOffsets(chord))
	{
		classes.push_back(foldToClass(offset));
	}
	return normalise(classes);
}


std::vector<int> pitchClassesOf(const Chord& chord, int root)
{
	std::vector<int> classes;
	const int base = foldToClass(root);
	for (const int relative : relativePitchClasses(chord))
	{
		classes.push_back(foldToClass(base + relative));
	}
	return normalise(classes);
}


namespace
{

//! The roots a fit is tried at, in order: every class that is SOUNDING first (a
//! chord's root is a tone of it), the bass of those first of all, then the
//! remaining classes - so a slice whose bass is not its root still names the
//! chord it spells.
std::vector<int> candidateRoots(const std::vector<int>& wanted, int preferredRoot)
{
	std::vector<int> roots = wanted;
	for (int pc = 0; pc < 12; ++pc)
	{
		if (std::find(roots.begin(), roots.end(), pc) == roots.end()) { roots.push_back(pc); }
	}
	const std::vector<int>::iterator bass =
		std::find(roots.begin(), roots.end(), foldToClass(preferredRoot));
	if (bass != roots.end()) { std::rotate(roots.begin(), bass, bass + 1); }
	return roots;
}

//! How far one entry at one root is from a sounding set, with both sides of the
//! difference listed: \a missing (chord tones the slice does not sound) and
//! \a extra (slice tones the chord does not contain).
Fit scoreEntry(const Chord& entry, int root, const std::vector<int>& wanted,
	std::vector<int>* missing, std::vector<int>* extra)
{
	const std::vector<int> chordClasses = pitchClassesOf(entry, root);
	Fit score;
	for (const int pc : chordClasses)
	{
		if (std::find(wanted.begin(), wanted.end(), pc) == wanted.end()) { missing->push_back(pc); }
	}
	for (const int pc : wanted)
	{
		if (std::find(chordClasses.begin(), chordClasses.end(), pc) == chordClasses.end())
		{
			extra->push_back(pc);
		}
	}
	score.missing = *missing;
	score.extra = *extra;
	return score;
}

//! Whether (entry, root) replaces the incumbent: the FEWEST missing tones first
//! and the fewest extra tones second, ties keeping the incumbent - which is what
//! makes a name reproducible (the roots and the entries are tried in a fixed
//! order, so the first of two equal candidates always wins).
bool beatsIncumbent(const Chord& entry, int root, const std::vector<int>& wanted,
	bool haveIncumbent, const Fit& incumbent, Fit* out)
{
	Fit candidate = scoreEntry(entry, root, wanted, &out->missing, &out->extra);
	out->chord = &entry;
	out->root = root;
	out->exact = candidate.missing.empty() && candidate.extra.empty();
	return !haveIncumbent || candidate.missing.size() < incumbent.missing.size()
		|| (candidate.missing.size() == incumbent.missing.size()
			&& candidate.extra.size() < incumbent.extra.size());
}

} // namespace


QString exactChordName(const std::vector<int>& pitchClasses, int root)
{
	const std::vector<int> wanted = normalise(pitchClasses);
	// One tone is not a chord, and the table's "octave" entry is exactly that
	// shape - so the floor here is two, the rule the header states.
	if (wanted.size() < 2) { return QString(); }

	for (const auto& entry : entries())
	{
		if (entry.isScale() || entry.size() < 2) { continue; }
		if (pitchClassesOf(entry, root) == wanted) { return entry.getName(); }
	}
	return QString();
}


Fit bestFit(const std::vector<int>& pitchClasses, int preferredRoot)
{
	const std::vector<int> wanted = normalise(pitchClasses);
	Fit best;
	if (wanted.empty()) { return best; }

	bool haveBest = false;
	Fit incumbent;
	for (const int root : candidateRoots(wanted, preferredRoot))
	{
		for (const auto& entry : entries())
		{
			if (entry.isScale() || entry.size() < 2) { continue; }
			Fit candidate;
			if (!beatsIncumbent(entry, root, wanted, haveBest, incumbent, &candidate)) { continue; }
			haveBest = true;
			incumbent = candidate;
			best = candidate;
		}
	}
	return best;
}


const Chord* coveringScale(const std::vector<int>& pitchClasses, int root)
{
	const std::vector<int> wanted = normalise(pitchClasses);
	if (wanted.empty()) { return nullptr; }

	const Chord* best = nullptr;
	int bestSize = 0;
	for (const auto& entry : entries())
	{
		if (!entry.isScale()) { continue; }
		const std::vector<int> classes = pitchClassesOf(entry, root);
		bool covers = true;
		for (const int pc : wanted)
		{
			if (std::find(classes.begin(), classes.end(), pc) == classes.end())
			{
				covers = false;
				break;
			}
		}
		if (!covers) { continue; }
		if (best == nullptr || entry.size() < bestSize)
		{
			best = &entry;
			bestSize = entry.size();
		}
	}
	return best;
}


int keyOf(int pitchClass, int octave)
{
	// Note.h's own convention: DefaultMiddleKey == Octave::Octave_4 + Key::C,
	// and the Octave enum's ordinal runs from Octave_m1 (0), so the multiplier
	// is (octave + 1) and keyOf(0, 4) is 60.
	const int key = 12 * (octave + 1) + foldToClass(pitchClass);
	if (key < 0) { return 0; }
	if (key > NumKeys - 1) { return NumKeys - 1; }
	return key;
}

} // namespace ChordVocabulary

} // namespace lmms
