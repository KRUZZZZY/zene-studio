/*
 * ControlCommandsLivecode.cpp - the livecode.* command group (board card #708).
 *
 * THE GROUP, AND WHY THE PREFIX IS livecode. `clock.*` is taken: the MIDI
 * clock tempo tracker owns it (include/ControlRegistryGroups.h, the
 * registerClockCommands block), so this group could not take that prefix or
 * any of its verbs. `livecode.*` is free in the registry (grep: zero hits
 * before this file) and names the thing the card asks for - LIVE CODING:
 * a script loaded once and evaluated by the clock, on every bar, beat or
 * transport edge, without anything re-triggering it.
 *
 * WHAT THE THREE COMMANDS ARE. schedule puts a script under the scheduler
 * (compile-checked, source capped, replace-in-place for a live edit);
 * unschedule takes it off; get_state reads the registry, the counters and the
 * transport observation back. The firing itself - the idle/transport hook -
 * lives in ScriptClock, off the control thread's timer, through the same
 * ScriptEngine::runString path script.run uses.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ControlRegistry.h"
#include "ScriptClock.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The inverse common to both mutating commands: this schedule command with
//! the definition that (for schedule) WAS there or (for unschedule) WAS in the
//! clock. The snapshot rows' `before` carries the same definition.
//!
//! Only the four arguments livecode.schedule's SCHEMA accepts are replayed:
//! scheduleState() reports `source_bytes` as well, and control.undo runs the
//! inverse through the command's schema, where an undeclared key is refused
//! as unexpected - so the inverse is rebuilt from the schema's own fields.
QJsonObject scheduleInverse(const QJsonObject& definition)
{
	QJsonObject args;
	args.insert(QStringLiteral("id"), definition.value(QStringLiteral("id")));
	args.insert(QStringLiteral("source"), definition.value(QStringLiteral("source")));
	args.insert(QStringLiteral("hook"), definition.value(QStringLiteral("hook")));
	args.insert(QStringLiteral("budget"), definition.value(QStringLiteral("budget")));
	return QJsonObject{{QStringLiteral("op"), QStringLiteral("livecode.schedule")},
		{QStringLiteral("applies"), QStringLiteral("command")},
		{QStringLiteral("args"), args}};
}

QJsonObject unscheduleInverse(const QString& id)
{
	return QJsonObject{{QStringLiteral("op"), QStringLiteral("livecode.unschedule")},
		{QStringLiteral("applies"), QStringLiteral("command")},
		{QStringLiteral("args"), QJsonObject{{QStringLiteral("id"), id}}}};
}

//! The A16 mechanism sentence each snapshot row records, for the three moves
//! the two mutating commands make: a schedule replacing one that was there,
//! a schedule for a brand-new id, and an unschedule taking one off.
QString mechanismReplacing()
{
	return QStringLiteral("snapshot: before holds the schedule this call replaced and the "
		"recorded inverse is livecode.schedule with that definition, so one control.undo "
		"puts the old script back under the same id (its fire counters restart; the "
		"definition, not the history, is what a snapshot restores).");
}

QString mechanismFresh()
{
	return QStringLiteral("snapshot: before holds that no schedule carried this id and the "
		"recorded inverse is livecode.unschedule with it, so one control.undo takes the "
		"schedule off the clock again.");
}

QString mechanismRemoved()
{
	return QStringLiteral("snapshot: before holds the definition this call removed and the "
		"recorded inverse is livecode.schedule with that definition, so one control.undo "
		"rearms the id where it was (its fire counters start over: a snapshot restores the "
		"definition, not the history).");
}

//! Exactly one of 'path' and 'source', read the way script.run reads it: a
//! path must exist and be readable, an inline source is taken as it stands.
//! false comes back with \a failure carrying the typed refusal.
bool loadScheduledSource(const QJsonObject& args, QString* source, ControlResult* failure)
{
	const bool hasSource = args.contains(QStringLiteral("source"));
	const bool hasPath = args.contains(QStringLiteral("path"));
	if (hasSource == hasPath)
	{
		*failure = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("exactly one of 'path' and 'source' is required"));
		return false;
	}
	if (!hasPath)
	{
		*source = args.value(QStringLiteral("source")).toString();
		return true;
	}
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!QFileInfo(path).exists())
	{
		*failure = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no Lua script at '%1' (a path argument must be absolute)").arg(path));
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*failure = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("cannot read the Lua script at '%1'").arg(path));
		return false;
	}
	*source = QString::fromUtf8(file.readAll());
	return true;
}

/*! livecode.schedule - put a script on the clock.
 *
 * Reads a file or an inline source exactly the way script.run does (one of
 * the two, never both), but instead of RUNNING it, hands it to the scheduler:
 * the source is compiled once (a parse, not an execution), stored, and from
 * then on the boundary it was armed for does the running - `hook` decides
 * whether that is every bar, every beat, or each transport edge. `budget`
 * works the way script.run's does: per-fire instruction bound, 0 meaning
 * "inherit the engine's budget when the fire happens".
 */
