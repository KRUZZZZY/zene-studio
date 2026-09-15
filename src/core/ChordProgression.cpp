/*
 * ChordProgression.cpp - the progression catalogue and the seeded generator
 *                        (include/ChordProgression.h)
 *
 * Every name here is either the catalogue's own label for a degree walk or a
 * name the pre-existing vocabulary already has. The only numbers this file
 * chooses are the catalogue's degree lists, which ARE the progressions.
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

#include "ChordProgression.h"

#include <algorithm>
#include <cmath>

#include "ChordVocabulary.h"
#include "Note.h"       // NumKeys
#include "NoteRandom.h" // rollUnit - the product's seeded hash

namespace lmms
{

namespace ChordProgression
{

namespace
{

/*! The three draw streams of one chord. Distinct salts keep them independent
 *  of each other at the same seed - NoteRandom's own convention for the two
 *  streams of a note's probability and velocity jitter. */
constexpr uint32_t kSaltVoicing = 11;
constexpr uint32_t kSaltPosition = 12;
constexpr uint32_t kSaltVelocity = 13;

//! How far a humanised chord may move, as a fraction of the step: an eighth of
//! a step, which is a feel and not a re-arrangement.
constexpr float kMaxPositionJitterFraction = 0.125f;
//! How far a humanised chord's velocity may move, in the engine's own volume
//! units (0..200).
constexpr float kMaxVelocityJitter = 20.0f;

int clampKey(int key)
{
	if (key < 0) { return 0; }
	if (key > NumKeys - 1) { return NumKeys - 1; }
	return key;
}

int clampVelocity(int velocity)
{
	if (velocity < MinVolume) { return MinVolume; }
	if (velocity > MaxVolume) { return MaxVolume; }
	return velocity;
}

/*! The tones of the chord on \a degree, as semitone offsets above the SCALE's
 *  own root: the degree, and the two scale tones two and four steps above it,
 *  wrapping the octave exactly once each. This is the triad the scale itself
 *  spells - the definition of "the chord on this degree", not a table. */
std::vector<int> stackedTones(const ChordVocabulary::Chord& scale, int degree)
{
	const int size = scale.size();
	std::vector<int> tones;
	for (int step = 0; step < 3; ++step)
	{
		const int index = degree + 2 * step;
		tones.push_back(scale[index % size] + 12 * (index / size));
	}
	return tones;
}

/*! The SEEDED draws of one chord: its voicing (an inversion), its position nudge
 *  and its velocity. Every one of them is a pure function of the seed and THIS
 *  chord's own identity - its degree, its root, the step it occupies - so a take
 *  is reproducible from the request alone (NoteRandom's rule). With `variation`
 *  0 nothing is drawn at all and the seed decides nothing. Split out of
 *  generateNotes() so one function does not carry the walk, the draws and the
 *  layout (Gate 4's complexity ratchet). */
void drawVariation(const Request& request, const ChordStep& step, int* inversion,
	tick_t* positionJitter, int* velocity)
{
	*inversion = 0;
	*positionJitter = 0;
	if (request.variation <= 0.0f) { return; }

	const int identity = static_cast<int>(request.stepTicks);
	const int maxInversion = std::min(2, static_cast<int>(step.keys.size()) - 1);
	if (maxInversion > 0)
	{
		const float draw = NoteRandom::rollUnit(request.seed, step.degree, step.root, identity,
			kSaltVoicing);
		*inversion = std::min(maxInversion, static_cast<int>(draw * (maxInversion + 1)));
	}
	const float positionDraw = NoteRandom::rollUnit(request.seed, step.degree, step.root, identity,
		kSaltPosition);
	*positionJitter = static_cast<tick_t>(std::lround(
		request.variation * kMaxPositionJitterFraction * static_cast<float>(request.stepTicks)
		* (2.0f * positionDraw - 1.0f)));
	const float velocityDraw = NoteRandom::rollUnit(request.seed, step.degree, step.root, identity,
		kSaltVelocity);
	*velocity = clampVelocity(*velocity + static_cast<int>(std::lround(
		request.variation * kMaxVelocityJitter * (2.0f * velocityDraw - 1.0f))));
}

//! The chord's tones voiced: an inversion lifts the lowest tones an octave, the
//! result is kept ASCENDING so "arpeggio up" is really up, and a tone past the
//! MIDI range is dropped rather than piled onto the top key.
std::vector<int> voicedKeys(const std::vector<int>& keys, int inversion)
{
	std::vector<int> voices = keys;
	for (int voice = 0; voice < inversion && voice < static_cast<int>(voices.size()); ++voice)
	{
		voices[static_cast<std::size_t>(voice)] += 12;
	}
	std::sort(voices.begin(), voices.end());
	voices.erase(std::remove_if(voices.begin(), voices.end(),
		[](int key) { return key > NumKeys - 1; }), voices.end());
	return voices;
}

