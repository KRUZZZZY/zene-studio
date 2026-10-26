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

#include <cmath>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "MpeExpression.h"
#include "Note.h"
#include "Song.h"

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

// ---------------------------------------------------------------------------
// Q050-016: the per-note expression EVENT STREAMS - pitch, pressure and timbre
// as ordered (tick, value) points, the time-varying half of #601's scalar
// snapshot. The two commands read and write the three optional attributes
// Note stores (mpepitchpts/mpepressurepts/mpetimbrepts), so the data is the
// same the playback path schedules and the project file round-trips.
// ---------------------------------------------------------------------------

QJsonArray streamPointsJson(const std::vector<MpeExpressionPoint>& points)
{
	QJsonArray out;
	for (const MpeExpressionPoint& point : points)
	{
		out.append(QJsonObject{{QStringLiteral("tick"), point.tick},
			{QStringLiteral("value"), point.value}});
	}
	return out;
}

//! The three axes of a note's stream (empty arrays when it carries none) plus
//! the presence flag, so an empty stream is never mistaken for a neutral one.
QJsonObject streamJson(const MpeExpressionStream* stream)
{
	QJsonObject out;
	out.insert(QStringLiteral("has_stream"), stream != nullptr);
	out.insert(QStringLiteral("pitch"),
		streamPointsJson(stream != nullptr ? stream->pitch() : std::vector<MpeExpressionPoint>{}));
	out.insert(QStringLiteral("pressure"),
		streamPointsJson(stream != nullptr ? stream->pressure() : std::vector<MpeExpressionPoint>{}));
	out.insert(QStringLiteral("timbre"),
		streamPointsJson(stream != nullptr ? stream->timbre() : std::vector<MpeExpressionPoint>{}));
	return out;
}

const MpeExpressionStream* streamOf(const Note* note)
{
	return note->hasMpeStream() ? &note->mpeStream() : nullptr;
}

QJsonObject streamResultSchema()
{
	return objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("has_stream"), booleanProperty()},
		{QStringLiteral("pitch"), arrayProperty()},
		{QStringLiteral("pressure"), arrayProperty()},
		{QStringLiteral("timbre"), arrayProperty()},
		{QStringLiteral("max_pitch_cents"), integerProperty()},
	});
}

/*! Read one axis's points. Every point needs a finite tick in 0..MaxSongLength
 *  from the note's start and a value inside the axis's range (pitch +-4800
 *  cents, pressure/timbre 0..127); a malformed point is a typed refusal, not a
 *  point at zero (QJsonValue holds a NaN, and QString/json toInt report a
 *  failed conversion with 0 - neither may be read as data). */
//! The axis a replaceStreamAxis() call names.
enum class StreamAxisKind
{
	Pitch,
	Pressure,
	Timbre
};

//! One point, refuted rather than read as zero when its text is malformed.
bool readStreamPoint(const QJsonObject& point, double low, double high,
	MpeExpressionPoint* out, ControlResult* error, const QString& axis)
{
	const double tick = point.value(QStringLiteral("tick")).toDouble(-1.0);
	const double value = point.value(QStringLiteral("value")).toDouble(1e9);
	const bool finite = std::isfinite(tick) && std::isfinite(value);
	if (!finite || tick < 0.0 || tick > static_cast<double>(MaxSongLength)
		|| value < low || value > high)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a %1 point needs tick 0..%2 from the note's start and value %3..%4")
				.arg(axis).arg(MaxSongLength).arg(low).arg(high));
		return false;
	}
	out->tick = static_cast<tick_t>(std::lround(tick));
	out->value = static_cast<int>(std::lround(value));
	return true;
}

bool readStreamPoints(const QJsonArray& json, bool pitch, std::vector<MpeExpressionPoint>* out,
	ControlResult* error, const QString& axis)
{
	if (json.size() > MpeExpressionStream::MaxPoints)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("at most %1 points per axis (%2 carried %3)")
				.arg(MpeExpressionStream::MaxPoints).arg(axis).arg(json.size()));
		return false;
	}
	const double low = pitch ? -MpeNoteExpression::MaxPitchCents : 0.0;
	const double high = pitch ? MpeNoteExpression::MaxPitchCents : 127.0;
	for (const QJsonValue& value : json)
	{
		MpeExpressionPoint point;
		if (!readStreamPoint(value.toObject(), low, high, &point, error, axis)) { return false; }
		out->push_back(point);
	}
	return true;
}

