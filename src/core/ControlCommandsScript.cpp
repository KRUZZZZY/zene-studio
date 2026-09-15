/*
 * ControlCommandsScript.cpp - the script.* command group (SPEC A11-A16).
 *
 * The product's Lua surface has never been reachable from a *running*
 * instance: `--run-script <file>` (src/core/main.cpp:206, :831) executes a
 * script and exits the process. script.run drives the same ScriptEngine inside
 * the live instance instead, so an agent's script sees the session the agent
 * built and the session survives it.
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

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ConfigManager.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ScriptEngine.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Where the shipped Lua examples live: the app's own "data:" search path
//! (ConfigManager resolves it to the source tree's data/ in a build tree).
QStringList scriptDirCandidates()
{
	QStringList candidates;
	candidates.append(ConfigManager::inst()->dataDir() + QStringLiteral("scripts/"));

	for (const QString& root : QDir::searchPaths(QStringLiteral("data")))
	{
		const QString slash = root.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/");
		candidates.append(root + slash + QStringLiteral("scripts/"));
	}
	return candidates;
}

//! The first candidate that holds at least one .lua file, else the first that
//! exists at all, else empty.
QString resolveScriptDir()
{
	QString firstExisting;
	for (const QString& candidate : scriptDirCandidates())
	{
		QDir dir(candidate);
		if (!dir.exists()) { continue; }
		if (firstExisting.isEmpty()) { firstExisting = dir.absolutePath(); }
		if (!dir.entryList({QStringLiteral("*.lua")}, QDir::Files).isEmpty())
		{
			return dir.absolutePath();
		}
	}
	return firstExisting;
}

QJsonObject scriptEntryJson(const QFileInfo& file)
{
	QByteArray bytes;
	// The helper writes its refusal into the ControlResult, so a local one is
	// passed rather than a null: an unreadable script is reported with an empty
	// hash instead of taking the socket down.
	ControlResult unreadable;
	if (!controlReadFileBytes(file.absoluteFilePath(), &bytes, &unreadable))
	{
		bytes.clear();
	}
	QJsonObject entry;
	entry.insert(QStringLiteral("name"), file.fileName());
	entry.insert(QStringLiteral("path"), file.absoluteFilePath());
	entry.insert(QStringLiteral("bytes"), static_cast<qint64>(file.size()));
	entry.insert(QStringLiteral("sha256"), controlSha256OfBytes(bytes));
	return entry;
}

ControlResult scriptList()
{
	const QString dir = resolveScriptDir();

	QJsonArray scripts;
	if (!dir.isEmpty())
	{
		QDir qdir(dir);
		const QFileInfoList files = qdir.entryInfoList({QStringLiteral("*.lua")}, QDir::Files,
			QDir::Name);
		for (const QFileInfo& file : files)
		{
			scripts.append(scriptEntryJson(file));
		}
	}

	QJsonObject result;
	result.insert(QStringLiteral("dir"), dir);
	result.insert(QStringLiteral("candidates"), QJsonArray::fromStringList(scriptDirCandidates()));
	result.insert(QStringLiteral("scripts"), scripts);
	result.insert(QStringLiteral("count"), scripts.size());
	return ControlResult::success(result);
}

//! The wire's closed error set for a Lua run (AGENT-TOOLING.md #4).
ControlErrorKind scriptErrorKind(ScriptEngine::RunResult result)
{
	switch (result)
	{
		case ScriptEngine::RunResult::Ok: return ControlErrorKind::None;
		case ScriptEngine::RunResult::VersionError: return ControlErrorKind::InvalidArgs;
		case ScriptEngine::RunResult::SandboxError: return ControlErrorKind::Refused;
		case ScriptEngine::RunResult::Busy: return ControlErrorKind::Busy;
		case ScriptEngine::RunResult::ScriptError: return ControlErrorKind::Refused;
	}
	return ControlErrorKind::Refused;
}

/*! The failure reply's message.
 *
 * A typed error on this wire carries `kind` + `message` and no result object, so
 * the Lua log lines the run produced are folded into the message instead of
 * being dropped: an agent that got a refusal still sees what the script said.
 */
QString scriptFailureMessage(const QString& subject, const QString& error,
	const QStringList& logs)
{
	QString message = QStringLiteral("script %1 failed: %2").arg(subject, error);
	constexpr int MaxLogLines = 8;
	for (int i = 0; i < logs.size() && i < MaxLogLines; ++i)
	{
		message += i == 0 ? QStringLiteral(" [log: ") : QStringLiteral(" | ");
		message += logs.at(i);
	}
	if (!logs.isEmpty()) { message += QLatin1Char(']'); }
	return message;
}

