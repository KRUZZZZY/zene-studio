/*
 * ControlCommandsProjectMmpzGit.cpp - the mmpz-git depth command group
 *                                     (docs/FEATURE-LIST-0.3.0.md row 42,
 *                                     task #612).
 *
 * The engine half is tools/mmpz-git/mmpz_git.py (the three-way merge driver,
 * the semantic diff, the musical conflict reporter and the audible-diff CLI);
 * this file is the thin control-surface wrapper that makes it drivable through
 * --control-socket.  A SHELL-OUT wrapper, deliberately: the merge driver is a
 * git merge driver and has to stay runnable by git itself, and the tool is
 * shipped in every configuration (no compile-time switch), so there is one
 * implementation of the merge and this file does not grow a second one.
 *
 * All four verbs operate on project FILES, not on the running session, so all
 * four are not_mutating in the A16 table (ControlReversibilityTableMmpzGit.cpp)
 * and none of them pushes an undo step.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

//! $MMPZ_GIT_TOOL wins; otherwise the tool is found in the source tree beside
//! the build directory (the same three relative positions a build tree can sit
//! at), because a control-socket session usually runs out of build/.
QString findMmpzGitTool()
{
	const QString env = qEnvironmentVariable("MMPZ_GIT_TOOL");
	if (!env.isEmpty() && QFile::exists(env)) { return env; }

	const QString appDir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
		appDir + QStringLiteral("/../../tools/mmpz-git/mmpz_git.py"),
		appDir + QStringLiteral("/../tools/mmpz-git/mmpz_git.py"),
		appDir + QStringLiteral("/../../../tools/mmpz-git/mmpz_git.py"),
	};
	for (const QString& c : candidates)
	{
		if (QFile::exists(c)) { return c; }
	}
	return QString();
}

QString findPython3()
{
	const QString py = QStandardPaths::findExecutable(QStringLiteral("python3"));
	if (!py.isEmpty()) { return py; }
	return QStandardPaths::findExecutable(QStringLiteral("python"));
}

struct ToolPaths
{
	QString python;
	QString tool;
};

ToolPaths resolveTool()
{
	ToolPaths p;
	p.python = findPython3();
	p.tool = findMmpzGitTool();
	return p;
}

ControlResult toolMissing()
{
	const ToolPaths tp = resolveTool();
	QStringList missing;
	if (tp.python.isEmpty()) { missing << QStringLiteral("python3"); }
	if (tp.tool.isEmpty()) { missing << QStringLiteral("tools/mmpz-git/mmpz_git.py"); }
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("mmpz-git cannot run here: %1 not found. Set MMPZ_GIT_TOOL to the "
			"path of mmpz_git.py (and MMPZ_GIT_RENDERER for audible-diff), or run from a "
			"tree where the source sits beside the build directory.").arg(missing.join(
			QStringLiteral(", "))));
}

//! A missing input file is refused by name before the tool is started, so the
//! caller gets the file it passed back rather than a Python traceback.
ControlResult missingFile(const QString& path)
{
	return ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no such file: %1").arg(path));
}

/*! Runs the Python tool with the given args and returns its stdout, stderr and
 *  exit code.  Every call is bounded: 120 s for reads, 600 s for audible-diff
 *  (which renders both projects, track by track, in child processes).
 */
struct RunResult
{
	int exitCode = -1;
	QString stdoutText;
	QString stderrText;
	bool finished = false;
};

RunResult runTool(const QStringList& args, int timeoutMs = 120000)
{
	RunResult out;
	ToolPaths tp = resolveTool();
	if (tp.python.isEmpty() || tp.tool.isEmpty()) { return out; }

	QProcess proc;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	// audible-diff needs a renderer; the binary that is running this command is
	// the obvious one, and an explicit $MMPZ_GIT_RENDERER still wins.
	if (env.value(QStringLiteral("MMPZ_GIT_RENDERER")).isEmpty())
	{
		env.insert(QStringLiteral("MMPZ_GIT_RENDERER"), QCoreApplication::applicationFilePath());
	}
	proc.setProcessEnvironment(env);
	proc.start(tp.python, QStringList{tp.tool} + args);
	out.finished = proc.waitForStarted(30000) && proc.waitForFinished(timeoutMs);
	if (!out.finished) { proc.kill(); }
	out.exitCode = out.finished ? proc.exitCode() : -1;
	out.stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
	out.stderrText = QString::fromUtf8(proc.readAllStandardError());
	return out;
}

