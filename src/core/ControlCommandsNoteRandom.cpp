/*
 * ControlCommandsNoteRandom.cpp - the `note.*` RANDOMISATION verbs: the seeded,
 *                                 persisted roll (SPEC A11-A16; board task #648,
 *                                 feature-list row 11).
 *
 * THE ITEM THIS CLOSES. Row 11 of docs/FEATURE-LIST-0.3.0.md: the engine and its
 * registered tests are in the tree (NoteRandomTest, NoteTransformTest,
 * SlideNotesTest, MidiProbabilityPersistenceTest) and `note.*` covered add / move
 * / remove / resize / select / velocity_set / expression_* only. The engine this
 * file drives is PRE-EXISTING; what did not exist was an id, a schema, an A16 row
 * and a proof.
 *
 *   include/NoteRandom.h:55-76    readProjectSeed / writeProjectSeed (the seed in
 *                                 the project header) and the seeded roll:
 *                                 roll / rollUnit / velocityFactor - every one a
 *                                 PURE FUNCTION of the seed and a note's identity
 *                                 (key, pos, length), with no hidden state
 *   include/Song.h:323-334        Song::midiSeed() / setMidiSeed(), the project
 *                                 value the header carries
 *   src/core/NoteRandom.cpp:93-105 the header attribute ("midiseed"), written only
 *                                 when it is not the default 0
 *
 * SEEDED AND PERSISTED, stated exactly. `note.randomize` rolls each note's velocity
 * (NoteRandom::velocityFactor, salt 1) and position (rollUnit, salt 2) from a seed.
 * The seed it uses by DEFAULT is the project's own (Song::midiSeed, persisted as
 * the header's "midiseed" on save), so a call is repeatable and survives a
 * save/re-open. A caller that names a `seed` argument rolls from that number
 * WITHOUT writing it to the project; `note.random_seed_set` is the verb that
 * persists one. Same seed on the same starting notes -> the same velocities;
 * different seed -> a different take. That pair is what the proof asserts, so a
 * comparator that only checks "the call succeeded" cannot pass it.
 *
 * THE ROLL IS NOT A TARGET. The velocity roll is multiplicative on the note's
 * CURRENT velocity and the position roll is drawn from the note's identity AT
 * ENTRY, so a second call rolls on top of the first rather than being a fixed
 * point (the engine's humanise has the same property: include/NoteTransform.h
 * :142-155). The inverse is therefore the clip's checkpoint, not "run it again".
 *
 * A16: note.random_seed_get is not_mutating; note.random_seed_set is true_inverse
 * through a recorded ACTION step (the seed is Song state, not a JournallingObject,
 * so there is no live checkpoint - the shape clock.master_set uses); note.randomize
 * is true_inverse through a live MidiClip checkpoint.
 *
 * UI ABSENCE. No action, view or shortcut in this tree randomises a clip or shows
 * the project seed (docs/KNOWN-LIMITATIONS.md, the row 11 line); this group is the
 * only route to the seeded roll.
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

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlGrooveSupport.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteRandom.h"
#include "Song.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The timing roll's own salt. MIDI depth separates its streams the same way
//! (NoteRandom::velocityFactor uses salt 1), so a velocity roll here does not
//! move the probability roll of the same note.
constexpr uint32_t kTimingSalt = 2;

//! Resolves the clip a roll runs over, refusing one with no notes: a roll over
//! nothing would report a success that moved nothing.
MidiClip* resolveRollClip(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), ref, error);
	if (clip == nullptr) { return nullptr; }
	if (clip->notes().empty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has no notes: there is nothing to roll").arg(clipId(ref->id)));
		return nullptr;
	}
	return clip;
}

/*! A seed argument, if the caller named one.
 *
 *  Absent (`*present` false) means "roll from the project's own seed". A value
 *  outside 0..INT_MAX is refused rather than truncated: the engine's seed is a
 *  32-bit unsigned, and a truncated seed would be a DIFFERENT roll reported as the
 *  one that was asked for.
 */