QJsonObject scriptRunResultJson(ScriptEngine::RunResult result, const QStringList& logs,
	const QString& path, quint64 budget, bool budgetOverridden)
{
	QJsonObject out;
	out.insert(QStringLiteral("ran"), result == ScriptEngine::RunResult::Ok);
	out.insert(QStringLiteral("path"), path);
	out.insert(QStringLiteral("log"), QJsonArray::fromStringList(logs));
	out.insert(QStringLiteral("log_lines"), logs.size());
	out.insert(QStringLiteral("instruction_budget"), static_cast<qint64>(budget));
	out.insert(QStringLiteral("budget_overridden"), budgetOverridden);
	// CODE-6: the memory budget and what THIS run measured against it. Read
	// from the engine, not recomputed here: the numbers are the allocator's own
	// count, so the report cannot drift into a second opinion about them.
	ScriptEngine* engine = ScriptEngine::instance();
	out.insert(QStringLiteral("memory_budget"), static_cast<qint64>(engine->memoryBudget()));
	out.insert(QStringLiteral("memory_live_bytes"), static_cast<qint64>(engine->lastRunMemoryBytes()));
	out.insert(QStringLiteral("memory_peak_bytes"), static_cast<qint64>(engine->lastRunMemoryPeakBytes()));
	out.insert(QStringLiteral("memory_refusals"), static_cast<qint64>(engine->lastRunMemoryRefusals()));
	return out;
}

ControlResult scriptRun(const QJsonObject& args)
{
	const bool hasPath = args.contains(QStringLiteral("path"));
	if (hasPath == args.contains(QStringLiteral("source")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("exactly one of 'path' or 'source' is required: 'path' runs a file, "
				"'source' runs Lua text supplied inline"));
	}

	QString path;
	if (hasPath)
	{
		path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo::exists(path))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no Lua script at '%1'").arg(path));
		}
	}

	ScriptEngine* engine = ScriptEngine::instance();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no script engine"));
	}

	// The budget is per invocation (SPEC / spec section 5) and is put back
	// afterwards, so an override shapes this run and not the process.
	const bool overridden = args.contains(QStringLiteral("budget"));
	const quint64 previous = engine->instructionBudget();
	if (overridden)
	{
		engine->setInstructionBudget(
			static_cast<quint64>(args.value(QStringLiteral("budget")).toDouble()));
	}

	QString error;
	ScriptEngine::RunResult result = ScriptEngine::RunResult::ScriptError;
	if (hasPath)
	{
		result = engine->runFile(path, &error);
	}
	else
	{
		const QString chunk = args.value(QStringLiteral("name")).toString(
			QStringLiteral("=(control source)"));
		result = engine->runString(args.value(QStringLiteral("source")).toString(), &error, chunk);
	}
	const QStringList logs = engine->takeLogMessages();
	const quint64 budget = engine->instructionBudget();
	if (overridden) { engine->setInstructionBudget(previous); }

	if (result != ScriptEngine::RunResult::Ok)
	{
		return ControlResult::failure(scriptErrorKind(result),
			scriptFailureMessage(path.isEmpty() ? QStringLiteral("(inline source)") :
				QStringLiteral("'%1'").arg(path), error, logs));
	}

	QJsonObject out = scriptRunResultJson(result, logs, path, budget, overridden);
	// SPEC A16: a script's edits are its own, so the record says exactly that
	// rather than inventing an inverse. A script may take its own checkpoint
	// (Lua `addCheckPoint()`), and control.undo replays that one.
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"), QJsonObject{});
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"),
			QStringLiteral("UNIMPLEMENTED: revert a Lua script's own edits")}});
	transaction.insert(QStringLiteral("reversible"), false);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("snapshot only: a Lua script mutates the engine through its own bindings; "
			"the registry records no inverse for that. A script can take its own ProjectJournal "
			"checkpoint with Lua addCheckPoint(), which control.undo does replay."));
	out.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(out);
}

/*! script.set_memory_budget (CODE-6): set the cap the Lua allocator refuses past.
 *
 * Why a command and not only a C++ setter: the budget is a bound on what a
 * SCRIPT may hold, and the release's contract is that anything the release does
 * must be drivable through the surface (charter 3.1). The alternative - a fixed
 * constant nobody can measure - is exactly the state `ScriptEngine.cpp`'s
 * luaL_newstate() left the instruction budget's sibling in.
 *
 * The budget is a process bound, not project state, so its A16 class is the
 * `control.set_undo_depth` precedent: a SNAPSHOT row whose before-state holds
 * the previous value and whose inverse is this command with that value, so one
 * control.undo restores the cap. A run already in flight keeps the budget it
 * read when it opened its state, and a refusal it already hit is not undone by
 * putting the cap back - the transaction says both.
 */