//! The report's own header names the count: "mmpz-git: N conflict(s) to
//! resolve in <file>".  -1 when the text does not carry it, so a caller is
//! never handed an invented number.
int conflictCountFromReport(const QString& text)
{
	static const QRegularExpression re(
		QStringLiteral("mmpz-git:\\s+(\\d+)\\s+conflict\\(s\\)"));
	const QRegularExpressionMatch m = re.match(text);
	return m.hasMatch() ? m.captured(1).toInt() : -1;
}

struct DiffSummary
{
	int added = -1;
	int removed = -1;
	int changed = -1;
	int moved = -1;
};

//! The diff's header: "# operations: %d added, %d removed, %d changed, %d moved".
DiffSummary diffSummaryFromText(const QString& text)
{
	DiffSummary out;
	static const QRegularExpression re(QStringLiteral(
		"# operations:\\s+(\\d+)\\s+added,\\s+(\\d+)\\s+removed,\\s+(\\d+)\\s+changed,"
		"\\s+(\\d+)\\s+moved"));
	const QRegularExpressionMatch m = re.match(text);
	if (m.hasMatch())
	{
		out.added = m.captured(1).toInt();
		out.removed = m.captured(2).toInt();
		out.changed = m.captured(3).toInt();
		out.moved = m.captured(4).toInt();
	}
	return out;
}

//! What every verb does before it runs the tool: resolve the interpreter and the
//! tool, then refuse a missing input file by name.  One place, so no handler can
//! forget a refusal - and so each handler stays under the complexity target.
bool prepareRun(const QStringList& files, ControlResult* error)
{
	const ToolPaths tp = resolveTool();
	if (tp.python.isEmpty() || tp.tool.isEmpty()) { *error = toolMissing(); return false; }
	for (const QString& p : files)
	{
		if (!QFile::exists(p)) { *error = missingFile(p); return false; }
	}
	return true;
}

//! True (with *error set) when the tool did not finish inside its bound.
bool runTimedOut(const RunResult& r, const QString& verb, ControlResult* error)
{
	if (r.finished) { return false; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("mmpz-git %1 did not finish in time").arg(verb));
	return true;
}

//! True (with *error set) when the tool exited in a way that is neither its
//! success code nor its "there are conflicts/differences" code.
bool toolFailed(const RunResult& r, const QString& verb, ControlResult* error)
{
	if (r.exitCode == 0 || r.exitCode == 1) { return false; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("mmpz-git %1 failed (exit %2): %3")
			.arg(verb).arg(r.exitCode).arg(r.stderrText.left(400)));
	return true;
}

//! The operation lines of a diff: the bodies below its '#' header lines.
QJsonArray operationLines(const QString& text)
{
	QJsonArray ops;
	for (const QString& line : text.split(QLatin1Char('\n')))
	{
		if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) { ops.append(line); }
	}
	return ops;
}

