/*
 * ControlCommandDef.cpp - the one way a command file registers a plain command
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
#include "ControlCommandDef.h"

#include <QJsonArray>

#include "ControlVocabulary.h"

namespace lmms::control
{

void defineCommandWithSchema(ControlRegistry& registry, const QString& id, const QString& description,
	const QJsonObject& argsSchema, bool mutating, std::function<ControlResult(const QJsonObject&)> handler, bool requiresEngine)
{
	ControlCommand cmd;
	cmd.id = id;
	const qsizetype dot = id.indexOf(QLatin1Char('.'));
	cmd.group = id.left(dot);
	cmd.verb = id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = argsSchema;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.requiresEngine = requiresEngine;
	cmd.handler = std::move(handler);
	registry.registerCommand(cmd);
}

void defineCommand(ControlRegistry& registry, const QString& id, const QString& description,
	const QJsonObject& args, const QStringList& required, bool mutating, PlainHandler handler)
{
	defineCommandWithSchema(registry, id, description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace lmms::control