bool readSeed(const QJsonObject& args, uint32_t* out, bool* present, ControlResult* error)
{
	*present = false;
	if (!args.contains(QStringLiteral("seed"))) { return true; }
	const QJsonValue value = args.value(QStringLiteral("seed"));
	const double number = value.toDouble(-1.0);
	if (!value.isDouble() || number < 0.0 || number > static_cast<double>(kMaxSchemaInteger))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'seed' is %1; the roll is a pure function of it and of each note's own "
				"identity, so it is a whole number in 0..%2").arg(number).arg(kMaxSchemaInteger));
		return false;
	}
	*out = static_cast<uint32_t>(number);
	*present = true;
	return true;
}

/*! A jitter argument, refused outside \a minimum..\a maximum rather than clamped
 *  (SPEC A11: a caller that asked for 5 asked for something this engine cannot
 *  mean - the same rule note.probability_set follows for its range). */
bool readJitter(const QJsonObject& args, const QString& key, double minimum, double maximum,
	double* out, ControlResult* error)
{
	const QJsonValue value = args.value(key);
	if (!value.isDouble())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is required and must be a number in [%2, %3]")
				.arg(key).arg(minimum).arg(maximum));
		return false;
	}
	const double number = value.toDouble();
	if (number < minimum || number > maximum)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is %2, outside [%3, %4]: the roll is a fraction of what the note "
				"already carries, and it is refused rather than clamped")
				.arg(key).arg(number).arg(minimum).arg(maximum));
		return false;
	}
	*out = number;
	return true;
}

//! The Song, or the typed refusal every handler here starts with.
Song* songOrRefuse(ControlResult* error)
{
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no project state to read or write"));
	}
	return song;
}

ControlResult randomSeedGet()
{
	ControlResult error;
	Song* song = songOrRefuse(&error);
	if (song == nullptr) { return error; }
	QJsonObject result;
	// A number, not an integer: the engine's seed is a uint32_t and the schema
	// subset's integer is signed, so a project carrying a seed above INT_MAX is
	// reported exactly rather than wrapped.
	result.insert(QStringLiteral("seed"), static_cast<double>(song->midiSeed()));
	result.insert(QStringLiteral("stored_in"),
		QStringLiteral("the project header's \"midiseed\" attribute, written only when the seed "
			"is not 0"));
	result.insert(QStringLiteral("default_is_zero"), song->midiSeed() == 0u);
	return ControlResult::success(result);
}

/*! note.random_seed_set - the PERSISTED half of the pair.
 *
 *  The seed is the Song's own value and the save path writes it into the project
 *  header, so this verb is what makes "the same seed reproduces the take" survive
 *  a save and a re-open. The Song is not a JournallingObject, so there is no live
 *  checkpoint: the inverse is ONE recorded action step on the engine's own undo
 *  stack (control::addUndoStep), the shape clock.master_set uses for the same
 *  reason.
 */
ControlResult randomSeedSet(const QJsonObject& args)
{
	ControlResult error;
	Song* song = songOrRefuse(&error);
	if (song == nullptr) { return error; }
	uint32_t requested = 0;
	bool present = false;
	if (!readSeed(args, &requested, &present, &error)) { return error; }
	if (!present)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'seed' is required: this verb writes the project's MIDI seed, and "
				"persisting it is the whole point of it"));
	}

	const uint32_t previous = song->midiSeed();
	song->setMidiSeed(requested);
	control::addUndoStep(
		[previous]() {
			Song* live = Engine::getSong();
			if (live != nullptr) { live->setMidiSeed(previous); }
		},
		[requested]() {
			Song* live = Engine::getSong();
			if (live != nullptr) { live->setMidiSeed(requested); }
		});

	QJsonObject result;
	result.insert(QStringLiteral("seed"), static_cast<double>(song->midiSeed()));
	result.insert(QStringLiteral("previous"), static_cast<double>(previous));
	result.insert(QStringLiteral("changed"), song->midiSeed() != previous);
	result.insert(QStringLiteral("persisted"), true);
	QJsonObject before;
	before.insert(QStringLiteral("seed"), static_cast<double>(previous));
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("seed"), static_cast<double>(previous));
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("note.random_seed_set"), inverseArgs, true,
			QStringLiteral("recorded ACTION checkpoint: the seed lives on the Song and in the "
				"project header (Song::saveProject writes \"midiseed\"), not on a "
				"JournallingObject, so the recorded undo step calls Song::setMidiSeed with the "
				"value the before-state holds and the redo half re-applies the requested one")));
	return ControlResult::success(result);
}

