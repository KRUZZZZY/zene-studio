/*
 * ChordProgression.h - named progressions, and the SEEDED generator that
 *                      writes one into a clip as notes
 *
 * WHAT IT IS. A progression is a named walk over the SCALE degrees of the
 * pre-existing vocabulary - "I-V-vi-IV" is the degrees {0, 4, 5, 3} of whatever
 * scale entry the request names. Each chord of the walk is built the way music
 * theory builds it: stack the scale's own tones in thirds on the degree (the
 * degree, +2, +4, wrapping the octave), and NAME the result by asking
 * ChordVocabulary for the entry whose tone set that stack is. So the names this
 * generator writes are the vocabulary's names and a scale whose stack has no
 * name gets an empty name rather than a new table of this fork's own.
 *
 * THE SEED. Generating notes has CHOICES in it (how each chord is voiced, how
 * far a chord is nudged off the grid, how much its velocity moves), and every
 * one of them is drawn from `seed` with NoteRandom::rollUnit - the seeded,
 * stateless hash this product already uses for MIDI depth and for the
 * quantise's humanise amount. The draw is a PURE FUNCTION of the seed and the
 * chord's own identity (its degree, its root, the step length), never of a
 * hidden random state, so:
 *
 *   - the same request with the same seed reproduces the same take, note for
 *     note (busy-ness included), and a test can assert it;
 *   - the same request with a different seed gives a different take, because
 *     every drawn choice has more than one outcome;
 *   - `variation` 0 means NO choice is drawn at all: the take is the
 *     progression itself, exactly on the grid, and the seed decides nothing.
 *     That is deliberate - a generator that silently moved a user's chords
 *     would be the surprise, so the variation is opt-in and stated.
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

#ifndef LMMS_CHORD_PROGRESSION_H
#define LMMS_CHORD_PROGRESSION_H

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include "ChordTrack.h" // ChordEvent is what a progression's steps become
#include "LmmsTypes.h"  // tick_t
#include "TimePos.h"    // DefaultTicksPerBar
#include "volume.h"     // MinVolume / MaxVolume / DefaultVolume
#include "lmms_export.h"

namespace lmms
{

namespace ChordProgression
{

//! How a chord's tones are laid out in time.
enum class Pattern
{
	Block,        //!< every tone at once, held for the step
	ArpeggioUp,   //!< the tones in ascending order, up the chord
	ArpeggioDown, //!< the tones in descending order, down the chord
	Broken        //!< the root held, the upper tones arpeggiated above it
};

//! How the progression is laid into the clip. Every field has a default, so a
//! caller names only what it means.
struct Request
{
	QString progression = QStringLiteral("I-V-vi-IV");
	QString scale = QStringLiteral("Major");
	int root = 0;                              //!< key pitch class (C = 0)
	int octave = 4;                            //!< the octave the key sounds in
	tick_t start = 0;                          //!< where the walk starts
	tick_t stepTicks = DefaultTicksPerBar;     //!< one chord per step
	int steps = 4;                             //!< how many chords to write
	Pattern pattern = Pattern::Block;
	int velocity = DefaultVolume;              //!< the base velocity (0..200)
	uint32_t seed = 0;                         //!< the draws' seed
	//! 0 (the default) draws nothing: the take is the progression itself.
	//! Above 0, each chord's voicing, its position and its velocity are drawn.
	float variation = 0.0f;
};

//! One chord of a progression, as it was built.
struct ChordStep
{
	int degree = 0;        //!< the scale degree it was built on
	int root = 0;          //!< its root as a pitch class
	//! The root's absolute MIDI key, which is also the chord's lowest tone.
	int rootKey = 0;
	QString name;          //!< the vocabulary's name for it; empty when no entry fits
	//! The chord's tones as absolute MIDI keys, ascending, root position - the
	//! engine's own form, which the generator then voices and lays out. Not part
	//! of any wire result.
	std::vector<int> keys;
};

//! One note the generator produces. The caller writes these into a clip.
struct NoteSpec
{
	tick_t pos = 0;
	tick_t length = 0;
	int key = 0;
	int velocity = DefaultVolume;
};

// ---------------------------------------------------------------------------
// The catalogue
// ---------------------------------------------------------------------------

//! One named progression: a walk over scale degrees.
struct Entry
{
	QString name;
	std::vector<int> degrees;
};

//! The most chords one call may write, and the most degrees one progression
//! may walk. Both are bounds the command reports rather than implies.
constexpr int MaxSteps = 64;
constexpr int MaxDegreeCount = 8;

//! Every named progression, in the catalogue's own order.
LMMS_EXPORT const std::vector<Entry>& catalogue();
//! The progression called \a name, or nullptr. The name IS the key.
LMMS_EXPORT const Entry* find(const QString& name);
//! The catalogue's names, in order - what chord.progression_list reports.
LMMS_EXPORT QStringList progressionNames();

//! The pattern names as the wire spells them ("block", "arpeggio_up", ...).
LMMS_EXPORT QStringList patternNames();
//! Parses a pattern name; false leaves \a out untouched.
LMMS_EXPORT bool patternFromName(const QString& name, Pattern* out);
//! The wire name of \a pattern.
LMMS_EXPORT QString patternName(Pattern pattern);

// ---------------------------------------------------------------------------
// The two halves
// ---------------------------------------------------------------------------

/*! The chords \a request names, in order, without laying them out in time.
 *
 *  Refuses (false, with \a error filled) when the progression or the scale is
 *  not in the vocabulary, or when a bound is outside its range. A degree whose
 *  stack has no name in the vocabulary is NOT a refusal: it comes back with an
 *  empty `name`, because the notes it implies are still the scale's own. */
LMMS_EXPORT bool chordsFor(const Request& request, std::vector<ChordStep>* out, QString* error);

/*! The notes \a request asks for, sorted by (position, key).
 *
 *  Deterministic: two calls with the same request produce identical vectors,
 *  seeds included. The seeded choices above are the only source of variety. */
LMMS_EXPORT bool generateNotes(const Request& request, std::vector<NoteSpec>* out, QString* error);

/*! Lays ONE chord's tones out in time under \a pattern.
 *
 *  \a pos is where the chord starts and \a room is how many ticks it may
 *  occupy: every note starts inside that window and no note runs past it, so a
 *  chord can never overlap the next one. Shared by the progression walk and by
 *  the chord track's own write verb, so the two cannot lay a chord out
 *  differently - the pattern is defined once. */
LMMS_EXPORT void layOutChord(const std::vector<int>& keys, tick_t pos, tick_t room,
	Pattern pattern, int velocity, std::vector<NoteSpec>* out);

/*! The same progression as chord-track EVENTS, ready for ChordTrack::set.
 *
 *  \a request's `start`, `stepTicks`, `steps` and `scale` are what it uses; the
 *  chord names are the ones chordsFor() produced. A step with no name is
 *  refused rather than written as a nameless event, because a chord track
 *  event's name IS its content (include/ChordTrack.h). */
LMMS_EXPORT bool trackEventsFor(const Request& request, std::vector<ChordEvent>* out, QString* error);

} // namespace ChordProgression

} // namespace lmms

#endif // LMMS_CHORD_PROGRESSION_H
