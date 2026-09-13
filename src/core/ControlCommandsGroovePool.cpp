/*
 * ControlCommandsGroovePool.cpp - the POOL half of the groove.* command
 *                                 group's mutating verbs (SPEC A11-A16):
 *                                 groove.set / groove.remove / groove.rename.
 *
 * These three edit the project's groove POOL and nothing else, and they are
 * kept apart from the clip-editing verbs (groove.apply / groove.quantize, in
 * ControlCommandsGrooveEdit.cpp) because their inverses are different KINDS:
 * a pool edit has no live object a journal checkpoint could restore, so its
 * inverse is a recorded ACTION checkpoint (control::addUndoStep) that writes
 * the captured <groove-pool> element back - the same mechanism the tempo map
 * and the modulation layer use, because the pool is project state the Song
 * checkpoint (which carries the track container) does not hold.
 * docs/GROOVE-POOL.md section 5 states the finding.
 *
 * Every refusal happens BEFORE the before-state is captured and before anything
 * is written: a refused call leaves no undo step behind and no half-edited pool.
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
#include <QString>

#include "ControlGrooveSupport.h"
#include "ControlRegistry.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

namespace
{

/*! The steps of a groove.set as the engine's own value type sees them.
 *
 *  A slot that is out of range, or a timing offset past half the slot width, is
 *  refused - and the whole call is refused with it, before anything is written,
 *  so a malformed step list cannot leave a half-written groove behind.
 */
bool readStepList(const QJsonArray& steps, GrooveTemplate* out, ControlResult* error)
{
	for (const QJsonValue& value : steps)
	{
		if (!value.isObject())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("every entry of 'steps' is an object of the form "
					"{\"slot\": <integer>, \"timing\": <integer ticks>, "
					"\"velocity\": <integer offset>}"));
			return false;
		}
		const QJsonObject entry = value.toObject();
		GrooveStep step;
		step.timing = static_cast<tick_t>(entry.value(QStringLiteral("timing")).toDouble(0.0));
		step.velocity = static_cast<int>(entry.value(QStringLiteral("velocity")).toDouble(0.0));
		const double slot = entry.value(QStringLiteral("slot")).toDouble(-1.0);
		if (!out->setStep(static_cast<int>(slot), step))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("slot %1 of '%2' is out of range or out of bounds: a slot is "
					"0..%3, a timing offset at most half the slot width, and a velocity in "
					"0..%4").arg(slot).arg(out->name())
					.arg(out->slotCount() - 1).arg(MaxVolume));
			return false;
		}
	}
	return true;
}


//! The geometry refusal, as one message: what a writable groove is.
ControlResult geometryRefusal(const QString& name, tick_t length, tick_t stepTicks)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' over %2 ticks in slots of %3 is not a groove this engine holds: "
			"the name is 1..%4 characters, the slot 1..%5 ticks, and the length a whole "
			"1..%6 slots").arg(name).arg(length).arg(stepTicks)
			.arg(GrooveTemplate::MaxNameLength).arg(GrooveTemplate::MaxStepTicks)
			.arg(GrooveTemplate::MaxSteps));
}


//! Reads the whole body of a groove.set: the geometry and the steps.
bool readGrooveBody(const QJsonObject& args, GrooveTemplate* out, ControlResult* error)
{
	const QString name =
		GrooveTemplate::normalisedName(args.value(QStringLiteral("name")).toString());
	tick_t stepTicks = 0;
	tick_t length = 0;
	const tick_t maxLength = static_cast<tick_t>(GrooveTemplate::MaxSteps)
		* GrooveTemplate::MaxStepTicks;
	if (!readTicks(args, QStringLiteral("step_ticks"), GrooveTemplate::MinStepTicks,
		GrooveTemplate::MaxStepTicks, &stepTicks, error))
	{
		return false;
	}
	if (!readTicks(args, QStringLiteral("length_ticks"), GrooveTemplate::MinStepTicks, maxLength,
		&length, error))
	{
		return false;
	}
	if (!GrooveTemplate::isWritable(name, length, stepTicks))
	{
		*error = geometryRefusal(name, length, stepTicks);
		return false;
	}

	*out = GrooveTemplate(name, length, stepTicks);
	if (!args.contains(QStringLiteral("steps"))) { return true; }
	return readStepList(args.value(QStringLiteral("steps")).toArray(), out, error);
}