/*! note.randomize - the seeded, repeatable roll over a clip's notes.
 *
 *  Velocity: NoteRandom::velocityFactor(jitter, seed, key, pos, length), a
 *  multiplicative factor in [1-jitter, 1+jitter]. Position: rollUnit on its own
 *  salt, mapped to [-jitter, +jitter] ticks and clamped at tick 0, because a note
 *  before the start of the song is not a position this engine can hold.
 */
ControlResult randomize(const QJsonObject& args)
{
	ControlResult error;
	Song* song = songOrRefuse(&error);
	if (song == nullptr) { return error; }
	ClipRef ref;
	MidiClip* clip = resolveRollClip(args, &ref, &error);
	if (clip == nullptr) { return error; }
	NoteScope scope = NoteScope::Clip;
	if (!readScope(args, &scope, &error)) { return error; }

	double velocityJitter = 0.0;
	if (!readJitter(args, QStringLiteral("velocity_jitter"), 0.0, 1.0, &velocityJitter, &error))
	{
		return error;
	}
	double positionJitter = 0.0;
	if (args.contains(QStringLiteral("position_jitter"))
		&& !readJitter(args, QStringLiteral("position_jitter"), 0.0,
			static_cast<double>(kMaxSchemaInteger), &positionJitter, &error))
	{
		return error;
	}
	uint32_t seed = song->midiSeed();
	bool seedGiven = false;
	if (!readSeed(args, &seed, &seedGiven, &error)) { return error; }
	if (!seedGiven) { seed = song->midiSeed(); }

	const NoteVector notes = scopeNotes(*clip, scope, clipId(ref.id));
	const NoteSnapshot before = snapshotNotes(*clip);
	clip->addJournalCheckPoint();
	for (Note* note : notes)
	{
		if (note == nullptr) { continue; }
		const int key = note->key();
		const int pos = static_cast<int>(note->pos().getTicks());
		const int length = static_cast<int>(note->length().getTicks());
		if (velocityJitter > 0.0)
		{
			const float factor = NoteRandom::velocityFactor(static_cast<float>(velocityJitter),
				seed, key, pos, length);
			const int velocity = static_cast<int>(note->getVolume());
			const int rolled = std::clamp(static_cast<int>(std::lround(velocity * factor)),
				static_cast<int>(MinVolume), static_cast<int>(MaxVolume));
			if (rolled != velocity) { note->setVolume(static_cast<volume_t>(rolled)); }
		}
		if (positionJitter > 0.0)
		{
			const float unit = NoteRandom::rollUnit(seed, key, pos, length, kTimingSalt);
			const int offset = static_cast<int>(
				std::lround(2.0 * positionJitter * unit - positionJitter));
			const int moved = std::max(0, pos + offset);
			if (moved != pos) { note->setPos(TimePos(moved)); }
		}
	}
	// The notes moved, so the clip's own order (position, then key) has to be
	// restored - the call note.move makes after it moves one note.
	clip->rearrangeAllNotes();
	clip->dataChanged();

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("scope"), scopeName(scope));
	result.insert(QStringLiteral("seed"), static_cast<double>(seed));
	result.insert(QStringLiteral("seed_source"),
		seedGiven ? QStringLiteral("argument") : QStringLiteral("project"));
	result.insert(QStringLiteral("velocity_jitter"), velocityJitter);
	result.insert(QStringLiteral("position_jitter"), positionJitter);
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	result.insert(QStringLiteral("notes_rolled"), static_cast<int>(notes.size()));
	addMoveCounts(before, *clip, &result);

	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("clip"), clipId(ref.id));
	beforeState.insert(QStringLiteral("track"), trackIdOf(ref.track));
	beforeState.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	beforeState.insert(QStringLiteral("seed"), static_cast<double>(seed));
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(beforeState, QStringLiteral("control.undo"), inverseArgs, true,
			grooveClipMechanism() + QStringLiteral("; and because the roll is multiplicative on "
				"velocities and drawn from each note's position at entry, the inverse is the "
				"checkpoint and NOT \"run it again\" - a second call rolls on top of the first")));
	return ControlResult::success(result);
}

} // namespace

void registerNoteRandomCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.random_seed_get");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("random_seed_get");
		cmd.description = QStringLiteral("The project's MIDI seed: the number every seeded roll of "
			"this project draws from, persisted in the project header as the \"midiseed\" "
			"attribute and written only when it is not 0. Read-only. A seed of 0 is the default "
			"of every project saved before MIDI depth existed and of every project that never "
			"touched it, so 0 means \"this project rolls from the default\", not \"unset\".");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("seed"), numberProperty()},
			{QStringLiteral("stored_in"), stringProperty()},
			{QStringLiteral("default_is_zero"), booleanProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) { return randomSeedGet(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.random_seed_set");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("random_seed_set");
		cmd.description = QStringLiteral("Set the project's MIDI seed: the PERSISTED half of the "
			"seed pair. Every later note.randomize that names no seed rolls from this value, and "
			"the save path writes it into the project header, so a take you liked is reproducible "
			"after a save and a re-open. The seed travels as a whole number in 0..2147483647 (the "
			"engine's is a 32-bit unsigned; a larger one cannot be spelled in this schema subset "
			"and is refused rather than truncated). Reversible: a recorded action step on the "
			"engine's own undo stack restores the previous seed, so Ctrl+Z and control.undo are "
			"one history.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
		}, {QStringLiteral("seed")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("seed"), numberProperty()},
			{QStringLiteral("previous"), numberProperty()},
			{QStringLiteral("changed"), booleanProperty()},
			{QStringLiteral("persisted"), booleanProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return randomSeedSet(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.randomize");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("randomize");
		cmd.description = QStringLiteral("Roll a clip's notes with the engine's SEEDED "
			"randomisation: 'velocity_jitter' (0..1) multiplies each note's velocity by a factor "
			"in [1-j, 1+j] and 'position_jitter' (ticks, optional) moves each note by up to that "
			"many ticks, never before tick 0. Both draws are pure functions of the seed and the "
			"note's own identity (key, position, length at entry), so the SAME seed on the same "
			"starting notes reproduces the same take and a DIFFERENT seed produces a different "
			"one. The seed defaults to the project's persisted one; naming a 'seed' argument "
			"rolls from it without writing it to the project (note.random_seed_set is the verb "
			"that persists one). 'scope' is 'clip' (default) or 'selection'. The roll is "
			"multiplicative and drawn at entry, so a second call rolls on top of the first - the "
			"inverse is the clip's checkpoint, not another call. Reversible through the "
			"ProjectJournal (MidiClip checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("velocity_jitter"), numberProperty()},
			{QStringLiteral("position_jitter"), numberProperty()},
			{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
			{QStringLiteral("scope"), enumProperty({QStringLiteral("clip"),
				QStringLiteral("selection")})},
		}, {QStringLiteral("clip"), QStringLiteral("velocity_jitter")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("scope"), stringProperty()},
			{QStringLiteral("seed"), numberProperty()},
			{QStringLiteral("seed_source"), stringProperty()},
			{QStringLiteral("velocity_jitter"), numberProperty()},
			{QStringLiteral("position_jitter"), numberProperty()},
			{QStringLiteral("note_count"), integerProperty()},
			{QStringLiteral("notes_rolled"), integerProperty()},
			{QStringLiteral("positions_moved"), integerProperty()},
			{QStringLiteral("velocities_moved"), integerProperty()},
			{QStringLiteral("notes_moved"), integerProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return randomize(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