ControlResult scriptSetMemoryBudget(const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("bytes")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("script.set_memory_budget needs 'bytes': the cap the Lua allocator "
				"refuses past, in [%1, %2]")
				.arg(ScriptEngine::MinMemoryBudgetBytes)
				.arg(ScriptEngine::MaxMemoryBudgetBytes));
	}

	const double requested = args.value(QStringLiteral("bytes")).toDouble();
	if (requested < static_cast<double>(ScriptEngine::MinMemoryBudgetBytes)
		|| requested > static_cast<double>(ScriptEngine::MaxMemoryBudgetBytes))
	{
		// A refusal, not a clamp: a client that asks for 4 bytes must be told
		// no, not silently given 512 KiB and a different experiment than the
		// one it designed.
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("script.set_memory_budget: %1 bytes is outside [%2, %3]. Below the "
				"floor a Lua state cannot even open (the run would fail with 'could not create "
				"a Lua state' rather than with a budget refusal), and this command never removes "
				"the cap: a budget the surface cannot set is a budget the surface cannot promise.")
				.arg(requested)
				.arg(ScriptEngine::MinMemoryBudgetBytes)
				.arg(ScriptEngine::MaxMemoryBudgetBytes));
	}

	ScriptEngine* engine = ScriptEngine::instance();
	const quint64 previous = engine->memoryBudget();
	const quint64 bytes = static_cast<quint64>(requested);
	engine->setMemoryBudget(bytes);

	QJsonObject out;
	out.insert(QStringLiteral("memory_budget"), static_cast<qint64>(bytes));
	out.insert(QStringLiteral("previous_memory_budget"), static_cast<qint64>(previous));
	out.insert(QStringLiteral("changed"), previous != bytes);
	out.insert(QStringLiteral("min_memory_budget"),
		static_cast<qint64>(ScriptEngine::MinMemoryBudgetBytes));
	out.insert(QStringLiteral("max_memory_budget"),
		static_cast<qint64>(ScriptEngine::MaxMemoryBudgetBytes));
	out.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"),
						QJsonObject{{QStringLiteral("memory_budget"), static_cast<qint64>(previous)}}},
			{QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("script.set_memory_budget")},
					{QStringLiteral("applies"), QStringLiteral("command")},
					{QStringLiteral("args"),
						QJsonObject{{QStringLiteral("bytes"), static_cast<qint64>(previous)}}}}},
			{QStringLiteral("reversible"), true},
			{QStringLiteral("mechanism"),
				QStringLiteral("snapshot: the previous budget is in before and the recorded "
					"inverse is this command with that value, so one control.undo puts the cap "
					"back. What the inverse does NOT do is re-run a script the old budget "
					"refused: that run happened under the cap in force then, and re-running it "
					"is a new run under the restored cap")}});
	return ControlResult::success(out);
}

} // namespace

void registerScriptCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("script.run");
		cmd.group = QStringLiteral("script");
		cmd.verb = QStringLiteral("run");
		cmd.description = QStringLiteral("Run a Lua script (docs/specs/SPEC-lua-api-v0.md) in this "
			"running instance - the same ScriptEngine the run-and-exit `--run-script` CLI flag "
			"drives, on its own worker thread with the engine apply side pumped on the UI thread. "
			"Returns the Lua log lines, or a typed error carrying them. 'budget' overrides the "
			"per-invocation instruction budget (default from the engine) for this call only.");
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("path"), control::stringProperty()},
			{QStringLiteral("source"), control::stringProperty()},
			{QStringLiteral("name"), control::stringProperty()},
			{QStringLiteral("budget"), control::integerProperty(1, 0x7fffffff)},
		});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("ran"), control::booleanProperty()},
			{QStringLiteral("path"), control::stringProperty()},
			{QStringLiteral("log"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("log_lines"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("instruction_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("budget_overridden"), control::booleanProperty()},
			// CODE-6: the memory budget beside it, and what this run measured.
			{QStringLiteral("memory_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("memory_live_bytes"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("memory_peak_bytes"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("memory_refusals"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return scriptRun(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("script.list");
		cmd.group = QStringLiteral("script");
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("The Lua scripts this build ships (data/scripts, resolved "
			"through the app's own 'data:' search path) with their sizes and hashes. Reads the "
			"filesystem, so it answers before the engine is up.");
		cmd.requiresEngine = false;
		cmd.argsSchema = control::objectSchema({});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("dir"), control::stringProperty()},
			{QStringLiteral("candidates"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("scripts"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) { return scriptList(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("script.set_memory_budget");
		cmd.group = QStringLiteral("script");
		cmd.verb = QStringLiteral("set_memory_budget");
		cmd.description = QStringLiteral("Set the block of memory a script run may hold, in bytes "
			"(CODE-6): the budget beside the instruction budget. The Lua state is opened with an "
			"allocator that refuses any allocation past this cap, so a runaway script fails with "
			"'memory budget exceeded' and its run is aborted - it cannot grow until the machine "
			"kills the process. Refuses a value outside [min_memory_budget, max_memory_budget]; "
			"there is no 'unlimited' through this surface. Reversible: one control.undo restores "
			"the previous cap (script.run reports the budget and what the last run measured).");
		cmd.mutating = true;
		cmd.argsSchema = control::objectSchema({
			{QStringLiteral("bytes"),
				control::integerProperty(static_cast<int>(ScriptEngine::MinMemoryBudgetBytes),
					static_cast<int>(ScriptEngine::MaxMemoryBudgetBytes))},
		});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("memory_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("previous_memory_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("changed"), control::booleanProperty()},
			{QStringLiteral("min_memory_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("max_memory_budget"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject& args) { return scriptSetMemoryBudget(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
