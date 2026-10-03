/*
 * ControlCommandsHistoryJump.cpp - control.history_jump: several undo or redo steps at once
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

/*! 0.5.0 W3 L13 (WF-102 undo history): going back to a point in the history in one go - the Undo History
 *  panel's double-click. It is control.undo (or control.redo) run 'steps' times, so every rule of those
 *  holds for each step: an irreversible command stops the walk, typed, where it stands, and a coalesced run
 *  is one step. It records nothing of its own. */

#include <cstdlib>

#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxJump = 1000;

//! Run @a step (control.undo or control.redo) up to @a count times, stopping at the first that did nothing
//! (its reply's @a done flag false) or failed; how many took, and the last reply.
int walk(const QString& step, const QString& done, int count, ControlResult* last)
{
	int taken = 0;
	while (taken < count)
	{
		*last = ControlRegistry::instance()->invoke(step);
		if (!last->ok || !last->result.value(done).toBool()) { break; }
		++taken;
	}
	return taken;
}

ControlResult historyJump(const QJsonObject& args)
{
	const int steps = args.value(QStringLiteral("steps")).toInt();
	if (steps == 0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'steps' is negative to undo, positive to redo")); }
	const bool back = steps < 0;
	ControlResult last = ControlResult::success();
	const int taken = walk(back ? QStringLiteral("control.undo") : QStringLiteral("control.redo"),
		back ? QStringLiteral("undone") : QStringLiteral("redone"), std::abs(steps), &last);
	if (taken == 0 && !last.ok) { return last; }
	return ControlResult::success({{QStringLiteral("steps"), back ? -taken : taken}, {QStringLiteral("asked"), steps},
		{QStringLiteral("stopped"), last.ok ? QString() : last.errorMessage}});
}

} // namespace

void registerHistoryJumpCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.history_jump");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("history_jump");
	cmd.description = QStringLiteral("Walk the undo history several steps at once: 'steps' negative undoes that many, positive "
		"redoes them - control.undo / control.redo run in turn, so an irreversible command stops the walk where it stands "
		"('stopped' says why) and a coalesced run is one step. 'steps' in the reply is how far it went.");
	cmd.argsSchema = objectSchema({{QStringLiteral("steps"), integerProperty(-MaxJump, MaxJump)}}, {QStringLiteral("steps")});
	cmd.resultSchema = objectProperty();
	cmd.handler = historyJump;
	registry.registerCommand(cmd);
}

} // namespace lmms
