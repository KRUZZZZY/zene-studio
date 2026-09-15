/*
 * ControlCommandsNoteTransform.cpp - the `note.*` TRANSFORM verbs (SPEC A11-A16;
 *                                     board task #648, feature-list row 11).
 *
 * The engine is PRE-EXISTING and already tested (tests/src/core/NoteTransformTest.cpp);
 * what did not exist was an id. Each verb below is one function of
 * include/NoteTransform.h, called on the notes `scope` selects:
 *
 *   NoteTransform::transpose      (NoteTransform.h:95-97)  moves every note's
 *                                 pitch by semitones, clamped to the MIDI range
 *   NoteTransform::offsetVelocity (NoteTransform.h:99-101) adds a delta, clamped
 *                                 to the engine's volume range (0..200)
 *   NoteTransform::scaleVelocity  (NoteTransform.h:103-105) multiplies by a factor,
 *                                 rounded and clamped
 *
 * THE GRID QUANTISE IS DELIBERATELY NOT HERE. NoteTransform::quantizeNotes() is
 * already driven by `groove.quantize` (ControlCommandsGrooveEdit.cpp:232), with a
 * strength, a humanise amount and a seed; a second verb wrapping the same call
 * would be two ids for one behaviour, which is what SPEC A11's "one action, one
 * implementation" exists to prevent. What was missing from `note.*` was the three
 * transforms that need NO groove and NO grid.
 *
 * THE RANGE IS THE ENGINE'S, STATED. A transpose clamps at 0 and 127 per note (a
 * note already at the top does not move and is not counted as moved), and a
 * velocity lands inside [MinVolume, MaxVolume] - the same clamps
 * note.velocity_set documents. What is REFUSED, typed, is an argument outside the
 * range this surface accepts (+/-127 semitones, +/-200 velocity units, a factor
 * beyond 0..8), because a caller that asked for +300 semitones asked for something
 * no note can represent.
 *
 * A16: all three are true_inverse through the clip's own journal checkpoint - the
 * clip's note list IS its serialized state, so one control.undo puts every key and
 * every velocity back (the mechanism note.velocity_set and groove.apply reverse
 * with).
 *
 * UI ABSENCE: the piano roll's own transpose is an interactive drag and its
 * velocity edits are per note; no action, menu entry or shortcut applies any of
 * these three to a clip or to a selection (docs/KNOWN-LIMITATIONS.md, row 11).
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

#include <QJsonObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlGrooveSupport.h"
#include "ControlNoteShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteTransform.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The widest transpose this surface accepts, in semitones. The engine's MIDI
//! range is 0..127, so +/-127 is a full-range move; anything larger is refused
//! rather than silently clamped to a no-op.
constexpr int kMaxTranspose = 127;

//! The widest velocity delta and the widest velocity factor. The engine's own
//! velocity range is 0..200 (include/volume.h:35-36), so a delta of one full range
//! in either direction is the largest edit that can mean anything.
constexpr int kMaxVelocityDelta = 200;
constexpr double kMaxVelocityFactor = 8.0;

/*! Resolves the clip a transform runs over and the notes `scope` selects.
 *
 *  Refuses a clip with no notes: a transform over nothing would report a success
 *  that moved nothing (the refusal note.randomize and resolveGrooveClip both make).
 */
MidiClip* resolveTransformClip(const QJsonObject& args, ClipRef* ref, NoteVector* notes,
	NoteScope* scope, ControlResult* error)
{
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), ref, error);
	if (clip == nullptr) { return nullptr; }
	if (!readScope(args, scope, error)) { return nullptr; }
	if (clip->notes().empty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has no notes: there is nothing to transform")
				.arg(clipId(ref->ordinal)));
		return nullptr;
	}
	*notes = scopeNotes(*clip, *scope, clipId(ref->ordinal));
	if (notes->empty())
	{
		// A scope of "selection" with nothing selected is refused rather than
		// reported as an edit that changed 0 notes: "I edited nothing" and "there
		// was nothing to edit" are different answers, and a caller can tell them
		// apart only if the second one is an error.
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has notes but none is selected, so scope 'selection' would edit "
				"nothing (note.select chooses them)").arg(clipId(ref->ordinal)));
		return nullptr;
	}
	return clip;
}

/*! Reads an integer argument inside \a minimum..\a maximum, refusing anything else
 *  including a missing value. */
bool readInteger(const QJsonObject& args, const QString& key, int minimum, int maximum,
	int* out, ControlResult* error)
{
	const QJsonValue value = args.value(key);
	if (!value.isDouble())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is required and must be a whole number in [%2, %3]")
				.arg(key).arg(minimum).arg(maximum));
		return false;
	}
	const double number = value.toDouble();
	const int rounded = static_cast<int>(number);
	if (number != static_cast<double>(rounded) || rounded < minimum || rounded > maximum)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is %2, outside [%3, %4]").arg(key).arg(number)
				.arg(minimum).arg(maximum));
		return false;
	}
	*out = rounded;
	return true;
}

