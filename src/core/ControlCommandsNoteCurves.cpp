/*
 * ControlCommandsNoteCurves.cpp - note.pitch_curve_get / set: a note's own pitch curve
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
 *
 */

/*! ED-122 (0.5.0 L07): a per-note expression curve on the spine - the note's pitch curve, which the
 *  engine already plays (the note's DetuningHelper, read by its play handle as the note sounds; the
 *  classic piano roll's detuning tool edits the same curve). Points are ticks from the note's start
 *  and semitones (+-60), linear between. Each write is one MidiClip checkpoint: the curve is saved
 *  inside the note's element. Pressure and timbre stay per-note values (note.expression_set). */

#include <cmath>

#include <QJsonArray>

#include "AutomationClip.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxCurvePoints = 256;

QJsonArray curveJson(const Note* note)
{
	QJsonArray out;
	if (note->detuning() == nullptr || !note->detuning()->hasAutomation()) { return out; }
	const AutomationClip::timeMap& map = note->detuning()->automationClip()->getTimeMap();
	for (auto it = map.begin(); it != map.end(); ++it)
	{
		out.append(QJsonObject{{QStringLiteral("tick"), POS(it)}, {QStringLiteral("semitones"), static_cast<double>(OUTVAL(it))}});
	}
	return out;
}

//! A curve point: where (ticks from the note's start, or with 'relative' a fraction 0..1 of its length) and how far.
struct CurvePoint
{
	double at = 0.0;
	float semitones = 0.f;
};

bool readCurve(const QJsonArray& json, bool relative, std::vector<CurvePoint>* points, ControlResult* error)
{
	if (json.size() > MaxCurvePoints)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("at most %1 points").arg(MaxCurvePoints));
		return false;
	}
	for (const QJsonValue& value : json)
	{
		const QJsonObject point = value.toObject();
		const double at = point.value(QStringLiteral("tick")).toDouble(-1.0);
		const double semitones = point.value(QStringLiteral("semitones")).toDouble(1e9);
		if (at < 0.0 || (relative && at > 1.0) || std::abs(semitones) > MaxDetuning)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a point needs tick >= 0 from the note's "
				"start (0..1 of its length with 'relative') and |semitones| <= %1").arg(MaxDetuning));
			return false;
		}
		points->push_back({at, static_cast<float>(semitones)});
	}
	return true;
}

//! The notes the call names: 'notes' (a list of ids), else the one 'note'.
bool targetNotes(MidiClip* clip, const QJsonObject& args, std::vector<Note*>* notes, ControlResult* error)
{
	QJsonArray ids = args.value(QStringLiteral("notes")).toArray();
	if (ids.isEmpty()) { ids.append(args.value(QStringLiteral("note"))); }
	for (const QJsonValue& id : ids)
	{
		int index = -1;
		Note* note = resolveNote(clip, id.toString(), &index, error);
		if (note == nullptr) { return false; }
		notes->push_back(note);
	}
	return true;
}

void putCurve(Note* note, const std::vector<CurvePoint>& points, bool relative)
{
	note->clearDetuning();  // a fresh curve: the old one may be shared with a copy of the note
	if (points.empty()) { return; }
	note->createDetuning();
	AutomationClip* curve = note->detuning()->automationClip();
	const double length = static_cast<double>(note->length().getTicks());
	for (const CurvePoint& point : points)
	{
		const auto tick = static_cast<tick_t>(std::lround(relative ? point.at * length : point.at));
		curve->putValue(TimePos(tick), point.semitones, false);
	}
}

ControlResult setCurve(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	std::vector<Note*> notes;
	if (!targetNotes(clip, args, &notes, &error)) { return error; }
	const bool relative = args.value(QStringLiteral("relative")).toBool();
	std::vector<CurvePoint> points;
	if (!readCurve(args.value(QStringLiteral("points")).toArray(), relative, &points, &error)) { return error; }
	const QJsonArray before = curveJson(notes.front());
	clip->addJournalCheckPoint();
	for (Note* note : notes) { putCurve(note, points, relative); }
	emit clip->dataChanged();
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("note"), noteIdOf(notes.front())},
		{QStringLiteral("notes"), static_cast<int>(notes.size())}, {QStringLiteral("points"), curveJson(notes.front())}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("points"), before}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("MidiClip journal checkpoint: undo restores the notes' previous curves")));
	return ControlResult::success(result);
}

ControlResult getCurve(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, &error);
	if (clip == nullptr) { return error; }
	int index = -1;
	const Note* note = resolveNote(clip, args.value(QStringLiteral("note")).toString(), &index, &error);
	if (note == nullptr) { return error; }
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("note"), noteIdOf(note)},
		{QStringLiteral("points"), curveJson(note)}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("note.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerNoteCurveCommands(ControlRegistry& registry)
{
	const QJsonObject which = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("note"), stringProperty()}},
		{QStringLiteral("clip"), QStringLiteral("note")});
	add(registry, "pitch_curve_get", QStringLiteral("A note's own pitch curve: [{tick from the note's start, semitones}] "
		"(empty: none)."), which, false, getCurve);
	add(registry, "pitch_curve_set", QStringLiteral("Replace the own pitch curve of a note ('note') or of several ('notes', "
		"ids) - the bend each plays while it sounds: 'points' [{tick from the note's start, semitones within +-60}], linear "
		"between; with 'relative' each tick is a fraction 0..1 of the note's own length (a shape for notes of any length); "
		"no points removes the curves. One undo step (MidiClip checkpoint)."), objectSchema({{QStringLiteral("clip"),
		stringProperty()}, {QStringLiteral("note"), stringProperty()}, {QStringLiteral("notes"), arrayProperty()},
		{QStringLiteral("relative"), booleanProperty()}, {QStringLiteral("points"), arrayProperty()}},
		{QStringLiteral("clip")}), true, setCurve);
}

} // namespace lmms
