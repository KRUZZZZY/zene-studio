/*
 * ControlCommandsGrooveEdit.cpp - the CLIP half of the groove.* command
 *                                 group's mutating verbs (SPEC A11-A16):
 *                                 groove.apply / groove.quantize.
 *
 * Both move the NOTES of one MIDI clip and nothing else, and both are
 * reversible through the clip's own journal checkpoint: a MidiClip is a
 * JournallingObject whose serialized state IS its note list, so the checkpoint
 * taken before the edit restores every position and velocity it moved - the
 * mechanism note.move and note.velocity_set reverse with (SPEC A16 class
 * `true_inverse`). The pool-editing verbs (groove.set / remove / rename, in
 * ControlCommandsGroovePool.cpp) have a different inverse - a recorded action
 * checkpoint - which is why the two halves are separate translation units.
 *
 * The arithmetic itself is engine code and is NOT here: applyGroove lives in
 * src/core/GrooveTemplate.cpp and quantizeNotes in src/core/NoteTransform.cpp,
 * both plain functions over a note list so they can be tested with no Engine at
 * all. What this file owns is the surface: the schemas, the refusals, and the
 * one-command-one-undo-step bookkeeping.
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

#include <QJsonObject>
#include <QString>

#include "ControlEdit.h" // ClipRef, resolveMidiClip, clipId, trackIdOf
#include "ControlGrooveSupport.h"
#include "ControlRegistry.h"
#include "MidiClip.h"
#include "NoteTransform.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

namespace
{

//! The largest tick value the schema subset can carry.
constexpr int kMaxSchemaInteger = 2147483647;

/*! The before-state of a CLIP edit.
 *
 *  Deliberately the clip's identity and its note COUNT, not its note list: the
 *  inverse of a clip edit is the clip's own journal checkpoint, a clip may hold
 *  an unbounded number of notes, and a record that grew with the clip would be
 *  the one thing control::MaxTransactionBytes cannot bound. The checkpoint
 *  restores the notes; this descriptor says which clip was touched.
 */
QJsonObject grooveClipBefore(const ClipRef& ref, const MidiClip& clip)
{
	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("track"), trackIdOf(ref.track));
	before.insert(QStringLiteral("note_count"), static_cast<int>(clip.notes().size()));
	return before;
}


/*! The recorded inverse of a clip edit.
 *
 *  The mechanism is the checkpoint, so the recorded op is `control.undo` with
 *  the target as its argument - a command the surface really implements and a
 *  reader can re-issue. It is deliberately NOT "groove.apply with the same
 *  arguments": that would be a descriptor that does not undo anything (an
 *  apply at strength 1 is idempotent, and a quantise has already moved the
 *  notes it would move again).
 */
QJsonObject grooveClipInverse(const QJsonObject& before, const QJsonObject& target)
{
	return transactionPayload(before, QStringLiteral("control.undo"), target, true,
		grooveClipMechanism());
}


//! Reads the optional mode name; absent reads as the nearest grid step.
bool readQuantizeMode(const QJsonObject& args, NoteTransform::QuantizeMode* out,
	ControlResult* error)
{
	const QString mode = args.value(QStringLiteral("mode")).toString();
	if (mode.isEmpty() || mode == QLatin1String("nearest"))
	{
		*out = NoteTransform::QuantizeMode::Nearest;
		return true;
	}
	if (mode == QLatin1String("floor")) { *out = NoteTransform::QuantizeMode::Floor; return true; }
	if (mode == QLatin1String("ceil")) { *out = NoteTransform::QuantizeMode::Ceil; return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'mode' is '%1'; it is 'nearest', 'floor' or 'ceil'").arg(mode));
	return false;
}


/*! Reads the optional humanise arguments.
 *
 *  The timing jitter may not exceed the grid: a jitter that large could move a
 *  note into a different slot, which is a rearrangement and not a
 *  humanisation. The velocity jitter is bounded by the engine's own volume
 *  range. The seed is bounded by what the schema subset can carry, since it
 *  travels as an integer.
 */
bool readHumanise(const QJsonObject& args, tick_t grid, NoteTransform::QuantizeOptions* options,
	ControlResult* error)
{
	if (args.contains(QStringLiteral("humanise_ticks"))
		&& !readTicks(args, QStringLiteral("humanise_ticks"), 0, grid,
			&options->humaniseTicks, error))
	{
		return false;
	}
	if (args.contains(QStringLiteral("humanise_velocity")))
	{
		tick_t velocity = 0;
		if (!readTicks(args, QStringLiteral("humanise_velocity"), 0, MaxVolume, &velocity, error))
		{
			return false;
		}
		options->humaniseVelocity = static_cast<int>(velocity);
	}
	if (!args.contains(QStringLiteral("seed"))) { return true; }

	const QJsonValue seed = args.value(QStringLiteral("seed"));
	const double value = seed.toDouble(-1.0);
	if (!seed.isDouble() || value < 0.0 || value > static_cast<double>(kMaxSchemaInteger))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'seed' is %1; the humanise jitter is a pure function of it and of "
				"each note's own identity, so it is a whole number in 0..%2")
				.arg(value).arg(kMaxSchemaInteger));
		return false;
	}
	options->seed = static_cast<uint32_t>(value);
	return true;
}


