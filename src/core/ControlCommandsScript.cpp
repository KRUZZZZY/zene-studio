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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ConfigManager.h"
#include "ControlAutomationSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ScriptEngine.h"
#include "SessionCheckpoint.h"

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

//! SHA-256 of a script, hashed in 64 KiB reads so a script of any size costs one chunk
//! of memory. An unreadable script yields an EMPTY hash, never the well-known SHA-256 of
//! zero bytes: 'bytes: 412' beside that hash would read as a claim about 412 bytes, and
//! it is a claim about none.
QString scriptFileSha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }

	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd())
	{
		const QByteArray chunk = file.read(64 * 1024);
		if (chunk.isEmpty())
		{
			if (file.atEnd() && file.error() == QFileDevice::NoError) { break; }
			return {};
		}
		hash.addData(chunk);
	}
	if (file.error() != QFileDevice::NoError) { return {}; }
	return QString::fromLatin1(hash.result().toHex());
}

QJsonObject scriptEntryJson(const QFileInfo& file)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("name"), file.fileName());
	entry.insert(QStringLiteral("path"), file.absoluteFilePath());
	entry.insert(QStringLiteral("bytes"), static_cast<qint64>(file.size()));
	entry.insert(QStringLiteral("sha256"), scriptFileSha256(file.absoluteFilePath()));
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

//! Runs the file or inline source \a args names. The budget is per invocation (SPEC / spec section 5) and is
//! put back afterwards, so an override shapes this run and not the process; \a budget reports the one in force.
ScriptEngine::RunResult runWithBudget(ScriptEngine* engine, const QJsonObject& args, const QString& path,
	QString* error, quint64* budget)
{
	const bool overridden = args.contains(QStringLiteral("budget"));
	const quint64 previous = engine->instructionBudget();
	if (overridden)
	{
		engine->setInstructionBudget(
			static_cast<quint64>(args.value(QStringLiteral("budget")).toDouble()));
	}
	ScriptEngine::RunResult result = ScriptEngine::RunResult::ScriptError;
	if (!path.isEmpty())
	{
		result = engine->runFile(path, error);
	}
	else
	{
		const QString chunk = args.value(QStringLiteral("name")).toString(
			QStringLiteral("=(control source)"));
		result = engine->runString(args.value(QStringLiteral("source")).toString(), error, chunk);
	}
	*budget = engine->instructionBudget();
	if (overridden) { engine->setInstructionBudget(previous); }
	return result;
}

//! SPEC A16: with a checkpoint taken and the project changed, the record is a snapshot whose one undo step
//! restores the whole project as it was before the script (and redo reapplies the run's result). Otherwise it
//! says why it claims nothing: a script that changed nothing has nothing to undo, and one that ran without a
//! checkpoint has no inverse the registry can name.
QJsonObject scriptTransaction(const control::SessionCheckpoint& checkpoint)
{
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"), QJsonObject{});
	if (checkpoint.pushed)
	{
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("restore the project as it was before the script")},
				{QStringLiteral("applies"), QStringLiteral("journal")}});
		transaction.insert(QStringLiteral("reversible"), true);
		// The snapshot restores what the commands the script ran (zene.command) did too.
		transaction.insert(QStringLiteral("covers_nested"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot: the pre-script and redo-state projects are captured whole (%1 bytes "
			"compressed in total, audio files by reference) and everything the script changed is ONE "
				"undo step that restores it exactly; redo reapplies the run's result. The finer "
				"steps the script pushed while it ran are replaced by that one step")
				.arg(checkpoint.retainedBytes));
	}
	else
	{
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("none: no undo step was kept for this run")}});
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("none: %1")
				.arg(checkpoint.taken() ? QStringLiteral("the script left the project exactly as it was, "
										  "so there is nothing to undo")
										: checkpoint.note));
	}
	return transaction;
}

bool scriptInput(const QJsonObject& args, QString* path, ControlResult* error)
{
	const bool hasPath = args.contains(QStringLiteral("path"));
	if (hasPath == args.contains(QStringLiteral("source")))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("exactly one of 'path' or 'source' is required: 'path' runs a file, "
				"'source' runs Lua text supplied inline"));
		return false;
	}
	if (!hasPath) { return true; }

	*path = args.value(QStringLiteral("path")).toString();
	if (QFileInfo::exists(*path)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no Lua script at '%1'").arg(*path));
	return false;
}

