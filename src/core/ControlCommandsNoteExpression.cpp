/*
 * ControlCommandsNoteExpression.cpp - the note.expression.* command group (SPEC
 *                                     A11-A16): read and write the per-note MPE
 *                                     expression task #601 stores (#602's
 *                                     per-note half).
 *
 * #601 landed per-note expression on the Note model, serialized as the optional
 * mpepitch/mpepressure/mpetimbre attributes, and playback applies the PITCH
 * axis (docs/MPE.md section 4). It shipped with NO control-surface command, so
 * until now nothing outside the process could read or write what a captured
 * gesture had stored. This group arms that existing plumbing rather than
 * inventing a second expression store: the arguments are the three axes #601
 * defines, and the inverse is a MidiClip checkpoint - the same mechanism
 * note.velocity_set reverses with, because MidiClip::loadSettings re-loads the
 * clip's note list.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "MpeExpression.h"
#include "Note.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString kGroup = QStringLiteral("note");

//! The checkpoint mechanism, stated once (note.velocity_set's own string).
const QString kMechanism = QStringLiteral("ProjectJournal (MidiClip checkpoint: "
	"MidiClip::loadSettings clears and re-loads the clip's note list, which is the mechanism the "
	"piano roll's own note edits reverse with)");

//! One note's expression. 'has_expression' is #601's own presence flag: a note
//! whose axes are all zero but which carries the attributes is NOT the same as
//! a note with no expression, and the wire says which is which.
QJsonObject expressionJson(const Note* note)
{
	QJsonObject out;
	out.insert(QStringLiteral("has_expression"), note->hasMpeExpression());
	out.insert(QStringLiteral("pitch_cents"), note->mpePitchCents());
	out.insert(QStringLiteral("pressure"), note->mpePressure());
	out.insert(QStringLiteral("timbre"), note->mpeTimbre());
	return out;
}

//! The clip/note pair every command in the group resolves first.
struct ExpressionTarget
{
	MidiClip* clip = nullptr;
	Note* note = nullptr;
	int index = -1;
	QString clipId;
	QString noteId;
};

bool resolveExpressionTarget(const QJsonObject& args, bool needsNote, ExpressionTarget* out,
	ControlResult* error)
{
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, error);
	if (clip == nullptr) { return false; }
	out->clip = clip;
	out->clipId = clipId(ref.id);
	if (!needsNote) { return true; }
	const QString wanted = args.value(QStringLiteral("note")).toString();
	out->note = resolveNote(clip, wanted, &out->index, error);
	if (out->note == nullptr) { return false; }
	// The id on the wire is the note's own (Note::id()); out->index is the
	// note's POSITION in its clip's list, which callers report but never address.
	out->noteId = noteIdOf(out->note);
	return true;
}

/*! The property set every command of the group declares. Returned as
 *  PROPERTIES, not as a schema: an objectSchema() call takes the property map,
 *  and handing it another schema nests {type:object, properties:{...}} under
 *  'properties' - which validates every real argument as an unexpected
 *  property (measured: "unexpected property 'clip'"). */
QJsonObject noteExpressionProperties()
{
	return QJsonObject{
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("pitch"), integerProperty(-MpeNoteExpression::MaxPitchCents,
			MpeNoteExpression::MaxPitchCents)},
		{QStringLiteral("pressure"), integerProperty(0, 127)},
	{QStringLiteral("timbre"), integerProperty(0, 127)},
	};
}

QJsonObject noteExpressionResultSchema()
{
	return objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("has_expression"), booleanProperty()},
		{QStringLiteral("pitch_cents"), integerProperty()},
		{QStringLiteral("pressure"), integerProperty()},
		{QStringLiteral("timbre"), integerProperty()},
		{QStringLiteral("notes"), arrayProperty()},
		{QStringLiteral("expression_count"), integerProperty()},
		{QStringLiteral("max_pitch_cents"), integerProperty()},
	});
}

void registerExpressionSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.expression_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("expression_set");
	cmd.description = QStringLiteral("Set a note's per-note MPE expression (task #601): 'pitch' "
		"is a bend offset in 1/100 semitone (+-4800), 'pressure' is channel pressure 0..127 and "
		"'timbre' is CC74 0..127. An axis the call omits keeps the value it had, and the note is "
		"marked as carrying expression either way. Reversible through the ProjectJournal (a "
		"MidiClip checkpoint).");
	cmd.argsSchema = objectSchema(noteExpressionProperties(),
		{QStringLiteral("clip"), QStringLiteral("note")});
	cmd.resultSchema = noteExpressionResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ExpressionTarget target;
		if (!resolveExpressionTarget(args, true, &target, &error)) { return error; }

		MpeNoteExpression wanted = target.note->mpeExpression();
		if (args.contains(QStringLiteral("pitch")))
		{
			wanted.pitchCents = static_cast<int>(args.value(QStringLiteral("pitch")).toDouble());
		}
		if (args.contains(QStringLiteral("pressure")))
		{
			wanted.pressure = static_cast<int>(args.value(QStringLiteral("pressure")).toDouble());
		}
		if (args.contains(QStringLiteral("timbre")))
		{
			wanted.timbre = static_cast<int>(args.value(QStringLiteral("timbre")).toDouble());
		}
		const QJsonObject before = expressionJson(target.note);

		target.clip->addJournalCheckPoint();
		target.note->setMpeExpression(wanted);
		target.clip->dataChanged();

		QJsonObject result = expressionJson(target.note);
		result.insert(QStringLiteral("clip"), target.clipId);
		result.insert(QStringLiteral("note"), target.noteId);
		result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("note.expression_set"),
				QJsonObject{{QStringLiteral("clip"), target.clipId},
					{QStringLiteral("note"), target.noteId},
					{QStringLiteral("pitch"), before.value(QStringLiteral("pitch_cents")).toInt()},
					{QStringLiteral("pressure"), before.value(QStringLiteral("pressure")).toInt()},
					{QStringLiteral("timbre"), before.value(QStringLiteral("timbre")).toInt()}},
				true, kMechanism));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExpressionGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.expression_get");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("expression_get");
	cmd.description = QStringLiteral("One note's per-note MPE expression, or - when 'note' is "
		"omitted - every note in the clip that carries one, with the count. 'has_expression' is "
		"the presence flag, so a captured-but-neutral note (every axis 0) is distinguishable from "
		"a note with no expression at all.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = noteExpressionResultSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ExpressionTarget target;
		if (!resolveExpressionTarget(args, false, &target, &error)) { return error; }

		QJsonObject result;
		result.insert(QStringLiteral("clip"), target.clipId);
		result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
		if (args.contains(QStringLiteral("note")))
		{
			if (!resolveExpressionTarget(args, true, &target, &error)) { return error; }
			result = expressionJson(target.note);
			result.insert(QStringLiteral("clip"), target.clipId);
			result.insert(QStringLiteral("note"), target.noteId);
			result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
			return ControlResult::success(result);
		}
		QJsonArray notes;
		const NoteVector& list = target.clip->notes();
		for (int i = 0; i < static_cast<int>(list.size()); ++i)
		{
			if (!list[i]->hasMpeExpression()) { continue; }
			QJsonObject entry = expressionJson(list[i]);
			// The entry carries the note's own id (Note::id(), SPEC-stable-ids.md
			// slice 2): `i` is a position in the list, and the note it holds is
			// already in hand, so no id has to be derived from the position.
			entry.insert(QStringLiteral("note"), noteIdOf(list[i]));
			notes.append(entry);
		}
		result.insert(QStringLiteral("notes"), notes);
		result.insert(QStringLiteral("expression_count"), notes.size());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExpressionClear(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.expression_clear");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("expression_clear");
	cmd.description = QStringLiteral("Drop a note's per-note MPE expression entirely, and the "
		"optional mpepitch/mpepressure/mpetimbre attributes with it - so the note serializes "
		"exactly as one that never carried expression. A no-op on a note that carries none is a "
		"typed refusal rather than a silent write. Reversible through the ProjectJournal (a "
		"MidiClip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("note")});
	cmd.resultSchema = noteExpressionResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ExpressionTarget target;
		if (!resolveExpressionTarget(args, true, &target, &error)) { return error; }
		if (!target.note->hasMpeExpression())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 carries no expression to clear").arg(target.noteId));
		}
		const QJsonObject before = expressionJson(target.note);

		target.clip->addJournalCheckPoint();
		target.note->clearMpeExpression();
		target.clip->dataChanged();

		QJsonObject result = expressionJson(target.note);
		result.insert(QStringLiteral("clip"), target.clipId);
		result.insert(QStringLiteral("note"), target.noteId);
		result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("note.expression_set"),
				QJsonObject{{QStringLiteral("clip"), target.clipId},
					{QStringLiteral("note"), target.noteId},
					{QStringLiteral("pitch"), before.value(QStringLiteral("pitch_cents")).toInt()},
					{QStringLiteral("pressure"), before.value(QStringLiteral("pressure")).toInt()},
					{QStringLiteral("timbre"), before.value(QStringLiteral("timbre")).toInt()}},
				true, kMechanism));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerNoteExpressionCommands(ControlRegistry& registry)
{
	registerExpressionSet(registry);
	registerExpressionGet(registry);
	registerExpressionClear(registry);
}

} // namespace lmms