ControlResult grooveApply(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to read"));
	}
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveGrooveClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	const QString name =
		GrooveTemplate::normalisedName(args.value(QStringLiteral("name")).toString());
	const GrooveTemplate* groove = pool->find(name);
	if (groove == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no groove called '%1' (the pool holds %2)")
				.arg(name).arg(pool->size()));
	}
	float strength = 1.0f;
	if (!readStrength(args, &strength, &error)) { return error; }
	// The groove is captured BY VALUE: the pool can be edited through another
	// command while this one runs, and the edit below must apply the groove
	// this call found.
	const GrooveTemplate applied = *groove;

	const NoteSnapshot before = snapshotNotes(*clip);
	const QJsonObject beforeState = grooveClipBefore(ref, *clip);
	clip->addJournalCheckPoint();
	applyGroove(clip->notes(), applied, strength);
	// The notes moved, so the clip's own order (position, then key) has to be
	// restored - the call note.move makes after it moves one note.
	clip->rearrangeAllNotes();
	clip->dataChanged();

	QJsonObject target;
	target.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	target.insert(QStringLiteral("name"), name);

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("name"), name);
	result.insert(QStringLiteral("strength"), static_cast<double>(strength));
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	addMoveCounts(before, *clip, &result);
	result.insert(QStringLiteral("__transaction"), grooveClipInverse(beforeState, target));
	return ControlResult::success(result);
}


ControlResult grooveQuantize(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	MidiClip* clip = resolveGrooveClip(args, &ref, &error);
	if (clip == nullptr) { return error; }

	NoteTransform::QuantizeOptions options;
	if (!readTicks(args, QStringLiteral("grid"), 1, kMaxSchemaInteger, &options.grid, &error))
	{
		return error;
	}
	if (!readStrength(args, &options.strength, &error)) { return error; }
	if (!readQuantizeMode(args, &options.mode, &error)) { return error; }
	if (!readHumanise(args, options.grid, &options, &error)) { return error; }

	const NoteSnapshot before = snapshotNotes(*clip);
	const QJsonObject beforeState = grooveClipBefore(ref, *clip);
	clip->addJournalCheckPoint();
	NoteTransform::quantizeNotes(clip->notes(), options);
	clip->rearrangeAllNotes();
	clip->dataChanged();

	QJsonObject target;
	target.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	target.insert(QStringLiteral("grid"), static_cast<qint64>(options.grid));

	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("grid"), static_cast<qint64>(options.grid));
	result.insert(QStringLiteral("strength"), static_cast<double>(options.strength));
	result.insert(QStringLiteral("humanise_ticks"), static_cast<qint64>(options.humaniseTicks));
	result.insert(QStringLiteral("humanise_velocity"), options.humaniseVelocity);
	result.insert(QStringLiteral("seed"), static_cast<qint64>(options.seed));
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip->notes().size()));
	addMoveCounts(before, *clip, &result);
	result.insert(QStringLiteral("__transaction"), grooveClipInverse(beforeState, target));
	return ControlResult::success(result);
}


//! One registration step per verb, because a description this long beside its
//! schema buries both.
void registerGrooveApply(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("groove.apply");
	cmd.group = QStringLiteral("groove");
	cmd.verb = QStringLiteral("apply");
	cmd.description = QStringLiteral("Apply a named groove to a MIDI clip's notes: each note is "
		"snapped to the slot it is nearest to and shifted by that slot's timing offset, and "
		"its velocity shifted by the slot's velocity offset (clamped to 0..200). 'strength' "
		"(0..1, default 1) is how far each note travels, so 0.5 is half the feel. Reversible "
		"through the ProjectJournal (MidiClip checkpoint): one control.undo restores every "
		"position and velocity.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("strength"), numberProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("name")});
	cmd.resultSchema = grooveStateSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("strength"), numberProperty()},
		{QStringLiteral("note_count"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("positions_moved"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("velocities_moved"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("notes_moved"), integerProperty(0, kMaxSchemaInteger)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return grooveApply(args); };
	registry.registerCommand(cmd);
}


void registerGrooveQuantize(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("groove.quantize");
	cmd.group = QStringLiteral("groove");
	cmd.verb = QStringLiteral("quantize");
	cmd.description = QStringLiteral("Quantise a MIDI clip's notes onto a grid of 'grid' "
		"ticks, with 'strength' (0..1, default 1) for how far each note travels and a "
		"'humanise_ticks' / 'humanise_velocity' amount added afterwards. 'mode' is 'nearest' "
		"(default), 'floor' or 'ceil'. The humanise jitter is a pure function of 'seed' "
		"(default 0) and each note's own identity, so the same call always produces the same "
		"take. Reversible through the ProjectJournal (MidiClip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("grid"), integerProperty(1, kMaxSchemaInteger)},
		{QStringLiteral("strength"), numberProperty()},
		{QStringLiteral("humanise_ticks"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("humanise_velocity"), integerProperty(0, static_cast<int>(MaxVolume))},
		{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("mode"), enumProperty({QStringLiteral("nearest"), QStringLiteral("floor"),
			QStringLiteral("ceil")})},
	}, {QStringLiteral("clip"), QStringLiteral("grid")});
	cmd.resultSchema = grooveStateSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("grid"), integerProperty(1, kMaxSchemaInteger)},
		{QStringLiteral("strength"), numberProperty()},
		{QStringLiteral("humanise_ticks"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("humanise_velocity"), integerProperty(0, static_cast<int>(MaxVolume))},
		{QStringLiteral("seed"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("note_count"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("positions_moved"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("velocities_moved"), integerProperty(0, kMaxSchemaInteger)},
		{QStringLiteral("notes_moved"), integerProperty(0, kMaxSchemaInteger)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return grooveQuantize(args); };
	registry.registerCommand(cmd);
}

} // namespace


} // namespace control

void registerGrooveEditCommands(ControlRegistry& registry)
{
	// The clip-editing half: the inverse of both verbs is the clip's own
	// journal checkpoint.
	registerGrooveApply(registry);
	registerGrooveQuantize(registry);
}

} // namespace lmms