ControlResult scriptRun(const QJsonObject& args)
{
	QString path;
	ControlResult inputError;
	if (!scriptInput(args, &path, &inputError)) { return inputError; }

	ScriptEngine* engine = ScriptEngine::instance();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no script engine"));
	}

	// Q050-044: the project as it is before the script touches it, bounded by the undo stack's byte
	// budget. A checkpoint over the budget refuses the run, before the script has changed anything.
	control::SessionCheckpoint checkpoint = control::takeSessionCheckpoint(QStringLiteral("script.run"), false);
	if (checkpoint.refused())
	{
		return ControlResult::failure(ControlErrorKind::Refused, checkpoint.note);
	}
	if (!checkpoint.taken())
	{
		return ControlResult::failure(ControlErrorKind::Refused, checkpoint.note);
	}
	ProjectJournal* journal = Engine::projectJournal();
	const quint64 serialBefore = journal != nullptr ? journal->topStepSerial() : 0;

	const bool overridden = args.contains(QStringLiteral("budget"));
	QString error;
	quint64 budget = 0;
	const ScriptEngine::RunResult result = runWithBudget(engine, args, path, &error, &budget);
	const QStringList logs = engine->takeLogMessages();

	// Everything the script queued is applied before the project is compared with the checkpoint.
	engine->processCommands();
	bool changed = false;
	QString checkpointRefusal;
	control::finishScriptCheckpoint(&checkpoint, serialBefore, &changed, &checkpointRefusal);
	if (!checkpointRefusal.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, checkpointRefusal);
	}

	if (result != ScriptEngine::RunResult::Ok)
	{
		QString message = scriptFailureMessage(path.isEmpty() ? QStringLiteral("(inline source)") :
			QStringLiteral("'%1'").arg(path), error, logs);
		// The edits made before the error stay in the project; one undo takes them all back.
		ControlResult failed = ControlResult::failure(scriptErrorKind(result), message);
		if (checkpoint.pushed)
		{
			message += QStringLiteral(" (the edits it made before failing are ONE undo step)");
			failed.errorMessage = message;
			failed.result.insert(QStringLiteral("__transaction"), scriptTransaction(checkpoint));
		}
		return failed;
	}

	QJsonObject out = scriptRunResultJson(result, logs, path, budget, overridden);
	out.insert(QStringLiteral("checkpoint"), checkpoint.toJson());
	out.insert(QStringLiteral("changed_project"), changed);
	out.insert(QStringLiteral("__transaction"), scriptTransaction(checkpoint));
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
	const bool changed = previous != bytes;
	// BUG 12: a cap the engine already has is neither set again nor recorded. A
	// transaction for it would be a reversible no-op on top of the last real
	// change, and the next control.undo would spend its one step on that no-op.
	if (changed) { engine->setMemoryBudget(bytes); }

	QJsonObject out;
	out.insert(QStringLiteral("memory_budget"), static_cast<qint64>(bytes));
	out.insert(QStringLiteral("previous_memory_budget"), static_cast<qint64>(previous));
	out.insert(QStringLiteral("changed"), changed);
	out.insert(QStringLiteral("min_memory_budget"),
		static_cast<qint64>(ScriptEngine::MinMemoryBudgetBytes));
	out.insert(QStringLiteral("max_memory_budget"),
		static_cast<qint64>(ScriptEngine::MaxMemoryBudgetBytes));
	if (!changed) { return ControlResult::success(out); }
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
			"per-invocation instruction budget (default from the engine) for this call only. The project is "
			"captured whole before the script runs (bounded by the undo stack's byte budget, "
			"control.set_undo_depth; over it the run is REFUSED before anything changes), and "
			"everything the script changed is ONE undo step: control.undo restores the exact "
			"pre-script project and control.redo reapplies the run's result.");
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
			// Q050-044: the whole-project checkpoint taken before the run, and whether the run changed the project.
			{QStringLiteral("checkpoint"), control::objectProperty()},
			{QStringLiteral("changed_project"), control::booleanProperty()},
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