ControlResult livecodeSchedule(const QJsonObject& args)
{
	QString source;
	ControlResult refusal = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("the source could not be loaded"));
	if (!loadScheduledSource(args, &source, &refusal)) { return refusal; }

	const QString hook = args.value(QStringLiteral("hook")).toString(QStringLiteral("bar"));
	const int budget = args.value(QStringLiteral("budget")).toInt(0);
	const QString id = args.contains(QStringLiteral("id"))
		? args.value(QStringLiteral("id")).toString()
		: ScriptClock::instance().newScheduleId();

	// What the clock held under this id BEFORE this call: the snapshot's
	// before-state and the thing the inverse puts back.
	const QJsonObject previous = ScriptClock::instance().scheduleState(id);
	const bool replacing = !previous.isEmpty();

	QString error;
	if (!ScriptClock::instance().setSchedule(id, source, hook, budget, &error))
	{
		// The typed refusals the schema cannot make: a bad hook is caught by
		// enumProperty above, this catches what only the parser can see (or a
		// source the cap or the empty check refused).
		return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
	}

	QJsonObject out;
	out.insert(QStringLiteral("id"), id);
	out.insert(QStringLiteral("hook"), hook);
	out.insert(QStringLiteral("budget"), budget);
	out.insert(QStringLiteral("source_bytes"), static_cast<qint64>(source.size()));
	out.insert(QStringLiteral("replaced"), replacing);
	out.insert(QStringLiteral("scheduled"), true);
	out.insert(QStringLiteral("__transaction"),
		QJsonObject{
			{QStringLiteral("before"),
				replacing ? previous : QJsonObject{{QStringLiteral("exists"), false}}},
			{QStringLiteral("inverse"),
				replacing ? scheduleInverse(previous) : unscheduleInverse(id)},
			{QStringLiteral("reversible"), true},
			{QStringLiteral("mechanism"), replacing ? mechanismReplacing() : mechanismFresh()}});
	return ControlResult::success(out);
}

/*! livecode.unschedule - take a script off the clock. The snapshot's
 *  before-state is the whole definition, so the inverse (schedule with it)
 *  rearms the id exactly where it was.
 */
ControlResult livecodeUnschedule(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("id")).toString();
	if (id.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the schedule 'id' is required and must not be empty"));
	}
	const QJsonObject previous = ScriptClock::instance().scheduleState(id);
	if (previous.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no scheduled script '%1'").arg(id));
	}
	QString error;
	if (!ScriptClock::instance().removeSchedule(id, &error))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, error);
	}
	QJsonObject out;
	out.insert(QStringLiteral("id"), id);
	out.insert(QStringLiteral("unscheduled"), true);
	out.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), previous},
			{QStringLiteral("inverse"), scheduleInverse(previous)},
			{QStringLiteral("reversible"), true},
			{QStringLiteral("mechanism"), mechanismRemoved()}});
	return ControlResult::success(out);
}