//! One note, clamped into the room the chord has from \a pos.
void appendNote(std::vector<NoteSpec>* out, int key, tick_t at, tick_t length, tick_t pos,
	tick_t span, int velocity)
{
	const tick_t startPos = at < 0 ? 0 : at;
	const tick_t space = span - (startPos - pos);
	if (length > space) { length = space; }
	if (length < 1) { length = 1; }
	NoteSpec spec;
	spec.pos = startPos;
	spec.length = length;
	spec.key = clampKey(key);
	spec.velocity = clampVelocity(velocity);
	out->push_back(spec);
}

//! The tones in one slot each: up the chord, or down it.
void layOutArpeggio(const std::vector<int>& keys, tick_t pos, tick_t span, bool down,
	int velocity, std::vector<NoteSpec>* out)
{
	const int voiceCount = static_cast<int>(keys.size());
	const tick_t slot = std::max<tick_t>(1, span / voiceCount);
	for (int voice = 0; voice < voiceCount; ++voice)
	{
		const int order = down ? voiceCount - 1 - voice : voice;
		appendNote(out, keys[static_cast<std::size_t>(order)],
			pos + static_cast<tick_t>(voice) * slot, slot, pos, span, velocity);
	}
}

} // namespace


const std::vector<Entry>& catalogue()
{
	// The catalogue: one entry per named walk. The degrees are 0-based indices
	// into the scale's own interval list, so "I-V-vi-IV" in Major is
	// {0, 4, 5, 3} - and the SAME entry over Aeolian is the minor walk, because
	// the chord on each degree is built from that scale's own tones.
	static const std::vector<Entry> entries = {
		{ QStringLiteral("I-V-vi-IV"), { 0, 4, 5, 3 } },
		{ QStringLiteral("I-vi-IV-V"), { 0, 5, 3, 4 } },
		{ QStringLiteral("vi-IV-I-V"), { 5, 3, 0, 4 } },
		{ QStringLiteral("ii-V-I"), { 1, 4, 0 } },
		{ QStringLiteral("I-vi-ii-V"), { 0, 5, 1, 4 } },
		{ QStringLiteral("I-IV-V-I"), { 0, 3, 4, 0 } },
		{ QStringLiteral("i-VI-III-VII"), { 0, 5, 2, 6 } },
		{ QStringLiteral("i-iv-v-i"), { 0, 3, 4, 0 } },
	};
	return entries;
}


const Entry* find(const QString& name)
{
	for (const Entry& entry : catalogue())
	{
		if (entry.name == name) { return &entry; }
	}
	return nullptr;
}


QStringList progressionNames()
{
	QStringList names;
	for (const Entry& entry : catalogue()) { names.append(entry.name); }
	return names;
}


QStringList patternNames()
{
	return { QStringLiteral("block"), QStringLiteral("arpeggio_up"),
		QStringLiteral("arpeggio_down"), QStringLiteral("broken") };
}


bool patternFromName(const QString& name, Pattern* out)
{
	const QString wanted = name.trimmed().toLower();
	if (wanted == QLatin1String("block")) { *out = Pattern::Block; return true; }
	if (wanted == QLatin1String("arpeggio_up")) { *out = Pattern::ArpeggioUp; return true; }
	if (wanted == QLatin1String("arpeggio_down")) { *out = Pattern::ArpeggioDown; return true; }
	if (wanted == QLatin1String("broken")) { *out = Pattern::Broken; return true; }
	return false;
}


QString patternName(Pattern pattern)
{
	switch (pattern)
	{
	case Pattern::Block: return QStringLiteral("block");
	case Pattern::ArpeggioUp: return QStringLiteral("arpeggio_up");
	case Pattern::ArpeggioDown: return QStringLiteral("arpeggio_down");
	case Pattern::Broken: return QStringLiteral("broken");
	}
	return QStringLiteral("block");
}