//! Reads a number argument inside \a minimum..\a maximum, refusing anything else.
bool readNumber(const QJsonObject& args, const QString& key, double minimum, double maximum,
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
			QStringLiteral("'%1' is %2, outside [%3, %4]").arg(key).arg(number)
				.arg(minimum).arg(maximum));
		return false;
	}
	*out = number;
	return true;
}

/*! The transaction payload every verb here records: the clip's own checkpoint, one
 *  recorded inverse op a reader can re-issue. */
QJsonObject transformTransaction(const ClipRef& ref, const MidiClip& clip, NoteScope scope,
	const QString& changedKey, int changed, const QJsonObject& inverseArgs)
{
	QJsonObject before;
	before.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	before.insert(QStringLiteral("track"), trackIdOf(ref.track));
	before.insert(QStringLiteral("note_count"), static_cast<int>(clip.notes().size()));
	before.insert(changedKey, changed);
	QJsonObject inverse = inverseArgs;
	inverse.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	inverse.insert(QStringLiteral("scope"), scopeName(scope));
	return transactionPayload(before, QStringLiteral("control.undo"), inverse, true,
		grooveClipMechanism() + QStringLiteral("; no velocity TARGET and no pre-transform key is "
			"stored per note, so the inverse is the checkpoint and not \"run the opposite "
			"transform\": a second transpose back would land notes the clamp had already "
			"moved somewhere else"));
}

//! The result shape every verb here returns: what it was asked for, what it acted
//! on, and how much really changed.
QJsonObject transformResult(const ClipRef& ref, const MidiClip& clip, NoteScope scope,
	int considered, const QJsonObject& arguments)
{
	QJsonObject result = arguments;
	result.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	result.insert(QStringLiteral("track"), trackIdOf(ref.track));
	result.insert(QStringLiteral("scope"), scopeName(scope));
	result.insert(QStringLiteral("note_count"), static_cast<int>(clip.notes().size()));
	result.insert(QStringLiteral("notes_considered"), considered);
	return result;
}

ControlResult transpose(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	NoteVector notes;
	NoteScope scope = NoteScope::Clip;
	MidiClip* clip = resolveTransformClip(args, &ref, &notes, &scope, &error);
	if (clip == nullptr) { return error; }
	int semitones = 0;
	if (!readInteger(args, QStringLiteral("semitones"), -kMaxTranspose, kMaxTranspose, &semitones,
		&error))
	{
		return error;
	}

	clip->addJournalCheckPoint();
	const int changed = NoteTransform::transpose(notes, semitones);
	// A transpose moves KEYS, and the clip's own order is (position, then key),
	// so the list has to be put back - the call note.move makes after it moves one
	// note. The positions did not move, so nothing else changes.
	clip->rearrangeAllNotes();
	clip->dataChanged();

	QJsonObject arguments;
	arguments.insert(QStringLiteral("semitones"), semitones);
	arguments.insert(QStringLiteral("notes_changed"), changed);
	QJsonObject result = transformResult(ref, *clip, scope, static_cast<int>(notes.size()),
		arguments);
	result.insert(QStringLiteral("__transaction"),
		transformTransaction(ref, *clip, scope, QStringLiteral("notes_changed"), changed,
			arguments));
	return ControlResult::success(result);
}

ControlResult velocityOffset(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	NoteVector notes;
	NoteScope scope = NoteScope::Clip;
	MidiClip* clip = resolveTransformClip(args, &ref, &notes, &scope, &error);
	if (clip == nullptr) { return error; }
	int delta = 0;
	if (!readInteger(args, QStringLiteral("delta"), -kMaxVelocityDelta, kMaxVelocityDelta, &delta,
		&error))
	{
		return error;
	}

	clip->addJournalCheckPoint();
	const int changed = NoteTransform::offsetVelocity(notes, delta);
	clip->dataChanged();

	QJsonObject arguments;
	arguments.insert(QStringLiteral("delta"), delta);
	arguments.insert(QStringLiteral("notes_changed"), changed);
	QJsonObject result = transformResult(ref, *clip, scope, static_cast<int>(notes.size()),
		arguments);
	result.insert(QStringLiteral("__transaction"),
		transformTransaction(ref, *clip, scope, QStringLiteral("notes_changed"), changed,
			arguments));
	return ControlResult::success(result);
}