//! Replace one axis's points when the call names it, leaving the others alone.
bool replaceStreamAxis(const QJsonObject& args, const QString& key, StreamAxisKind kind,
	MpeExpressionStream* stream, ControlResult* error)
{
	if (!args.contains(key)) { return true; }
	std::vector<MpeExpressionPoint> points;
	if (!readStreamPoints(args.value(key).toArray(), kind == StreamAxisKind::Pitch,
		&points, error, key))
	{
		return false;
	}
	switch (kind)
	{
		case StreamAxisKind::Pitch: stream->setPitch(std::move(points)); break;
		case StreamAxisKind::Pressure: stream->setPressure(std::move(points)); break;
		case StreamAxisKind::Timbre: stream->setTimbre(std::move(points)); break;
	}
	return true;
}

void registerStreamGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.expression_stream_get");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("expression_stream_get");
	cmd.description = QStringLiteral("A note's time-varying MPE expression (Q050-016): the three "
		"axes as ordered [{tick from the note's start, value}] points - pitch in 1/100 semitone "
		"(+-4800), pressure and timbre 0..127. 'has_stream' tells an empty stream from a note that "
		"carries none; a point holds until the next one.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("note")});
	cmd.resultSchema = streamResultSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ExpressionTarget target;
		if (!resolveExpressionTarget(args, true, &target, &error)) { return error; }
		QJsonObject result = streamJson(streamOf(target.note));
		result.insert(QStringLiteral("clip"), target.clipId);
		result.insert(QStringLiteral("note"), target.noteId);
		result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerStreamSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("note.expression_stream_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("expression_stream_set");
	cmd.description = QStringLiteral("Replace a note's time-varying MPE expression stream "
		"(Q050-016). Each of 'pitch', 'pressure' and 'timbre' is an optional array of "
		"[{tick from the note's start, value}] points (pitch +-4800 cents, pressure/timbre "
		"0..127); an axis the call names replaces that axis's points (an empty array clears "
		"it), an axis it omits keeps its points. All three empty removes the stream. Points are "
		"sorted by tick on write. Reversible through the ProjectJournal (a MidiClip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
		{QStringLiteral("pitch"), arrayProperty()},
		{QStringLiteral("pressure"), arrayProperty()},
		{QStringLiteral("timbre"), arrayProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("note")});
	cmd.resultSchema = streamResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ExpressionTarget target;
		if (!resolveExpressionTarget(args, true, &target, &error)) { return error; }

		MpeExpressionStream stream = target.note->hasMpeStream()
			? target.note->mpeStream() : MpeExpressionStream{};
		if (!replaceStreamAxis(args, QStringLiteral("pitch"), StreamAxisKind::Pitch, &stream, &error)
			|| !replaceStreamAxis(args, QStringLiteral("pressure"), StreamAxisKind::Pressure, &stream, &error)
			|| !replaceStreamAxis(args, QStringLiteral("timbre"), StreamAxisKind::Timbre, &stream, &error))
		{
			return error;
		}

		const QJsonObject before = streamJson(streamOf(target.note));
		target.clip->addJournalCheckPoint();
		// Publication is one atomic swap of an immutable snapshot (Note.h,
		// MpeStreamSnapshot.h): a handle the audio thread copied keeps the old
		// snapshot, which is freed off the realtime path once it lets go.
		if (stream.empty()) { target.note->clearMpeStream(); }
		else { target.note->setMpeStream(stream); }
		MpeStreamSnapshot::reclaimRetired();
		target.clip->dataChanged();
		if (target.clip->linkId() > 0) { ClipLinks::mirrorContent(target.clip); }
		Engine::getSong()->setModified();

		QJsonObject result = streamJson(streamOf(target.note));
		result.insert(QStringLiteral("clip"), target.clipId);
		result.insert(QStringLiteral("note"), target.noteId);
		result.insert(QStringLiteral("max_pitch_cents"), MpeNoteExpression::MaxPitchCents);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("note.expression_stream_set"),
				QJsonObject{{QStringLiteral("clip"), target.clipId},
					{QStringLiteral("note"), target.noteId},
					{QStringLiteral("pitch"), before.value(QStringLiteral("pitch"))},
					{QStringLiteral("pressure"), before.value(QStringLiteral("pressure"))},
					{QStringLiteral("timbre"), before.value(QStringLiteral("timbre"))}},
				true, kMechanism));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
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
		const bool changed = before != expressionJson(target.note);
		target.clip->dataChanged();
		if (target.clip->linkId() > 0) { ClipLinks::mirrorContent(target.clip); }
		if (changed) { Engine::getSong()->setModified(); }

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
		if (target.clip->linkId() > 0) { ClipLinks::mirrorContent(target.clip); }
		Engine::getSong()->setModified();

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
	registerStreamSet(registry);
	registerStreamGet(registry);
}

} // namespace lmms