/*! The pool's read-back after a pool edit, with the one groove it touched: a
 *  caller never has to guess what the pool became. */
QJsonObject groovePoolResult(const GroovePool& pool, const GrooveTemplate& touched)
{
	QJsonObject out = groovePoolState(pool);
	out.insert(QStringLiteral("groove"), grooveJson(touched, true));
	return out;
}


//! The refusal a full pool gives a NEW name (replacing one is always allowed).
ControlResult poolFullRefusal(const QString& name)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the pool already holds its maximum of %1 grooves and no groove called "
			"'%2' to replace; remove one first")
			.arg(GroovePool::MaxTemplates).arg(name));
}


ControlResult grooveSet(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to write"));
	}
	ControlResult error;
	GrooveTemplate wanted;
	if (!readGrooveBody(args, &wanted, &error)) { return error; }
	if (pool->find(wanted.name()) == nullptr && pool->size() >= GroovePool::MaxTemplates)
	{
		return poolFullRefusal(wanted.name());
	}

	const QString before = pool->toXml();
	// The previous groove is captured BY VALUE before the write: find() hands
	// back a pointer into the pool's own vector, and inserting a new groove can
	// move it (the "capture values, never a pointer" rule the modulation
	// layer's recorded steps follow).
	const GrooveTemplate* existing = pool->find(wanted.name());
	const bool hadPrevious = existing != nullptr;
	const GrooveTemplate previous = hadPrevious ? *existing : GrooveTemplate();
	bool replaced = false;
	if (!pool->set(wanted, &replaced))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the pool refused '%1'; nothing was written").arg(wanted.name()));
	}
	recordGroovePoolRestore(before);

	QJsonObject result = groovePoolResult(*pool, wanted);
	result.insert(QStringLiteral("replaced"), replaced);
	result.insert(QStringLiteral("__transaction"), groovePoolInverse(before,
		hadPrevious ? QStringLiteral("groove.set") : QStringLiteral("groove.remove"),
		hadPrevious ? grooveWriteArgs(previous)
			: QJsonObject{{QStringLiteral("name"), wanted.name()}}));
	return ControlResult::success(result);
}


ControlResult grooveRemove(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to write"));
	}
	const QString name =
		GrooveTemplate::normalisedName(args.value(QStringLiteral("name")).toString());
	const GrooveTemplate* existing = pool->find(name);
	if (existing == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no groove called '%1' (the pool holds %2)")
				.arg(name).arg(pool->size()));
	}
	const GrooveTemplate removed = *existing;

	const QString before = pool->toXml();
	if (!pool->remove(name))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' could not be removed; the pool is unchanged").arg(name));
	}
	recordGroovePoolRestore(before);

	QJsonObject result = groovePoolResult(*pool, removed);
	result.insert(QStringLiteral("removed"), name);
	// The named inverse is a REAL command: groove.set with the removed groove's
	// own content rebuilds it, and the recorded checkpoint puts it back at its
	// own position in the pool.
	result.insert(QStringLiteral("__transaction"),
		groovePoolInverse(before, QStringLiteral("groove.set"), grooveWriteArgs(removed)));
	return ControlResult::success(result);
}


