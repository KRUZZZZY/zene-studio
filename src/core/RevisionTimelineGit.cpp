/*
 * RevisionTimelineGit.cpp - the project's own git history as revision entries,
 *                          and the bytes of one commit's copy of it
 *                          (feature-list row 76's third artefact).
 *
 * Bounded by RevisionTimelineBounds::GitTimeoutMs, because a control handler
 * runs on the UI thread and an unbounded child would stall the whole surface.
 * Optional in both directions: a machine without git, a project outside any
 * repository, and a command that fails all produce NO entries and a `git` object
 * that says which - a timeline is never failed by its git half.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "RevisionTimelineGit.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>

namespace lmms
{
namespace control
{
namespace timelineGit
{

namespace
{

using Bounds = RevisionTimelineBounds;

//! The `git` to run, resolved once (QStandardPaths caches behind the scenes).
//! Empty when the machine has none, which is one of the reasons a timeline
//! reports no git entries.
QString gitExecutable()
{
	static const QString path = QStandardPaths::findExecutable(QStringLiteral("git"));
	return path;
}

//! Runs \a args under \a dir, bounded by the shared timeout. False when git is
//! absent, the command could not start, it timed out, or it exited non-zero -
//! every one of which means "no git entries" and never "a failed timeline".
bool runGit(const QString& dir, const QStringList& args, QByteArray* out)
{
	const QString git = gitExecutable();
	if (git.isEmpty()) { return false; }
	QProcess process;
	process.setWorkingDirectory(dir);
	process.setStandardErrorFile(QProcess::nullDevice());
	process.start(git, args);
	if (!process.waitForStarted(Bounds::GitTimeoutMs)) { return false; }
	if (!process.waitForFinished(Bounds::GitTimeoutMs))
	{
		process.kill();
		process.waitForFinished();
		return false;
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) { return false; }
	*out = process.readAllStandardOutput();
	return true;
}

//! The reason a `git` report carries when no history could be read.
QString noHistoryReason(const QString& projectPath)
{
	if (gitExecutable().isEmpty())
	{
		return QStringLiteral("no 'git' executable on this machine: the timeline lists the "
			"artefacts on disk only");
	}
	return QStringLiteral("git could not report a history for %1 (not a repository, no commits "
		"for the file, or the command failed within %2 ms)")
		.arg(projectPath).arg(Bounds::GitTimeoutMs);
}

//! One "git:<short-sha>" entry per line of `git log`'s %h/%ct/%s output, which
//! arrive newest first. A line that does not carry all three fields is skipped
//! rather than guessed at.
QVector<RevisionEntry> parseGitLog(const QByteArray& out)
{
	QVector<RevisionEntry> entries;
	const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	for (const QString& line : lines)
	{
		const QStringList fields = line.split(QChar(0x1f));
		if (fields.size() < 3) { continue; }
		RevisionEntry entry;
		entry.id = QStringLiteral("git:") + fields.at(0);
		entry.source = QStringLiteral("git");
		entry.commit = fields.at(0);
		entry.timestamp = QDateTime::fromSecsSinceEpoch(fields.at(1).toLongLong()).toUTC();
		entry.note = fields.at(2);
		entries.append(entry);
	}
	return entries;
}

} // namespace

QVector<RevisionEntry> entries(const QString& projectPath, QJsonObject* report)
{
	const QFileInfo project(projectPath);
	const QString dir = project.absolutePath();
	const QStringList args{QStringLiteral("-C"), dir, QStringLiteral("log"),
		QStringLiteral("--max-count=%1").arg(Bounds::MaxGitEntries),
		QStringLiteral("--format=%h%x1f%ct%x1f%s"), QStringLiteral("--"),
		project.absoluteFilePath()};

	QByteArray out;
	if (!runGit(dir, args, &out))
	{
		report->insert(QStringLiteral("in_repository"), false);
		report->insert(QStringLiteral("reason"), noHistoryReason(projectPath));
		return QVector<RevisionEntry>();
	}

	const QVector<RevisionEntry> entries = parseGitLog(out);
	report->insert(QStringLiteral("in_repository"), !entries.isEmpty());
	report->insert(QStringLiteral("listed"), entries.size());
	return entries;
}

bool bytesOfCommit(const QString& projectPath, const QString& commit, QByteArray* bytes,
	QString* error)
{
	const QFileInfo project(projectPath);
	const QString dir = project.absolutePath();
	const QString spec = commit + QStringLiteral(":./") + project.fileName();
	const QStringList args{QStringLiteral("-C"), dir, QStringLiteral("cat-file"),
		QStringLiteral("blob"), spec};
	if (!runGit(dir, args, bytes))
	{
		*error = QStringLiteral("git could not read %1 (the commit, or the project's path in "
			"it, is not in this repository)").arg(spec);
		return false;
	}
	if (bytes->size() > Bounds::MaxRevisionBytes)
	{
		*error = QStringLiteral("the revision in %1 is %2 bytes, over the %3-byte revision cap")
			.arg(spec).arg(bytes->size()).arg(Bounds::MaxRevisionBytes);
		return false;
	}
	return true;
}

} // namespace timelineGit
} // namespace control
} // namespace lmms