/*! livecode.get_state - the schedule registry, the fire counters, and the
 *  transport observation the grid is working from. Reads nothing the audio
 *  thread writes to memory it owns: the position here is the same Song accessor
 *  transport.get_state reports, read from the control thread.
 */
ControlResult livecodeGetState()
{
	return ControlResult::success(ScriptClock::instance().stateJson());
}

} // namespace

void registerLivecodeCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("livecode.schedule");
		cmd.group = QStringLiteral("livecode");
		cmd.verb = QStringLiteral("schedule");
		cmd.description = QStringLiteral("Put a Lua script on the schedule clock: from then on "
			"the engine evaluates it on its own - 'hook' = 'bar' (default), 'beat' or "
			"'transport' - with NO re-trigger, exactly as the scheduled hook fires (board card "
			"#708; docs/LUA-SCRIPTING.md). Exactly one of 'path' (absolute) and 'source', like "
			"script.run; the source is compiled once (parsed, not run) and a syntax error is "
			"refused HERE, not on the first fire. 'budget' bounds each fire's instructions "
			"(0 = inherit the engine's). Scheduling an id that exists REPLACES its script - the "
			"live edit - and keeps its counters. Reversible: one control.undo restores the "
			"previous definition, or removes the schedule if there was none.");
		cmd.mutating = true;
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("path"), control::stringProperty()},
			{QStringLiteral("source"), control::stringProperty()},
			{QStringLiteral("hook"), control::enumProperty({QStringLiteral("bar"),
				QStringLiteral("beat"), QStringLiteral("transport")})},
			{QStringLiteral("budget"), control::integerProperty(0, 0x7fffffff)},
			{QStringLiteral("id"), control::stringProperty()},
		});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("id"), control::stringProperty()},
			{QStringLiteral("hook"), control::enumProperty({QStringLiteral("bar"),
				QStringLiteral("beat"), QStringLiteral("transport")})},
			{QStringLiteral("budget"), control::integerProperty(0, 0x7fffffff)},
			{QStringLiteral("source_bytes"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("replaced"), control::booleanProperty()},
			{QStringLiteral("scheduled"), control::booleanProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return livecodeSchedule(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("livecode.unschedule");
		cmd.group = QStringLiteral("livecode");
		cmd.verb = QStringLiteral("unschedule");
		cmd.description = QStringLiteral("Take a script off the schedule clock; the clock stops "
			"evaluating that id on its hook. Reversible: one control.undo rearms the same id "
			"with the definition this call removed (its fire counters start over - a snapshot "
			"restores the definition, not the history).");
		cmd.mutating = true;
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("id"), control::stringProperty()},
		}, {QStringLiteral("id")});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("id"), control::stringProperty()},
			{QStringLiteral("unscheduled"), control::booleanProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return livecodeUnschedule(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("livecode.get_state");
		cmd.group = QStringLiteral("livecode");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Everything the schedule clock holds: every schedule "
			"(id, hook, budget, source bytes) with its fire/error/budget-exceeded counters and "
			"last typed failure, the aggregate counters (fires, errors, budget_exceeded, "
			"busy_skips, regrids, coalesced), and the transport observation the grid is "
			"working from (poll_ms, position, next bar/beat ticks). Reads only - it schedules "
			"nothing and fires nothing.");
		cmd.mutating = false;
		cmd.argsSchema = control::objectSchema({});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("poll_ms"), control::integerProperty(0, 1000000)},
			{QStringLiteral("playing"), control::booleanProperty()},
			{QStringLiteral("position_ticks"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("ticks_per_bar"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("ticks_per_beat"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("next_bar_ticks"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("next_beat_ticks"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("count"), control::integerProperty(0, 0x7fffffff)},
			{QStringLiteral("schedules"), QJsonObject{{QStringLiteral("type"),
				QStringLiteral("array")}}},
			{QStringLiteral("counters"), QJsonObject{{QStringLiteral("type"),
				QStringLiteral("object")}}},
		});
		cmd.handler = [](const QJsonObject&) { return livecodeGetState(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
