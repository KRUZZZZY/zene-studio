/*
 * ControlCommandsUndo.cpp - the bounded-undo slice of the control.* group:
 *                           control.undo_depth, control.set_undo_depth and
 *                           control.set_undo_coalescing (SPEC A16's "undo depth
 *                           and drag coalescing" obligation, task #623).
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

/*
 * WHY THESE THREE COMMANDS ARE THE FEATURE AND NOT A WRAPPER AROUND IT
 *
 * The engine half (the two-sided bound and the coalescing primitive) is
 * ProjectJournal; the rule that decides which commands coalesce is DATA in the
 * contract table. Neither is observable or controllable by an agent until it is
 * on the wire, and the scope contract of this release says a capability that
 * cannot be driven through the socket is not in the release:
 *
 *   control.undo_depth          READ. The depth, the two caps, the bytes
 *                               retained, how many steps a bound has evicted and
 *                               the coalescing rule in force - everything a
 *                               client needs to reason about its own undo
 *                               history before it needs to undo anything.
 *   control.set_undo_depth      SET the two caps. Mutating (lowering a cap
 *                               evicts steps, which is a destructive act with a
 *                               recorded inverse for the CAP but not for what
 *                               it dropped - see docs/UNDO-BOUNDS.md).
 *   control.set_undo_coalescing SET the window the grouping happens in, 0 to
 *                               switch grouping OFF. Not mutating: it is
 *                               control-surface grouping state, it writes
 *                               nothing into the project, and it cannot change
 *                               anything already on the stack.
 *
 * The undo itself is NOT re-implemented here: control.undo/control.redo and the
 * GUI's Ctrl+Z stay the one implementation (Edit > Undo calls
 * ProjectJournal::undo(), and so does the command). A parallel undo stack is
 * exactly the defect SPEC A16 exists to remove.
 */

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlUndoCoalescing.h"
#include "Engine.h"
#include "ProjectJournal.h"

namespace lmms
{
namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

//! The coalescing rule as a client reads it: the window in force, whether it is
//! on, the rule in one sentence, and WHICH commands it applies to (read from the
//! contract table rather than restated here - one definition).
QJsonObject coalescingState(const ControlRegistry& registry)
{
	QJsonObject out;
	const int window = registry.coalesceWindowMs();
	out.insert(QStringLiteral("window_ms"), window);
	out.insert(QStringLiteral("enabled"), window > 0);
	out.insert(QStringLiteral("rule"), coalescingRuleText());
	out.insert(QStringLiteral("commands"), QJsonArray::fromStringList(coalescingCommands()));
	return out;
}

//! The engine's own view of its undo stack. Every field is always present (0 /
//! false when there is no journal at all), because a client may poll this before
//! the engine exists and a checker cannot read optional keys.
QJsonObject boundsReport(ProjectJournal* journal)
{
	QJsonObject out;
	const bool live = journal != nullptr;
	const int evicted = live ? journal->evictedSteps() : 0;
	out.insert(QStringLiteral("depth"), live ? journal->undoDepth() : 0);
	out.insert(QStringLiteral("redo_depth"), live ? journal->redoDepth() : 0);
	out.insert(QStringLiteral("cap_steps"), live ? journal->maxUndoStates() : ProjectJournal::MAX_UNDO_STATES);
	out.insert(QStringLiteral("cap_bytes"), live ? journal->maxUndoBytes()
												: ProjectJournal::DefaultMaxUndoBytes);
	out.insert(QStringLiteral("retained_bytes"), live ? journal->retainedBytes() : 0);
	out.insert(QStringLiteral("evicted"), evicted);
	// `bounded` says out loud that a bound has actually DROPPED something, so a
	// client can tell "this is the whole history" from "this is what the bound
	// retains" - the same rule the transaction record follows.
	out.insert(QStringLiteral("bounded"), evicted > 0);
	out.insert(QStringLiteral("coalesced_steps"), live ? journal->coalescedSteps() : 0);
	out.insert(QStringLiteral("can_undo"), live && journal->canUndo());
	out.insert(QStringLiteral("can_redo"), live && journal->canRedo());
	return out;
}

//! The typed refusal for a cap outside the range this engine will honour. A cap
//! that can be set to a billion is not a bound, so the ceilings are named.
ControlResult capRefused(const QString& what, qint64 value, qint64 high)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("control.set_undo_depth: %1 %2 is outside [1, %3]; the cap is a bound that is "
			"enforced, so it is refused rather than clamped silently")
			.arg(what).arg(value).arg(high));
}