namespace
{

/*! The BOUNDS half of a request: every range refusal with its own message, so
 *  chordsFor() carries the walk and not the gauntlet (Gate 4's complexity
 *  ratchet). The names are proved by the caller, which needs the resolved
 *  scale and entry anyway. */
bool validateKey(const Request& request, QString* error)
{
	const auto fail = [error](const QString& reason) {
		if (error != nullptr) { *error = reason; }
		return false;
	};
	if (request.root < 0 || request.root > 11)
	{
		return fail(QStringLiteral("the key root is %1; a root is a pitch class 0..11 (C = 0)")
			.arg(request.root));
	}
	if (request.octave < ChordVocabulary::MinOctave || request.octave > ChordVocabulary::MaxOctave)
	{
		return fail(QStringLiteral("the octave is %1; this engine's octaves are %2..%3")
			.arg(request.octave).arg(ChordVocabulary::MinOctave).arg(ChordVocabulary::MaxOctave));
	}
	if (request.stepTicks < 1)
	{
		return fail(QStringLiteral("the step is %1 ticks; a chord needs at least one tick")
			.arg(static_cast<qint64>(request.stepTicks)));
	}
	return true;
}


/*! The WALK and LEVEL bounds of a request: how many chords, at what velocity,
 *  with how much variation, and whether the catalogue entry fits the degree
 *  bound. The second half of the gauntlet (see validateKey above). */
bool validateWalk(const Request& request, const Entry& entry, QString* error)
{
	const auto fail = [error](const QString& reason) {
		if (error != nullptr) { *error = reason; }
		return false;
	};
	if (request.steps < 1 || request.steps > MaxSteps)
	{
		return fail(QStringLiteral("%1 chords were asked for; this generator writes 1..%2")
			.arg(request.steps).arg(MaxSteps));
	}
	if (request.velocity < MinVolume || request.velocity > MaxVolume)
	{
		return fail(QStringLiteral("the velocity is %1; this engine's notes are %2..%3")
			.arg(request.velocity).arg(MinVolume).arg(MaxVolume));
	}
	if (request.variation < 0.0f || request.variation > 1.0f)
	{
		return fail(QStringLiteral("the variation is %1; it is a fraction in 0..1")
			.arg(static_cast<double>(request.variation)));
	}
	if (static_cast<int>(entry.degrees.size()) > MaxDegreeCount)
	{
		return fail(QStringLiteral("'%1' walks %2 degrees; the catalogue allows %3")
			.arg(entry.name).arg(entry.degrees.size()).arg(MaxDegreeCount));
	}
	return true;
}

} // namespace


namespace
{

/*! The walk itself: one ChordStep per step, built from the SCALE's own tones and
 *  named by the vocabulary. Split out of chordsFor() so one function does not
 *  carry the refusals AND the arithmetic (Gate 4's complexity ratchet). */
bool buildSteps(const Request& request, const ChordVocabulary::Chord& scale, const Entry& entry,
	std::vector<ChordStep>* out, QString* error)
{
	for (int index = 0; index < request.steps; ++index)
	{
		const int degree = entry.degrees[static_cast<std::size_t>(index) % entry.degrees.size()];
		if (degree < 0 || degree >= scale.size())
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("'%1' walks degree %2, and '%3' has %4 of them")
					.arg(entry.name).arg(degree).arg(request.scale).arg(scale.size());
			}
			out->clear();
			return false;
		}
		const std::vector<int> tones = stackedTones(scale, degree);
		const int absoluteRoot = request.root + tones[0];
		const int rootPc = ((absoluteRoot % 12) + 12) % 12;

		ChordStep step;
		step.degree = degree;
		step.root = rootPc;
		int baseKey = ChordVocabulary::keyOf(rootPc, request.octave + absoluteRoot / 12);
		// A chord that would run past the MIDI range is moved DOWN as a whole
		// rather than clipped tone by tone: a clipped chord is a different
		// chord, and the top of the range is not a musical statement.
		const int top = baseKey + (tones.back() - tones[0]);
		if (top > NumKeys - 1) { baseKey -= (top - (NumKeys - 1)); }
		if (baseKey < 0) { baseKey = 0; }
		step.rootKey = baseKey;
		std::vector<int> classes;
		for (const int tone : tones)
		{
			const int key = clampKey(baseKey + (tone - tones[0]));
			step.keys.push_back(key);
			classes.push_back(ChordVocabulary::pitchClassOf(key));
		}
		// The NAME is the vocabulary's, and an empty one is a real answer: the
		// scale spells a stack this product's table has no entry for.
		step.name = ChordVocabulary::exactChordName(classes, rootPc);
		out->push_back(step);
	}
	return true;
}

} // namespace


