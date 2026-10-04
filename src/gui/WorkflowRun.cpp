/*
 * WorkflowRun.cpp - running a workflow's steps: where it stopped, what applied, and resuming
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

#include "WorkflowRun.h"

#include <QTimer>

#include "CommandMetadata.h"
#include "ControlWindowCommands.h"
#include "MacroMenu.h"
#include "MainWindow.h"
#include "WorkflowRunReport.h"

namespace lmms::workflow
{

namespace
{

//! The run under way or last finished, and what the earlier runs of it (before its resumes) did.
struct Chain
{
	bool running = false;
	QString name;
	QJsonArray steps;
	QJsonObject outcome;  //!< workflow.status's body
	QJsonArray applied;   //!< 1-based numbers of the steps applied so far, over the resumes
	QJsonArray skipped;   //!< 1-based numbers of the steps a resume skipped
};

Chain& chain()
{
	static Chain value;
	return value;
}

QJsonObject stepEntry(int index, const QJsonObject& step)
{
	const QString command = step.value(QStringLiteral("command")).toString();
	return {{QStringLiteral("step"), index + 1}, {QStringLiteral("command"), command}, {QStringLiteral("name"), gui::commandName(command)}};
}

QJsonArray numberedSteps(const QJsonArray& steps)
{
	QJsonArray out;
	for (int i = 0; i < steps.size(); ++i) { out.append(stepEntry(i, steps.at(i).toObject())); }
	return out;
}

//! "1, 2 and 4" - step numbers as a sentence lists them.
QString listed(const QJsonArray& numbers)
{
	QStringList words;
	for (const QJsonValue& number : numbers) { words.append(QString::number(number.toInt())); }
	if (words.size() < 2) { return words.join(QString()); }
	const QString last = words.takeLast();
	return words.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
}

//! "Step 3" or "Steps 1 and 2": @a numbers after the word, singular or plural.
QString steps(const QJsonArray& numbers)
{
	return (numbers.size() > 1 ? QStringLiteral("steps ") : QStringLiteral("step ")) + listed(numbers);
}

//! What applied and what was skipped, as the sentence's middle: "Steps 1 and 2 applied and stay applied ...".
QString appliedText(const QJsonObject& out)
{
	const QJsonArray applied = out.value(QStringLiteral("applied_steps")).toArray();
	const QJsonArray skipped = out.value(QStringLiteral("skipped_steps")).toArray();
	QString text = applied.isEmpty() ? QStringLiteral("No step applied")
		: QStringLiteral("Applied, and still applied: %1 (undo each with Edit > Undo)").arg(steps(applied));
	if (!skipped.isEmpty()) { text += QStringLiteral("; skipped: %1").arg(steps(skipped)); }
	return text;
}

//! The outcome as one sentence a person reads: which step failed and why, what applied, what did not run.
QString message(const QJsonObject& out)
{
	const QString name = out.value(QStringLiteral("name")).toString();
	if (!out.contains(QStringLiteral("failed")))
	{
		return QStringLiteral("Workflow '%1' finished. %2.").arg(name, appliedText(out));
	}
	const QJsonObject failed = out.value(QStringLiteral("failed")).toObject();
	const QJsonArray notRun = out.value(QStringLiteral("not_run")).toArray();
	const QString rest = notRun.isEmpty() ? QString() : QStringLiteral("; not run: %1").arg(steps(notRun));
	return QStringLiteral("Workflow '%1' stopped at step %2 of %3 (%4): %5. %6%7. Retry the step, or skip it and continue.")
		.arg(name).arg(failed.value(QStringLiteral("step")).toInt()).arg(out.value(QStringLiteral("total")).toInt())
		.arg(failed.value(QStringLiteral("name")).toString(), failed.value(QStringLiteral("error")).toString(), appliedText(out), rest);
}

//! The failed step's report: its place, command (id and name), the reason and the arguments it was given.
QJsonObject failure(int index, const QJsonObject& result)
{
	QJsonObject out = stepEntry(index, chain().steps.at(index).toObject());
	out.insert(QStringLiteral("error"), result.value(QStringLiteral("error")));
	out.insert(QStringLiteral("args"), chain().steps.at(index).toObject().value(QStringLiteral("args")));
	return out;
}

QJsonObject outcome(int from, int failedAt, const QJsonArray& results)
{
	const Chain& now = chain();
	QJsonObject out{{QStringLiteral("name"), now.name}, {QStringLiteral("state"), QStringLiteral("done")},
		{QStringLiteral("total"), now.steps.size()}, {QStringLiteral("from"), from}, {QStringLiteral("ran"), results.size()},
		{QStringLiteral("stopped_at"), failedAt}, {QStringLiteral("results"), results}, {QStringLiteral("steps"), numberedSteps(now.steps)},
		{QStringLiteral("applied_steps"), now.applied}, {QStringLiteral("skipped_steps"), now.skipped},
		{QStringLiteral("resumable"), failedAt >= 0}};
	if (failedAt >= 0)
	{
		QJsonArray notRun;
		for (int i = failedAt + 1; i < now.steps.size(); ++i) { notRun.append(i + 1); }
		out.insert(QStringLiteral("not_run"), notRun);
		out.insert(QStringLiteral("failed"), failure(failedAt, results.last().toObject()));
	}
	out.insert(QStringLiteral("message"), message(out));
	return out;
}

//! Where the user can see it: the Workflows menu's status line and the run's report (WorkflowRunReport).
void show(const QJsonObject& out)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr || ControlRegistry::instance()->isHeadless()) { return; }
	gui::MacroMenu::refreshIn(window);
	gui::WorkflowRunReport::showOutcome(window, out);
}

//! The steps from @a from on, each a top-level registry call of its own; the outcome lands in the chain.
void runSteps(int from)
{
	Chain& now = chain();
	QJsonArray results;
	int failedAt = -1;
	for (int i = from; i < now.steps.size() && failedAt < 0; ++i)
	{
		const QJsonObject step = now.steps.at(i).toObject();
		const ControlResult ran = ControlRegistry::instance()->invoke(step.value(QStringLiteral("command")).toString(),
			step.value(QStringLiteral("args")).toObject());
		QJsonObject entry = stepEntry(i, step);
		entry.insert(QStringLiteral("ok"), ran.ok);
		if (ran.ok) { now.applied.append(i + 1); }
		else
		{
			entry.insert(QStringLiteral("error"), ran.errorMessage);
			failedAt = i;
		}
		results.append(entry);
	}
	now.running = false;
	now.outcome = outcome(from, failedAt, results);
	show(now.outcome);
}

ControlResult queue(int from)
{
	Chain& now = chain();
	now.running = true;
	now.outcome = {{QStringLiteral("name"), now.name}, {QStringLiteral("state"), QStringLiteral("running")},
		{QStringLiteral("from"), from}, {QStringLiteral("total"), now.steps.size()}};
	// The steps run on the next turn of the event loop, each as a top-level command: run inside this handler, the
	// registry would fold every step's undo step into this command's while each step kept a transaction record of
	// its own, and control.undo would then disagree with Ctrl+Z.
	QTimer::singleShot(0, ControlRegistry::instance(), [from] { runSteps(from); });
	return ControlResult::success({{QStringLiteral("name"), now.name}, {QStringLiteral("queued"), now.steps.size() - from},
		{QStringLiteral("from"), from}, {QStringLiteral("poll"), QStringLiteral("workflow.status")}});
}

ControlResult alreadyRunning()
{
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a workflow is already running"));
}

} // namespace

bool running()
{
	return chain().running;
}

ControlResult start(const QString& name, const QJsonArray& steps)
{
	if (running()) { return alreadyRunning(); }
	Chain& now = chain();
	now = Chain{};  // a new run: nothing applied or skipped yet
	now.name = name;
	now.steps = steps;
	return queue(0);
}

ControlResult resume(bool skip, const QJsonObject& library)
{
	if (running()) { return alreadyRunning(); }
	Chain& now = chain();
	const int failedAt = now.outcome.value(QStringLiteral("stopped_at")).toInt(-1);
	if (now.outcome.value(QStringLiteral("state")).toString() != QStringLiteral("done") || failedAt < 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the last workflow run did not stop at a failing step: "
			"there is nothing to resume"));
	}
	// What the library holds now: a workflow edited since it stopped resumes as it is now, from the same step.
	if (!library.contains(now.name))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("workflow '%1' is no longer saved").arg(now.name));
	}
	now.steps = library.value(now.name).toObject().value(QStringLiteral("steps")).toArray();
	const int from = failedAt + (skip ? 1 : 0);
	if (from > now.steps.size())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("workflow '%1' has %2 steps now: step %3 is gone")
			.arg(now.name).arg(now.steps.size()).arg(failedAt + 1));
	}
	if (skip) { now.skipped.append(failedAt + 1); }
	ControlResult queued = queue(from);
	queued.result.insert(skip ? QStringLiteral("skipped") : QStringLiteral("retried"), failedAt + 1);
	return queued;
}

QJsonObject lastRun()
{
	const QJsonObject& out = chain().outcome;
	return out.isEmpty() ? QJsonObject{{QStringLiteral("state"), QStringLiteral("idle")}} : out;
}

} // namespace lmms::workflow