//! A finished merge's result: exit 0 is clean; exit 1 carries the conflict count
//! read back out of the report (never an invented number); the sidecar is named
//! whether or not one was written.
ControlResult mergeResult(const RunResult& r, const QString& ours, const QString& report)
{
	const int conflicts = conflictCountFromReport(r.stderrText);
	const QString sidecar = ours + QStringLiteral(".mmpz-git-conflicts.json");
	QJsonObject result;
	result.insert(QStringLiteral("merged_file"), ours);
	result.insert(QStringLiteral("conflicts"), r.exitCode == 0 ? 0 : qMax(conflicts, 0));
	result.insert(QStringLiteral("report_path"), report);
	result.insert(QStringLiteral("report"), r.exitCode == 0 ? QString() : r.stderrText);
	result.insert(QStringLiteral("sidecar"), QFile::exists(sidecar) ? sidecar : QString());
	result.insert(QStringLiteral("exit_code"), r.exitCode);
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// project.merge
// ---------------------------------------------------------------------------

void registerProjectMerge(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.merge");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("merge");
	cmd.description = QStringLiteral("Three-way (base, ours, theirs) merge of LMMS project "
		"files through the mmpz-git merge driver. Operates on FILES, not the running "
		"session: the merged document is written to the 'ours' path (which is what git "
		"expects of a merge driver). 'conflicts' is 0 for a clean merge and the number of "
		"musical conflicts otherwise; a conflicted file is marked in place with comments a "
		"human resolves and `project.conflicts` re-prints. Exit 2 from the driver (a merge "
		"it refuses to write) is an error here, never a silent success.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("base"), stringProperty()},
			{QStringLiteral("ours"), stringProperty()},
			{QStringLiteral("theirs"), stringProperty()},
			{QStringLiteral("report"), stringProperty()}},
		{QStringLiteral("base"), QStringLiteral("ours"), QStringLiteral("theirs")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("conflicts"), integerProperty()},
		{QStringLiteral("merged_file"), stringProperty()},
		{QStringLiteral("report_path"), stringProperty()},
		{QStringLiteral("report"), stringProperty()},
		{QStringLiteral("sidecar"), stringProperty()},
		{QStringLiteral("exit_code"), integerProperty()},
	});
	cmd.mutating = false; // does not change the running session
	cmd.handler = [](const QJsonObject& args) {
		const QString base = args.value(QStringLiteral("base")).toString();
		const QString ours = args.value(QStringLiteral("ours")).toString();
		const QString theirs = args.value(QStringLiteral("theirs")).toString();
		ControlResult error;
		if (!prepareRun({base, ours, theirs}, &error)) { return error; }

		QStringList toolArgs{QStringLiteral("merge"), base, ours, theirs};
		const QString report = args.value(QStringLiteral("report")).toString();
		if (!report.isEmpty())
		{
			toolArgs << QStringLiteral("--report") << report;
		}

		const RunResult r = runTool(toolArgs, 120000);
		if (runTimedOut(r, QStringLiteral("merge"), &error)) { return error; }
		if (r.exitCode == 2)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git merge refused to write %1: %2")
					.arg(ours, r.stderrText.left(400)));
		}
		if (toolFailed(r, QStringLiteral("merge"), &error)) { return error; }

		return mergeResult(r, ours, report);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.diff
// ---------------------------------------------------------------------------