ControlResult velocityScale(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	NoteVector notes;
	NoteScope scope = NoteScope::Clip;
	MidiClip* clip = resolveTransformClip(args, &ref, &notes, &scope, &error);
	if (clip == nullptr) { return error; }
	double factor = 1.0;
	if (!readNumber(args, QStringLiteral("factor"), 0.0, kMaxVelocityFactor, &factor, &error))
	{
		return error;
	}

	clip->addJournalCheckPoint();
	const int changed = NoteTransform::scaleVelocity(notes, static_cast<float>(factor));
	clip->dataChanged();

	QJsonObject arguments;
	arguments.insert(QStringLiteral("factor"), factor);
	arguments.insert(QStringLiteral("notes_changed"), changed);
	QJsonObject result = transformResult(ref, *clip, scope, static_cast<int>(notes.size()),
		arguments);
	result.insert(QStringLiteral("__transaction"),
		transformTransaction(ref, *clip, scope, QStringLiteral("notes_changed"), changed,
			arguments));
	return ControlResult::success(result);
}

//! The result schema every verb here shares, with the argument keys each one adds.
QJsonObject transformSchema(QJsonObject extra)
{
	QJsonObject schema;
	schema.insert(QStringLiteral("clip"), stringProperty());
	schema.insert(QStringLiteral("track"), stringProperty());
	schema.insert(QStringLiteral("scope"), stringProperty());
	schema.insert(QStringLiteral("note_count"), integerProperty());
	schema.insert(QStringLiteral("notes_considered"), integerProperty());
	schema.insert(QStringLiteral("notes_changed"), integerProperty());
	for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
	{
		schema.insert(it.key(), it.value());
	}
	return objectSchema(schema);
}

QJsonObject scopeArgument()
{
	return enumProperty({QStringLiteral("clip"), QStringLiteral("selection")});
}

} // namespace

void registerNoteTransformCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.transpose");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("transpose");
		cmd.description = QStringLiteral("Move every note of a clip (or of the current selection) "
			"by 'semitones'. A note whose target is outside the engine's 0..127 MIDI range is "
			"clamped there and is not counted as changed, which is NoteTransform::transpose's own "
			"documented rule; a 'semitones' value beyond +/-127 is refused rather than clamped to "
			"a no-op. 'scope' is 'clip' (default) or 'selection' - and a 'selection' scope with "
			"nothing selected is refused, because \"I edited nothing\" and \"there was nothing to "
			"edit\" are different answers. Reversible through the ProjectJournal (MidiClip "
			"checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("semitones"), integerProperty(-kMaxTranspose, kMaxTranspose)},
			{QStringLiteral("scope"), scopeArgument()},
		}, {QStringLiteral("clip"), QStringLiteral("semitones")});
		cmd.resultSchema = transformSchema({
			{QStringLiteral("semitones"), integerProperty(-kMaxTranspose, kMaxTranspose)},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return transpose(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.velocity_offset");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("velocity_offset");
		cmd.description = QStringLiteral("Add 'delta' to every note's velocity in scope, clamped to "
			"the engine's own 0..200 volume range (a note already at a bound does not move and is "
			"not counted as changed). The note the delta is added to is the CURRENT velocity, so "
			"applying it twice adds twice - there is no stored pre-edit velocity, and the inverse "
			"is the clip's checkpoint. 'scope' is 'clip' (default) or 'selection'. Reversible "
			"through the ProjectJournal (MidiClip checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("delta"),
				integerProperty(-kMaxVelocityDelta, kMaxVelocityDelta)},
			{QStringLiteral("scope"), scopeArgument()},
		}, {QStringLiteral("clip"), QStringLiteral("delta")});
		cmd.resultSchema = transformSchema({
			{QStringLiteral("delta"), integerProperty(-kMaxVelocityDelta, kMaxVelocityDelta)},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return velocityOffset(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("note.velocity_scale");
		cmd.group = QStringLiteral("note");
		cmd.verb = QStringLiteral("velocity_scale");
		cmd.description = QStringLiteral("Multiply every note's velocity in scope by 'factor' "
			"(0..8), rounded and clamped to the engine's own 0..200 volume range. A factor above 1 "
			"is a crescendo, below 1 a diminuendo, and 0 silences the notes without deleting them; "
			"a note already at a bound is not counted as changed. It is multiplicative on the "
			"CURRENT velocity, so applying it twice multiplies twice. 'scope' is 'clip' (default) "
			"or 'selection'. Reversible through the ProjectJournal (MidiClip checkpoint).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("factor"), numberProperty()},
			{QStringLiteral("scope"), scopeArgument()},
		}, {QStringLiteral("clip"), QStringLiteral("factor")});
		cmd.resultSchema = transformSchema({
			{QStringLiteral("factor"), numberProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return velocityScale(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