bool chordsFor(const Request& request, std::vector<ChordStep>* out, QString* error)
{
	const auto fail = [error](const QString& reason) {
		if (error != nullptr) { *error = reason; }
		return false;
	};
	if (out == nullptr) { return fail(QStringLiteral("no output vector")); }
	out->clear();

	const ChordVocabulary::Chord* scale = ChordVocabulary::scaleByName(request.scale);
	if (scale == nullptr)
	{
		return fail(QStringLiteral("'%1' is not a scale of this engine's vocabulary (its %2 "
			"scales are listed by chord.progression_list)")
			.arg(request.scale).arg(ChordVocabulary::scaleNames().size()));
	}
	const Entry* entry = find(request.progression);
	if (entry == nullptr)
	{
		return fail(QStringLiteral("no progression called '%1' (this engine knows %2: %3)")
			.arg(request.progression).arg(progressionNames().size())
			.arg(progressionNames().join(QStringLiteral(", "))));
	}
	if (!validateKey(request, error)) { return false; }
	if (!validateWalk(request, *entry, error)) { return false; }
	return buildSteps(request, *scale, *entry, out, error);
}


void layOutChord(const std::vector<int>& keys, tick_t pos, tick_t room, Pattern pattern, int velocity,
	std::vector<NoteSpec>* out)
{
	if (out == nullptr || keys.empty()) { return; }
	const tick_t span = room < 1 ? 1 : room;

	if (pattern == Pattern::Block)
	{
		for (const int key : keys) { appendNote(out, key, pos, span, pos, span, velocity); }
		return;
	}
	if (pattern == Pattern::Broken)
	{
		// The root is held for the whole chord and the tones above it are
		// arpeggiated in the slots that follow.
		const tick_t slot = std::max<tick_t>(1, span / static_cast<tick_t>(keys.size()));
		appendNote(out, keys[0], pos, span, pos, span, velocity);
		for (int voice = 1; voice < static_cast<int>(keys.size()); ++voice)
		{
			appendNote(out, keys[static_cast<std::size_t>(voice)],
				pos + static_cast<tick_t>(voice) * slot, slot, pos, span, velocity);
		}
		return;
	}
	layOutArpeggio(keys, pos, span, pattern == Pattern::ArpeggioDown, velocity, out);
}


bool generateNotes(const Request& request, std::vector<NoteSpec>* out, QString* error)
{
	if (out == nullptr) { return false; }
	out->clear();

	std::vector<ChordStep> chords;
	if (!chordsFor(request, &chords, error)) { return false; }

	for (int index = 0; index < request.steps; ++index)
	{
		const ChordStep& step = chords[static_cast<std::size_t>(index)];
		const tick_t nominal = request.start + static_cast<tick_t>(index) * request.stepTicks;
		const tick_t nextNominal = request.start + static_cast<tick_t>(index + 1) * request.stepTicks;

		int inversion = 0;
		tick_t positionJitter = 0;
		int velocity = request.velocity;
		drawVariation(request, step, &inversion, &positionJitter, &velocity);

		tick_t pos = nominal + positionJitter;
		if (pos < 0) { pos = 0; }

		const std::vector<int> voices = voicedKeys(step.keys, inversion);
		if (voices.empty()) { continue; }

		// The pattern itself is ChordProgression::layOutChord - ONE definition,
		// shared with the chord track's own write verb, so a track's chords and
		// a generated progression cannot be laid out differently.
		layOutChord(voices, pos, nextNominal - pos, request.pattern, velocity, out);
	}

	// Sorted by (position, key) so the output is comparable as a whole - the
	// clip re-sorts on insert anyway, and two runs are then bit-comparable.
	std::sort(out->begin(), out->end(), [](const NoteSpec& a, const NoteSpec& b) {
		if (a.pos != b.pos) { return a.pos < b.pos; }
		return a.key < b.key;
	});
	return true;
}


bool trackEventsFor(const Request& request, std::vector<ChordEvent>* out, QString* error)
{
	if (out == nullptr) { return false; }
	out->clear();

	std::vector<ChordStep> chords;
	if (!chordsFor(request, &chords, error)) { return false; }

	for (int index = 0; index < request.steps; ++index)
	{
		const ChordStep& step = chords[static_cast<std::size_t>(index)];
		if (step.name.isEmpty())
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("the chord on degree %1 of '%2' has no name in this "
					"engine's vocabulary, so it cannot be a chord-track event")
					.arg(step.degree).arg(request.scale);
			}
			out->clear();
			return false;
		}
		ChordEvent event;
		event.pos = request.start + static_cast<tick_t>(index) * request.stepTicks;
		event.length = request.stepTicks;
		event.root = step.root;
		// The root's own octave: keyOf()'s inverse, so a degree that wrapped an
		// octave keeps the octave it actually sounds in.
		event.octave = step.rootKey / 12 - 1;
		event.chord = step.name;
		event.scale = request.scale;
		out->push_back(event);
	}
	return true;
}

} // namespace ChordProgression

} // namespace lmms