ControlResult handleUndoDepth(const ControlRegistry& registry)
{
	QJsonObject result = boundsReport(Engine::projectJournal());
	result.insert(QStringLiteral("coalescing"), coalescingState(registry));
	return ControlResult::success(result);
}

/*! control.set_undo_depth: set either cap, or both.
 *
 * The transaction is a SNAPSHOT whose inverse is the command itself
 * (`applies: command`), so one control.undo puts the caps back. What it cannot
 * undo is named in the mechanism: the steps a lower cap evicted are gone, and
 * the result reports how many that cost (`dropped`), so a client is never left
 * guessing why its history is shorter than it was.
 */
ControlResult handleSetUndoDepth(const QJsonObject& args)
{
	ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("control.set_undo_depth: the engine's undo stack is not addressable yet"));
	}

	const bool wantSteps = args.contains(QStringLiteral("steps"));
	const bool wantBytes = args.contains(QStringLiteral("bytes"));
	if (!wantSteps && !wantBytes)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("control.set_undo_depth needs 'steps', 'bytes', or both; with neither there "
				"is nothing to set"));
	}

	const int previousSteps = journal->maxUndoStates();
	const qint64 previousBytes = journal->maxUndoBytes();
	const int evictedBefore = journal->evictedSteps();

	if (wantSteps)
	{
		const int steps = args.value(QStringLiteral("steps")).toInt();
		if (!journal->setMaxUndoStates(steps))
		{
			return capRefused(QStringLiteral("steps"), steps, ProjectJournal::MaxUndoStateLimit);
		}
	}
	if (wantBytes)
	{
		const qint64 bytes = static_cast<qint64>(args.value(QStringLiteral("bytes")).toDouble());
		if (!journal->setMaxUndoBytes(bytes))
		{
			return capRefused(QStringLiteral("bytes"), bytes, ProjectJournal::MaxUndoByteLimit);
		}
	}

	QJsonObject result = boundsReport(journal);
	result.insert(QStringLiteral("previous_steps"), previousSteps);
	result.insert(QStringLiteral("previous_bytes"), previousBytes);
	result.insert(QStringLiteral("dropped"), journal->evictedSteps() - evictedBefore);

	// The inverse restores the CAPS, and says in as many words that the steps a
	// lower cap evicted are not part of what it restores.
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"),
						QJsonObject{{QStringLiteral("steps"), previousSteps},
							{QStringLiteral("bytes"), previousBytes}}},
			{QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("control.set_undo_depth")},
					{QStringLiteral("applies"), QStringLiteral("command")},
					{QStringLiteral("args"),
						QJsonObject{{QStringLiteral("steps"), previousSteps},
							{QStringLiteral("bytes"), previousBytes}}}}},
			{QStringLiteral("reversible"), true},
			{QStringLiteral("mechanism"),
				QStringLiteral("snapshot: the previous caps are in before and the recorded inverse is "
					"the command itself, so one control.undo puts them back. Lowering a cap evicts the "
					"oldest steps there and then; THOSE are not recoverable - the inverse restores the "
					"cap, not the history it dropped (docs/UNDO-BOUNDS.md)")}});
	return ControlResult::success(result);
}

//! control.set_undo_coalescing: the window a same-command-same-target run is
//! grouped in. 0 switches grouping off, which is the pre-0.3.0 behaviour
//! (one undo step per command) and is the honest way to ask for it.
ControlResult handleSetUndoCoalescing(ControlRegistry& registry, const QJsonObject& args)
{
	const int window = args.value(QStringLiteral("window_ms")).toInt();
	if (!registry.setCoalesceWindowMs(window))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("control.set_undo_coalescing: window_ms %1 is outside [0, %2]. 0 is legal "
				"and means 'coalesce nothing'")
				.arg(window).arg(MaxUndoCoalesceWindowMs));
	}
	QJsonObject result;
	result.insert(QStringLiteral("window_ms"), registry.coalesceWindowMs());
	result.insert(QStringLiteral("enabled"), registry.coalesceWindowMs() > 0);
	// A window change is a BOUNDARY: whatever gesture was in flight is not
	// continued across it, so changing the window can never retroactively group
	// two gestures into one step.
	result.insert(QStringLiteral("coalescing"), coalescingState(registry));
	return ControlResult::success(result);
}

void registerUndoDepthCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.undo_depth");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("undo_depth");
	cmd.description = QStringLiteral("Read the engine's own undo stack: its depth and redo depth, the "
		"TWO bounds it is kept within (a count cap and a byte budget), the serialised bytes it retains, "
		"how many steps a bound has evicted, and the coalescing rule in force (the window, and which "
		"commands it groups). Writes nothing.");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("depth"), integerProperty()},
		{QStringLiteral("redo_depth"), integerProperty()},
		{QStringLiteral("cap_steps"), integerProperty()},
		{QStringLiteral("cap_bytes"), integerProperty()},
		{QStringLiteral("retained_bytes"), integerProperty()},
		{QStringLiteral("evicted"), integerProperty()},
		{QStringLiteral("bounded"), booleanProperty()},
		{QStringLiteral("coalesced_steps"), integerProperty()},
		{QStringLiteral("can_undo"), booleanProperty()},
		{QStringLiteral("can_redo"), booleanProperty()},
		// {window_ms, enabled, rule, commands[]}
		{QStringLiteral("coalescing"), objectProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) { return handleUndoDepth(registry); };
	registry.registerCommand(cmd);
}

void registerSetUndoDepthCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.set_undo_depth");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("set_undo_depth");
	cmd.description = QStringLiteral("Set the undo stack's two caps: `steps` (how many undo steps are "
		"kept) and/or `bytes` (the serialised size they may occupy). Lowering a cap EVICTS the oldest "
		"steps immediately and the result reports how many (`dropped`); the caps themselves are "
		"reversible (one control.undo restores them), the evicted steps are not. Refuses a value "
		"outside the declared ceilings rather than clamping it.");
	cmd.mutating = true;
	cmd.argsSchema = objectSchema({
		{QStringLiteral("steps"), integerProperty(1, ProjectJournal::MaxUndoStateLimit)},
		{QStringLiteral("bytes"), integerProperty(1, static_cast<int>(ProjectJournal::MaxUndoByteLimit))},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("cap_steps"), integerProperty()},
		{QStringLiteral("cap_bytes"), integerProperty()},
		{QStringLiteral("previous_steps"), integerProperty()},
		{QStringLiteral("previous_bytes"), integerProperty()},
		{QStringLiteral("depth"), integerProperty()},
		{QStringLiteral("retained_bytes"), integerProperty()},
		{QStringLiteral("dropped"), integerProperty()},
		{QStringLiteral("evicted"), integerProperty()},
		{QStringLiteral("bounded"), booleanProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return handleSetUndoDepth(args); };
	registry.registerCommand(cmd);
}

void registerSetUndoCoalescingCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.set_undo_coalescing");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("set_undo_coalescing");
	cmd.description = QStringLiteral("Set the window (milliseconds) inside which a run of the same "
		"command on the same target is ONE undo step, so a 200-call drag costs one Ctrl+Z. 0 disables "
		"grouping entirely, which is the pre-0.3.0 behaviour. Changing the window ends the gesture in "
		"flight, so it can never group two gestures retroactively. Does not touch the project.");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema({
		{QStringLiteral("window_ms"), integerProperty(0, MaxUndoCoalesceWindowMs)},
	}, {QStringLiteral("window_ms")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("window_ms"), integerProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("coalescing"), objectProperty()},
	});
	cmd.handler = [&registry](const QJsonObject& args) {
		return handleSetUndoCoalescing(registry, args);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerUndoBoundsCommands(ControlRegistry& registry)
{
	registerUndoDepthCommand(registry);
	registerSetUndoDepthCommand(registry);
	registerSetUndoCoalescingCommand(registry);
}

} // namespace lmms
