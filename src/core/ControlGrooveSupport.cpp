/*
 * ControlGrooveSupport.cpp - the groove.* group's shared helpers: the pool
 *                             access, the wire form, the schemas and the
 *                             argument readers.
 *
 * Split out of ControlCommandsGroove.cpp so neither half of the group has to
 * carry the other's boilerplate: together they are the read half
 * (ControlCommandsGroove.cpp), the edit half (ControlCommandsGrooveEdit.cpp)
 * and this, and each of the three stays well under the file-length ratchet
 * (Gate 7, tests/file-length-gate.sh).
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlEdit.h" // ClipRef, resolveMidiClip, clipId
#include "ControlGrooveSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h" // control::addUndoStep
#include "Engine.h"
#include "MidiClip.h"
#include "Song.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

GroovePool* projectGroovePool()
{
	Song* song = Engine::getSong();
	return song == nullptr ? nullptr : &song->groovePool();
}


QString groovePoolXml()
{
	GroovePool* pool = projectGroovePool();
	return pool == nullptr ? QString() : pool->toXml();
}


void recordGroovePoolRestore(const QString& before)
{
	if (before.isEmpty()) { return; }
	// The redo half captures the pool as it is NOW (the edit has already run),
	// so a redo is the command again rather than a dropped step.
	const QString after = groovePoolXml();
	addUndoStep(
		[before]() {
			GroovePool* pool = projectGroovePool();
			if (pool != nullptr) { pool->fromXml(before); }
		},
		[after]() {
			GroovePool* pool = projectGroovePool();
			if (pool != nullptr) { pool->fromXml(after); }
		});
}


QJsonObject groovePoolInverse(const QString& before, const QString& inverseOp,
	const QJsonObject& inverseArgs)
{
	QJsonObject captured;
	captured.insert(QStringLiteral("pool"), before);

	QJsonObject inverse;
	inverse.insert(QStringLiteral("op"), inverseOp);
	inverse.insert(QStringLiteral("args"), inverseArgs);
	// "journal": the action checkpoint on the engine's OWN undo stack is what
	// control.undo unwinds, so an agent's command and a user's Ctrl+Z stay one
	// history (SPEC A16 deliverable 1).
	inverse.insert(QStringLiteral("applies"), QStringLiteral("journal"));

	QJsonObject payload;
	payload.insert(QStringLiteral("before"), captured);
	payload.insert(QStringLiteral("inverse"), inverse);
	payload.insert(QStringLiteral("reversible"), true);
	payload.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the pool is project state the Song's own journal "
			"checkpoint does not carry (it is not in the track container), so the recorded "
			"step writes the captured <groove-pool> element back - the mechanism "
			"docs/GROOVE-POOL.md section 5 records, the same one the tempo map and the "
			"modulation layer use"));
	return payload;
}


QJsonObject grooveJson(const GrooveTemplate& groove, bool detailed)
{
	QJsonObject out;
	out.insert(QStringLiteral("name"), groove.name());
	out.insert(QStringLiteral("length_ticks"), static_cast<qint64>(groove.lengthTicks()));
	out.insert(QStringLiteral("step_ticks"), static_cast<qint64>(groove.stepTicks()));
	out.insert(QStringLiteral("slot_count"), groove.slotCount());
	if (!detailed) { return out; }

	out.insert(QStringLiteral("neutral"), groove.neutral());
	QJsonArray steps;
	for (int slot = 0; slot < groove.slotCount(); ++slot)
	{
		const GrooveStep value = groove.step(slot);
		QJsonObject entry;
		entry.insert(QStringLiteral("slot"), slot);
		entry.insert(QStringLiteral("timing"), static_cast<qint64>(value.timing));
		entry.insert(QStringLiteral("velocity"), value.velocity);
		steps.append(entry);
	}
	out.insert(QStringLiteral("steps"), steps);
	return out;
}


QJsonObject grooveWriteArgs(const GrooveTemplate& groove)
{
	QJsonObject out;
	out.insert(QStringLiteral("name"), groove.name());
	out.insert(QStringLiteral("length_ticks"), static_cast<qint64>(groove.lengthTicks()));
	out.insert(QStringLiteral("step_ticks"), static_cast<qint64>(groove.stepTicks()));
	QJsonArray steps;
	for (int slot = 0; slot < groove.slotCount(); ++slot)
	{
		const GrooveStep value = groove.step(slot);
		QJsonObject entry;
		entry.insert(QStringLiteral("slot"), slot);
		entry.insert(QStringLiteral("timing"), static_cast<qint64>(value.timing));
		entry.insert(QStringLiteral("velocity"), value.velocity);
		steps.append(entry);
	}
	out.insert(QStringLiteral("steps"), steps);
	return out;
}


QJsonArray grooveListJson(const GroovePool& pool)
{
	QJsonArray out;
	for (int index = 0; index < pool.size(); ++index)
	{
		out.append(grooveJson(pool.at(index), false));
	}
	return out;
}


QJsonObject groovePoolState(const GroovePool& pool)
{
	QJsonObject out;
	out.insert(QStringLiteral("count"), pool.size());
	out.insert(QStringLiteral("max_grooves"), GroovePool::MaxTemplates);
	out.insert(QStringLiteral("max_steps"), GrooveTemplate::MaxSteps);
	out.insert(QStringLiteral("max_name_length"), GrooveTemplate::MaxNameLength);
	out.insert(QStringLiteral("max_step_ticks"), static_cast<qint64>(GrooveTemplate::MaxStepTicks));
	out.insert(QStringLiteral("templates"), grooveListJson(pool));
	return out;
}


QJsonObject grooveStateSchema(QJsonObject extra)
{
	QJsonObject properties{
		{QStringLiteral("count"), integerProperty(0, GroovePool::MaxTemplates)},
		{QStringLiteral("max_grooves"), integerProperty(0, GroovePool::MaxTemplates)},
		{QStringLiteral("max_steps"), integerProperty(0, GrooveTemplate::MaxSteps)},
		{QStringLiteral("max_name_length"), integerProperty(0, GrooveTemplate::MaxNameLength)},
		{QStringLiteral("max_step_ticks"),
			integerProperty(0, static_cast<int>(GrooveTemplate::MaxStepTicks))},
		{QStringLiteral("templates"), arrayProperty()},
	};
	for (auto it = extra.begin(); it != extra.end(); ++it) { properties.insert(it.key(), it.value()); }
	return objectSchema(properties);
}


QJsonObject grooveStepsProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
		{QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
}


QJsonObject grooveDetailSchema()
{
	return objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("length_ticks"), tickProperty()},
		{QStringLiteral("step_ticks"),
			integerProperty(1, static_cast<int>(GrooveTemplate::MaxStepTicks))},
		{QStringLiteral("slot_count"), integerProperty(1, GrooveTemplate::MaxSteps)},
		{QStringLiteral("neutral"), booleanProperty()},
		{QStringLiteral("steps"), grooveStepsProperty()},
	});
}


bool readStrength(const QJsonObject& args, float* out, ControlResult* error)
{
	if (!args.contains(QStringLiteral("strength")))
	{
		*out = 1.0f;
		return true;
	}
	// A value that is not a number reads as -1 and is refused by the same
	// test, so a string 'strength' cannot slip through as a silent 0.
	const double value = args.value(QStringLiteral("strength")).toDouble(-1.0);
	if (value < 0.0 || value > 1.0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'strength' is %1; it is how far a note travels to the groove or "
				"to the grid, so it is a fraction in 0..1 (1 = the whole amount, 0 = nothing)")
				.arg(value));
		return false;
	}
	*out = static_cast<float>(value);
	return true;
}


bool readTicks(const QJsonObject& args, const QString& key, tick_t minimum, tick_t maximum,
	tick_t* out, ControlResult* error)
{
	const QJsonValue value = args.value(key);
	const double number = value.toDouble(-1.0);
	if (!value.isDouble() || number < static_cast<double>(minimum)
		|| number > static_cast<double>(maximum))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is %2; it is a whole number of ticks in %3..%4")
				.arg(key, QString::number(number)).arg(minimum).arg(maximum));
		return false;
	}
	*out = static_cast<tick_t>(number);
	return true;
}


MidiClip* resolveGrooveClip(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), ref, error);
	if (clip == nullptr) { return nullptr; }
	if (clip->notes().empty())
	{
		// ONE refusal for both directions, because it is one fact: there is no
		// feel in a clip with no notes, and nothing to move in one either.
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has no notes: a groove is the timing and velocity feel of a "
				"note pattern, so there is nothing here to capture or to move")
				.arg(clipId(ref->ordinal)));
		return nullptr;
	}
	return clip;
}


NoteSnapshot snapshotNotes(const MidiClip& clip)
{
	NoteSnapshot out;
	out.positions.reserve(clip.notes().size());
	out.velocities.reserve(clip.notes().size());
	for (const Note* note : clip.notes())
	{
		if (note == nullptr) { continue; }
		out.positions.push_back(note->pos().getTicks());
		out.velocities.push_back(static_cast<int>(note->getVolume()));
	}
	return out;
}


void addMoveCounts(const NoteSnapshot& before, const MidiClip& clip, QJsonObject* out)
{
	int movedPositions = 0;
	int movedVelocities = 0;
	const NoteVector& notes = clip.notes();
	const std::size_t count = std::min(before.positions.size(), notes.size());
	for (std::size_t index = 0; index < count; ++index)
	{
		const Note* note = notes[index];
		if (note == nullptr) { continue; }
		if (note->pos().getTicks() != before.positions[index]) { ++movedPositions; }
		if (static_cast<int>(note->getVolume()) != before.velocities[index]) { ++movedVelocities; }
	}
	out->insert(QStringLiteral("positions_moved"), movedPositions);
	out->insert(QStringLiteral("velocities_moved"), movedVelocities);
	out->insert(QStringLiteral("notes_moved"), std::max(movedPositions, movedVelocities));
}


QString grooveClipMechanism()
{
	return QStringLiteral("ProjectJournal (MidiClip checkpoint: the clip's note list IS its "
		"serialized state, so the checkpoint taken before the edit restores every position "
		"and velocity this command moved - the mechanism note.velocity_set reverses with)");
}

} // namespace control

} // namespace lmms