void registerProjectDiff(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.diff");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("diff");
	cmd.description = QStringLiteral("Semantic, XML-aware diff of two LMMS project files: "
		"which elements were added, removed, changed or moved, named by their musical path. "
		"Reads both files and writes nothing. This is the diff the repository configures for "
		".mmpz, so `git diff` and this verb show the same operation list.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("a"), stringProperty()},
			{QStringLiteral("b"), stringProperty()}},
		{QStringLiteral("a"), QStringLiteral("b")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("added"), integerProperty()},
		{QStringLiteral("removed"), integerProperty()},
		{QStringLiteral("changed"), integerProperty()},
		{QStringLiteral("moved"), integerProperty()},
		{QStringLiteral("operations"), arrayProperty()},
		{QStringLiteral("exit_code"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		const QString a = args.value(QStringLiteral("a")).toString();
		const QString b = args.value(QStringLiteral("b")).toString();
		ControlResult error;
		if (!prepareRun({a, b}, &error)) { return error; }

		const RunResult r = runTool({QStringLiteral("diff"), a, b}, 120000);
		if (runTimedOut(r, QStringLiteral("diff"), &error)) { return error; }
		if (toolFailed(r, QStringLiteral("diff"), &error)) { return error; }

		const DiffSummary summary = diffSummaryFromText(r.stdoutText);
		QJsonObject result;
		if (summary.added >= 0)
		{
			result.insert(QStringLiteral("added"), summary.added);
			result.insert(QStringLiteral("removed"), summary.removed);
			result.insert(QStringLiteral("changed"), summary.changed);
			result.insert(QStringLiteral("moved"), summary.moved);
		}
		result.insert(QStringLiteral("operations"), operationLines(r.stdoutText));
		result.insert(QStringLiteral("exit_code"), r.exitCode);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.conflicts
// ---------------------------------------------------------------------------

void registerProjectConflicts(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.conflicts");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("conflicts");
	cmd.description = QStringLiteral("Re-present the musical conflicts a project file "
		"carries: the marker comments the mmpz-git merge driver left behind, read back "
		"without re-running the merge. Each conflict names the track, the pattern, the note "
		"and the bar, and what each side did. Reads one file and writes nothing; "
		"conflict_count 0 means the file is not conflicted.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("file"), stringProperty()}},
		{QStringLiteral("file")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("conflict_count"), integerProperty()},
		{QStringLiteral("report"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		const QString file = args.value(QStringLiteral("file")).toString();
		ControlResult error;
		if (!prepareRun({file}, &error)) { return error; }

		const RunResult r = runTool({QStringLiteral("conflicts"), file}, 120000);
		if (runTimedOut(r, QStringLiteral("conflicts"), &error)) { return error; }
		if (r.exitCode == 2)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git conflicts refused %1: %2")
					.arg(file, r.stderrText.left(400)));
		}

		const QString text = r.stdoutText + r.stderrText;
		int count = conflictCountFromReport(text);
		if (r.exitCode == 0 && count < 0) { count = 0; } // "no conflicts marked in ..."
		QJsonObject result;
		result.insert(QStringLiteral("conflict_count"), qMax(count, 0));
		result.insert(QStringLiteral("report"), text);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.audible_diff
// ---------------------------------------------------------------------------

void registerProjectAudibleDiff(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.audible_diff");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("audible_diff");
	cmd.description = QStringLiteral("Render two project files (through the built binary, "
		"as child processes) and report which bars of which track differ, with the RMS and "
		"peak difference per run of bars. This is the audible answer to `diff`: it names a "
		"bar and a track, not a note. Long-running: it renders both projects, track by "
		"track. differ=false means the renders matched bar for bar.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("a"), stringProperty()},
			{QStringLiteral("b"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("mix_only"), booleanProperty()}},
		{QStringLiteral("a"), QStringLiteral("b")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("differ"), booleanProperty()},
		{QStringLiteral("report"), stringProperty()},
		{QStringLiteral("exit_code"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		const QString a = args.value(QStringLiteral("a")).toString();
		const QString b = args.value(QStringLiteral("b")).toString();
		ControlResult error;
		if (!prepareRun({a, b}, &error)) { return error; }

		QStringList toolArgs{QStringLiteral("audible-diff"), a, b};
		const QString track = args.value(QStringLiteral("track")).toString();
		if (!track.isEmpty())
		{
			toolArgs << QStringLiteral("--track") << track;
		}
		if (args.value(QStringLiteral("mix_only")).toBool())
		{
			toolArgs << QStringLiteral("--mix-only");
		}

		const RunResult r = runTool(toolArgs, 600000);
		if (runTimedOut(r, QStringLiteral("audible-diff"), &error)) { return error; }
		if (r.exitCode == 2)
		{
			// no renderer, an unreadable input, a format mismatch: an error the
			// caller must see, not a difference
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git audible-diff failed: %1")
					.arg(r.stderrText.left(600)));
		}

		QJsonObject result;
		result.insert(QStringLiteral("differ"), r.exitCode == 1);
		result.insert(QStringLiteral("report"), r.stdoutText + r.stderrText);
		result.insert(QStringLiteral("exit_code"), r.exitCode);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectMmpzGitCommands(ControlRegistry& registry)
{
	registerProjectMerge(registry);
	registerProjectDiff(registry);
	registerProjectConflicts(registry);
	registerProjectAudibleDiff(registry);
}

} // namespace lmms
