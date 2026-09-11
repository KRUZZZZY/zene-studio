/*
 * ControlCommandsControl.cpp - the control.* command group (SPEC A11-A16).
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

#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "lmmsversion.h"

namespace lmms
{

namespace
{

//! The control protocol version (AGENT-TOOLING.md #2).
constexpr int ControlProtocolVersion = 1;

QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {})
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject noArgsSchema()
{
	return schemaObject({});
}

} // namespace

void registerControlGroupCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.ping");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("ping");
		cmd.description = QStringLiteral("Liveness probe; also reports whether the engine is addressable yet.");
		cmd.requiresEngine = false;
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("pong"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("engine_ready"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("version"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
			{QStringLiteral("proto"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			QJsonObject result;
			result.insert(QStringLiteral("pong"), true);
			result.insert(QStringLiteral("engine_ready"), ControlRegistry::isEngineReady());
			result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
			result.insert(QStringLiteral("proto"), ControlProtocolVersion);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.version");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("version");
		cmd.description = QStringLiteral("The product version string and the control protocol version.");
		cmd.requiresEngine = false;
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("version"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
			{QStringLiteral("proto"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			QJsonObject result;
			result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
			result.insert(QStringLiteral("proto"), ControlProtocolVersion);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.commands_list");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("commands_list");
		cmd.description = QStringLiteral("Every registered command with its schemas and requires declaration.");
		cmd.requiresEngine = false;
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("commands"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [&registry](const QJsonObject&) { return ControlResult::success(registry.describeAll()); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.transactions");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("transactions");
		cmd.description = QStringLiteral("The transactions recorded for mutating commands (SPEC A16 hook).");
		cmd.requiresEngine = false;
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("transactions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		});
		cmd.handler = [&registry](const QJsonObject&) {
			QJsonObject result;
			result.insert(QStringLiteral("transactions"), registry.transactions());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.undo");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("undo");
		cmd.description = QStringLiteral("Undo the last journal checkpoint through the engine's ProjectJournal.");
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("undone"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("can_undo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("can_redo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("mechanism"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			auto* journal = Engine::projectJournal();
			bool undone = false;
			if (journal != nullptr && journal->canUndo())
			{
				journal->undo();
				undone = true;
			}
			QJsonObject result;
			result.insert(QStringLiteral("undone"), undone);
			result.insert(QStringLiteral("can_undo"), journal != nullptr && journal->canUndo());
			result.insert(QStringLiteral("can_redo"), journal != nullptr && journal->canRedo());
			result.insert(QStringLiteral("mechanism"), QStringLiteral("lmms::ProjectJournal"));
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.redo");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("redo");
		cmd.description = QStringLiteral("Redo the last undone journal checkpoint.");
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("redone"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("can_undo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("can_redo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			auto* journal = Engine::projectJournal();
			bool redone = false;
			if (journal != nullptr && journal->canRedo())
			{
				journal->redo();
				redone = true;
			}
			QJsonObject result;
			result.insert(QStringLiteral("redone"), redone);
			result.insert(QStringLiteral("can_undo"), journal != nullptr && journal->canUndo());
			result.insert(QStringLiteral("can_redo"), journal != nullptr && journal->canRedo());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("control.quit");
		cmd.group = QStringLiteral("control");
		cmd.verb = QStringLiteral("quit");
		cmd.description = QStringLiteral("Ask the instance to exit (the reply is sent first).");
		cmd.requiresEngine = false;
		cmd.argsSchema = noArgsSchema();
		cmd.resultSchema = schemaObject({
			{QStringLiteral("quitting"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			QJsonObject result;
			result.insert(QStringLiteral("quitting"), true);
			// Ask the event loop to stop...
			QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
			// ...and verify with a watchdog. Measured in this tree: after a
			// control.undo / control.redo the event loop no longer stops on
			// QCoreApplication::quit() (the journal restore leaves the loop
			// un-stoppable; see the lane report). The watchdog keeps the shutdown
			// contract anyway: it unlinks the control socket and leaves the
			// process with a success code.
			QTimer::singleShot(2000, QCoreApplication::instance(), []() {
				fprintf(stderr, "control.quit: the event loop did not stop; "
					"unlinking the control socket and exiting\n");
				fflush(stderr);
				ControlRegistry::instance()->runShutdownHooks();
				std::exit(EXIT_SUCCESS);
			});
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
