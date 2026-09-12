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

#include <QJsonArray>
#include <QJsonObject>

#include "AudioEngine.h"

#include "ControlVocabulary.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The control protocol version (AGENT-TOOLING.md #2).
constexpr int ControlProtocolVersion = 1;

//! The engine's own view of the audio device. A client must never be told a bare
//! "ready" while the instance cannot make a sound (task #626): the fallback is
//! announced here, with the device that failed and the one in use. The SHAPE is
//! stable - every field is always present - because a client may poll this before
//! the engine object exists at all, and a checker cannot read optional keys.
QJsonObject audioReport()
{
	QJsonObject out;
	out.insert(QStringLiteral("state"), QStringLiteral("engine_missing"));
	out.insert(QStringLiteral("device"), QString());
	out.insert(QStringLiteral("requested"), QString());
	out.insert(QStringLiteral("start_failed"), false);
	out.insert(QStringLiteral("sound_output"), false);
	AudioEngine* audio = Engine::audioEngine();
	if (audio == nullptr) { return out; }

	const bool failed = audio->audioDevStartFailed();
	out.insert(QStringLiteral("state"),
		failed ? QStringLiteral("dummy_fallback") : QStringLiteral("ok"));
	out.insert(QStringLiteral("device"), audio->audioDevName());
	out.insert(QStringLiteral("requested"), audio->audioDevRequestName());
	out.insert(QStringLiteral("start_failed"), failed);
	out.insert(QStringLiteral("sound_output"), !failed);
	if (failed)
	{
		out.insert(QStringLiteral("message"), audio->audioDevStartReason());
	}
	return out;
}

//! control.ping's result: liveness, readiness, and - while not ready - why.
QJsonObject pingResult()
{
	const ControlRegistry::ReadinessReport state = ControlRegistry::readinessReport();
	QJsonObject result;
	result.insert(QStringLiteral("pong"), true);
	result.insert(QStringLiteral("engine_ready"), state.ready);
	result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
	result.insert(QStringLiteral("proto"), ControlProtocolVersion);
	if (!state.ready)
	{
		QJsonObject reason;
		reason.insert(QStringLiteral("code"), state.code);
		reason.insert(QStringLiteral("message"), state.message);
		result.insert(QStringLiteral("reason"), reason);
	}
	result.insert(QStringLiteral("audio"), audioReport());
	return result;
}

//! control.quit's handler. Split out of the registration below so the CCN
//! ratchet measures the decision, not the declaration.
ControlResult handleQuit(const QJsonObject& args)
{
	const bool save = args.value(QStringLiteral("save")).toBool(false);
	Song* song = Engine::getSong();
	const bool modified = song != nullptr && song->isModified();
	const bool hasFile = song != nullptr && !song->projectFileName().isEmpty();

	// Saving without a file name would open the interactive Save-As dialog - a
	// hang in a headless run. Refuse, typed and actionable.
	if (save && !hasFile)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("control.quit with save:true needs a project file; call project.save "
				"first, or call control.quit with save:false to discard the unsaved changes"));
	}

	QJsonObject result;
	result.insert(QStringLiteral("quitting"), true);
	result.insert(QStringLiteral("save_requested"), save);
	result.insert(QStringLiteral("project_modified"), modified);
	result.insert(QStringLiteral("unsaved_changes"),
		(!save && modified) ? QStringLiteral("discarded") : QStringLiteral("none"));

	// The real fix for #626 is here: the GUI's own quit questions (the "project
	// was modified" QMessageBox that MainWindow::closeEvent shows) are answered
	// from this intent instead of by a dialog nobody can click, and a request
	// that arrives before startup has finished is remembered and applied by
	// main() once the engine is ready. There is no "did the event loop stop?"
	// workaround any more; the bounded last-resort guard lives in
	// ControlRegistry::scheduleQuit().
	ControlRegistry::requestQuit(save ? ControlRegistry::QuitPromptAnswer::Save
									  : ControlRegistry::QuitPromptAnswer::Discard);
	return ControlResult::success(result);
}

void registerPingCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.ping");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("ping");
	cmd.description = QStringLiteral("Liveness probe; also reports whether the engine is addressable "
		"yet, and why not when it is not.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("pong"), booleanProperty()},
		{QStringLiteral("engine_ready"), booleanProperty()},
		{QStringLiteral("version"), stringProperty()},
		{QStringLiteral("proto"), integerProperty()},
		// present only while engine_ready is false: {code, message}
		{QStringLiteral("reason"), objectProperty()},
		// always present: the device actually in use and whether it makes sound
		{QStringLiteral("audio"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(pingResult()); };
	registry.registerCommand(cmd);
}

void registerVersionCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.version");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("version");
	cmd.description = QStringLiteral("The product version string and the control protocol version.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("version"), stringProperty()},
		{QStringLiteral("proto"), integerProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		QJsonObject result;
		result.insert(QStringLiteral("version"), QString::fromUtf8(LMMS_VERSION));
		result.insert(QStringLiteral("proto"), ControlProtocolVersion);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerCommandsListCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.commands_list");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("commands_list");
	cmd.description = QStringLiteral("Every registered command with its schemas and requires declaration.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("commands"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) { return ControlResult::success(registry.describeAll()); };
	registry.registerCommand(cmd);
}

void registerTransactionsCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.transactions");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("transactions");
	cmd.description = QStringLiteral("The transactions recorded for mutating commands (SPEC A16 hook).");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("transactions"), arrayProperty()},
	});
	cmd.handler = [&registry](const QJsonObject&) {
		QJsonObject result;
		result.insert(QStringLiteral("transactions"), registry.transactions());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerUndoCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.undo");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("undo");
	cmd.description = QStringLiteral("Undo the last journal checkpoint through the engine's ProjectJournal.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("undone"), booleanProperty()},
		{QStringLiteral("can_undo"), booleanProperty()},
		{QStringLiteral("can_redo"), booleanProperty()},
		{QStringLiteral("mechanism"), stringProperty()},
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

void registerRedoCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.redo");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("redo");
	cmd.description = QStringLiteral("Redo the last undone journal checkpoint.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("redone"), booleanProperty()},
		{QStringLiteral("can_undo"), booleanProperty()},
		{QStringLiteral("can_redo"), booleanProperty()},
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

void registerQuitCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.quit");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("quit");
	cmd.description = QStringLiteral("Ask the instance to run its normal shutdown (the reply is sent "
		"first). Unsaved changes are discarded unless save is true.");
	cmd.requiresEngine = false;
	cmd.argsSchema = objectSchema({
		// false (the default): discard unsaved changes. true: save the current
		// project to its existing file first (refused when it has none).
		{QStringLiteral("save"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("quitting"), booleanProperty()},
		{QStringLiteral("save_requested"), booleanProperty()},
		{QStringLiteral("project_modified"), booleanProperty()},
		{QStringLiteral("unsaved_changes"), stringProperty()},
	});
	cmd.handler = handleQuit;
	registry.registerCommand(cmd);
}

} // namespace

void registerControlGroupCommands(ControlRegistry& registry)
{
	registerPingCommand(registry);
	registerVersionCommand(registry);
	registerCommandsListCommand(registry);
	registerTransactionsCommand(registry);
	registerUndoCommand(registry);
	registerRedoCommand(registry);
	registerQuitCommand(registry);
}

} // namespace lmms
