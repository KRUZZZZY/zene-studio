/*
 * ControlCommandsSessionPatterns.cpp - session.pattern_list: the patterns a launcher cell can play
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

/*! WF-054 (0.5.0): a MIDI launcher cell plays a PATTERN - the pattern editor's numbered patterns,
 *  `session.set_slot {type: "midi", pattern}` - and a person choosing one needs their names, not
 *  their numbers. Read-only. */

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PatternStore.h"
#include "PatternTrack.h"

namespace lmms
{

using namespace control;

void registerSessionPatternCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.pattern_list");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("pattern_list");
	cmd.description = QStringLiteral("The patterns a MIDI launcher cell can play: each pattern's number - "
		"session.set_slot's 'pattern' - and the name of its pattern track. Read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("patterns"), arrayProperty()}, {QStringLiteral("count"), integerProperty()}});
	cmd.handler = [](const QJsonObject&) {
		QJsonArray patterns;
		PatternStore* store = Engine::patternStore();
		const int count = store != nullptr ? store->numOfPatterns() : 0;
		for (int pattern = 0; pattern < count; ++pattern)
		{
			const PatternTrack* track = PatternTrack::findPatternTrack(pattern);
			patterns.append(QJsonObject{{QStringLiteral("pattern"), pattern},
				{QStringLiteral("name"), track != nullptr ? track->name() : QString()}});
		}
		return ControlResult::success({{QStringLiteral("patterns"), patterns}, {QStringLiteral("count"), patterns.size()}});
	};
	registry.registerCommand(cmd);
}

} // namespace lmms