ControlResult grooveRename(const QJsonObject& args)
{
	GroovePool* pool = projectGroovePool();
	if (pool == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the song is not up yet, so there is no groove pool to write"));
	}
	const QString name =
		GrooveTemplate::normalisedName(args.value(QStringLiteral("name")).toString());
	const QString to = GrooveTemplate::normalisedName(
		args.value(QStringLiteral("to")).toString());
	if (name.isEmpty() || to.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("groove.rename needs 'name' and the 'to' name it becomes (1..%1 "
				"characters each)").arg(GrooveTemplate::MaxNameLength));
	}
	const GrooveTemplate* existing = pool->find(name);
	if (existing == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no groove called '%1' (the pool holds %2)")
				.arg(name).arg(pool->size()));
	}
	if (to != name && pool->find(to) != nullptr)
	{
		// The name is the key, so renaming onto another groove's name would
		// destroy it. Refused, typed, nothing written (an edit that silently
		// deletes something nobody named is not an edit - I4).
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the pool already holds a groove called '%1'; rename it first, or "
				"choose another name").arg(to));
	}
	const GrooveTemplate renamed = *existing;

	const QString before = pool->toXml();
	if (!pool->rename(name, to))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' could not be renamed to '%2'; the pool is unchanged")
				.arg(name, to));
	}
	recordGroovePoolRestore(before);

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("name"), to);
	inverseArgs.insert(QStringLiteral("to"), name);

	QJsonObject result = groovePoolResult(*pool, renamed);
	result.insert(QStringLiteral("renamed"), name);
	result.insert(QStringLiteral("renamed_to"), to);
	result.insert(QStringLiteral("__transaction"),
		groovePoolInverse(before, QStringLiteral("groove.rename"), inverseArgs));
	return ControlResult::success(result);
}


//! One registration step per verb, because a description this long beside its
//! schema buries both.
void registerGrooveSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("groove.set");
	cmd.group = QStringLiteral("groove");
	cmd.verb = QStringLiteral("set");
	cmd.description = QStringLiteral("Write a groove VERBATIM: its name, its cycle length and "
		"slot width in ticks, and (optionally) each slot's timing offset and velocity - "
		"so a groove can be authored by hand as well as captured by groove.extract, and so an "
		"extract that replaced a groove has a real inverse to record. The name is the key: an "
		"existing groove of that name is replaced. One undoable step (a recorded action "
		"checkpoint that writes the captured pool back).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("length_ticks"), integerProperty(GrooveTemplate::MinStepTicks,
			static_cast<int>(GrooveTemplate::MaxSteps)
				* static_cast<int>(GrooveTemplate::MaxStepTicks))},
		{QStringLiteral("step_ticks"), integerProperty(GrooveTemplate::MinStepTicks,
			static_cast<int>(GrooveTemplate::MaxStepTicks))},
		{QStringLiteral("steps"), grooveStepsProperty()},
	}, {QStringLiteral("name"), QStringLiteral("length_ticks"), QStringLiteral("step_ticks")});
	cmd.resultSchema = grooveStateSchema({
		{QStringLiteral("groove"), grooveDetailSchema()},
		{QStringLiteral("replaced"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return grooveSet(args); };
	registry.registerCommand(cmd);
}


void registerGrooveRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("groove.remove");
	cmd.group = QStringLiteral("groove");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete a named groove from the project's pool, leaving "
		"the rest in place. Removing the last groove leaves the pool empty, which is the state "
		"a project that never captured one saves in (no <groove-pool> element at all). "
		"Reversible through a recorded action checkpoint.");
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}},
		{QStringLiteral("name")});
	cmd.resultSchema = grooveStateSchema({
		{QStringLiteral("groove"), grooveDetailSchema()},
		{QStringLiteral("removed"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return grooveRemove(args); };
	registry.registerCommand(cmd);
}


void registerGrooveRename(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("groove.rename");
	cmd.group = QStringLiteral("groove");
	cmd.verb = QStringLiteral("rename");
	cmd.description = QStringLiteral("Rename a groove, keeping its steps and its position in "
		"the pool. A rename onto another groove's name is refused, typed, and changes nothing "
		"- the name is the key, so it would destroy that groove. Reversible through a recorded "
		"action checkpoint.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("to"), stringProperty()},
	}, {QStringLiteral("name"), QStringLiteral("to")});
	cmd.resultSchema = grooveStateSchema({
		{QStringLiteral("groove"), grooveDetailSchema()},
		{QStringLiteral("renamed"), stringProperty()},
		{QStringLiteral("renamed_to"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return grooveRename(args); };
	registry.registerCommand(cmd);
}

} // namespace


} // namespace control

void registerGroovePoolCommands(ControlRegistry& registry)
{
	registerGrooveSet(registry);
	registerGrooveRemove(registry);
	registerGrooveRename(registry);
}

} // namespace lmms
