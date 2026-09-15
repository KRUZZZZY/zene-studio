/*
 * ControlCommandsProjectMmpzGit.cpp - the mmpz-git depth command group
 *                                     (docs/FEATURE-LIST-0.3.0.md row 42,
 *                                     task #612).
 *
 * The engine half is tools/mmpz-git/mmpz_git.py (the merge driver, diff,
 * conflict reporter and audible-diff CLI); this file is the thin control-surface
 * wrapper that makes it drivable through --control-socket.
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
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

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
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("mmpz-git tool not found. Set MMPZ_GIT_TOOL to the path of "
			"mmpz_git.py, or ensure the source tree is beside the build directory."));
}

/*! Runs the Python tool with the given args and returns its stdout, stderr and
 *  exit code.  Every call is bounded: 120 s for reads, 600 s for audible-diff.
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
	// audible-diff needs the renderer; point it at this binary
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

// ---------------------------------------------------------------------------
// project.merge
// ---------------------------------------------------------------------------

void registerProjectMerge(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.merge");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("merge");
	cmd.description = QStringLiteral("3-way merge of LMMS project files using the mmpz-git "
		"merge driver.  Operates on files, not the running session.  Returns the conflict "
		"count; 0 means a clean merge.  The merged result is written to the 'ours' path.");
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
	});
	cmd.mutating = false; // does not change the running session
	cmd.handler = [](const QJsonObject& args) {
		ToolPaths tp = resolveTool();
		if (tp.python.isEmpty() || tp.tool.isEmpty()) { return toolMissing(); }

		const QString base = args.value(QStringLiteral("base")).toString();
		const QString ours = args.value(QStringLiteral("ours")).toString();
		const QString theirs = args.value(QStringLiteral("theirs")).toString();
		for (const QString& p : {base, ours, theirs})
		{
			if (!QFile::exists(p))
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("no such file: %1").arg(p));
			}
		}

		QStringList toolArgs{QStringLiteral("merge"), base, ours, theirs};
		const QString report = args.value(QStringLiteral("report")).toString();
		if (!report.isEmpty())
		{
			toolArgs << QStringLiteral("--report") << report;
		}

		RunResult r = runTool(toolArgs, 120000);
		if (!r.finished)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git merge did not finish in time"));
		}

		QJsonObject result;
		result.insert(QStringLiteral("merged_file"), ours);
		result.insert(QStringLiteral("conflicts"), r.exitCode == 0 ? 0 : (r.exitCode == 1 ? 1 : 0));
		result.insert(QStringLiteral("report_path"), report);
		if (r.exitCode == 2)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git merge failed: %1").arg(r.stderrText.left(400)));
		}
		return ControlResult::success(result);
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
	cmd.description = QStringLiteral("Semantic diff of two LMMS project files. "
		"Returns an operation list (added, removed, changed, moved).");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("a"), stringProperty()},
			{QStringLiteral("b"), stringProperty()}},
		{QStringLiteral("a"), QStringLiteral("b")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("operations"), arrayProperty()},
		{QStringLiteral("exit_code"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ToolPaths tp = resolveTool();
		if (tp.python.isEmpty() || tp.tool.isEmpty()) { return toolMissing(); }

		const QString a = args.value(QStringLiteral("a")).toString();
		const QString b = args.value(QStringLiteral("b")).toString();
		for (const QString& p : {a, b})
		{
			if (!QFile::exists(p))
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("no such file: %1").arg(p));
			}
		}

		RunResult r = runTool({QStringLiteral("diff"), a, b}, 120000);
		if (!r.finished)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git diff did not finish in time"));
		}

		QJsonArray ops;
		for (const QString& line : r.stdoutText.split(QStringLiteral("\n")))
		{
			if (!line.isEmpty()) { ops.append(line); }
		}
		QJsonObject result;
		result.insert(QStringLiteral("operations"), ops);
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
	cmd.description = QStringLiteral("Report the musical conflicts marked in a merged "
		"project file by the mmpz-git merge driver.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("file"), stringProperty()}},
		{QStringLiteral("file")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("conflict_count"), integerProperty()},
		{QStringLiteral("report"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ToolPaths tp = resolveTool();
		if (tp.python.isEmpty() || tp.tool.isEmpty()) { return toolMissing(); }

		const QString file = args.value(QStringLiteral("file")).toString();
		if (!QFile::exists(file))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no such file: %1").arg(file));
		}

		RunResult r = runTool({QStringLiteral("conflicts"), file}, 120000);
		if (!r.finished)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git conflicts did not finish in time"));
		}

		QJsonObject result;
		result.insert(QStringLiteral("report"), r.stdoutText + r.stderrText);
		result.insert(QStringLiteral("conflict_count"), r.exitCode == 0 ? 0 : 1);
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
	cmd.description = QStringLiteral("Render two project files and report which bars "
		"of which track differ.  Requires the built binary to be findable (the tool "
		"uses this binary as the renderer).  This is a long-running command: it "
		"spawns renders and compares them bar-by-bar.");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("a"), stringProperty()},
			{QStringLiteral("b"), stringProperty()},
			{QStringLiteral("track"), stringProperty()},
			{QStringLiteral("mix_only"), booleanProperty()}},
		{QStringLiteral("a"), QStringLiteral("b")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("differ"), booleanProperty()},
		{QStringLiteral("report"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ToolPaths tp = resolveTool();
		if (tp.python.isEmpty() || tp.tool.isEmpty()) { return toolMissing(); }

		const QString a = args.value(QStringLiteral("a")).toString();
		const QString b = args.value(QStringLiteral("b")).toString();
		for (const QString& p : {a, b})
		{
			if (!QFile::exists(p))
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("no such file: %1").arg(p));
			}
		}

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

		RunResult r = runTool(toolArgs, 600000);
		if (!r.finished)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mmpz-git audible-diff did not finish in time"));
		}

		QJsonObject result;
		result.insert(QStringLiteral("report"), r.stdoutText + r.stderrText);
		result.insert(QStringLiteral("differ"), r.exitCode != 0);
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
