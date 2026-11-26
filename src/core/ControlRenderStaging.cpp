/*
 * ControlRenderStaging.cpp - how a render the control surface runs is staged and
 *                            published.
 *
 * The design (the child writes a staged file, published only on success) is
 * described in the header; runCliRender in ControlCommandsProject.cpp is the one
 * caller.
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

#include "ControlRenderStaging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QUuid>

#include "LoudnessReport.h"

namespace lmms
{

namespace
{

/*! Removes a staged render and its loudness sidecar; false when either is still there.
 *
 *  Called only once the child is gone. Windows may still refuse to delete a file a
 *  just-terminated process held open for a moment, so the removal is retried briefly;
 *  a file that stays is reported by the caller, never silently left.
 */
bool discardStagedRender(const QString& staged)
{
	const QStringList files{staged, LoudnessReport::sidecarPathFor(staged)};
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		bool gone = true;
		for (const QString& file : files)
		{
			if (QFile::exists(file) && !QFile::remove(file)) { gone = false; }
		}
		if (gone) { return true; }
		QThread::msleep(100);
	}
	return false;
}

//! The text one symlink holds: raw (possibly relative) where Qt can say so.
QString symlinkText(const QFileInfo& link)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	return link.readSymLink();
#else
	return link.symLinkTarget(); // one hop, made absolute against the link's own folder
#endif
}

/*! The loudness report moved beside the caller's own path, naming that path inside it.
 *
 *  The child writes the report beside the file it was told to render, so its `file`
 *  field carries the staged name; the caller asked for `out` and reads a report that
 *  names it, exactly as it did when the child rendered straight to `out`.
 */
void publishRenderSidecar(const QString& staged, const QString& out)
{
	const QString sidecar = LoudnessReport::sidecarPathFor(staged);
	if (!QFile::exists(sidecar)) { return; }
	const QString published = LoudnessReport::sidecarPathFor(out);
	QFile::remove(published);
	if (!QFile::rename(sidecar, published)) { return; }
	QFile report(published);
	if (!report.open(QIODevice::ReadOnly | QIODevice::Text)) { return; }
	QString text = QString::fromUtf8(report.readAll());
	report.close();
	if (!text.contains(staged)) { return; }
	text.replace(staged, out);
	if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { return; }
	report.write(text.toUtf8());
}

/*! The staged render moved over `destination`, the file `out` resolves to; false when it could not be.
 *
 *  A file already at the destination is moved aside first and put back if the move
 *  fails, so a failed publish leaves a destination rather than neither file: the rule
 *  RenderQueue::commitStaged applies to its own jobs. A destination that is not a file
 *  (a directory, say) is never moved aside - the child could not have written there
 *  either, and the caller's directory stays where it is. The destination is the end
 *  of any symlink chain at `out`, so every link in it stays a link.
 */
bool publishStagedRender(const QString& staged, const QString& destination, const QString& out)
{
	const QFileInfo existing(destination);
	if (existing.exists() && !existing.isFile()) { return false; }
	QString aside;
	if (existing.isFile())
	{
		aside = existing.dir().filePath(QStringLiteral(".zene-backup-")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
		if (!QFile::rename(destination, aside)) { return false; }
	}
	if (!QFile::rename(staged, destination))
	{
		if (!aside.isEmpty() && !QFile::exists(destination)) { QFile::rename(aside, destination); }
		return false;
	}
	if (!aside.isEmpty()) { QFile::remove(aside); }
	// The loudness report travels with the audio it describes.
	publishRenderSidecar(staged, out);
	return true;
}

//! The child's exit code as the callers read it: -1 when it never finished, and never
//! 0 for a child that crashed - a crash is not a success whatever code it carries.
int renderExitCode(const QProcess& renderer, bool finished)
{
	if (!finished) { return -1; }
	const int code = renderer.exitCode();
	return renderer.exitStatus() == QProcess::NormalExit || code != 0 ? code : -1;
}

/*! Ends a render that did not succeed, its staged files removed once the child is gone.
 *
 *  A child that did not finish is killed and waited for FIRST: until it is gone it may
 *  still hold the staged file open (Windows refuses the removal then) or still be
 *  writing it. A staged file that cannot be removed is named in `diagnostics` rather
 *  than left without a word.
 */
void abandonCliRender(QProcess& renderer, bool finished, const QString& staged, QString* diagnostics)
{
	if (!finished)
	{
		renderer.kill();
		renderer.waitForFinished(30000);
	}
	const bool gone = renderer.state() == QProcess::NotRunning;
	if (gone && discardStagedRender(staged)) { return; }
	const QString left = QStringLiteral("a partial render could not be removed: %1").arg(staged);
	*diagnostics = diagnostics->isEmpty() ? left : *diagnostics + QStringLiteral("; ") + left;
}

} // namespace

//! See ControlRenderStaging.h.
QString stagedRenderPath(const QString& target, const QString& formatName)
{
	return QFileInfo(target).dir().filePath(QStringLiteral(".zene-render-")
		+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(12)
		+ QLatin1Char('.') + formatName);
}

//! See ControlRenderStaging.h.
bool resolveRenderTarget(const QString& out, QString* target)
{
	QString path = out;
	for (int hop = 0; hop < MaxRenderSymlinkHops && QFileInfo(path).isSymLink(); ++hop)
	{
		const QFileInfo link(path);
		const QString next = symlinkText(link);
		if (next.isEmpty()) { return false; }
		path = QDir::isAbsolutePath(next) ? next : link.dir().filePath(next);
	}
	if (QFileInfo(path).isSymLink()) { return false; }
	*target = path;
	return true;
}

//! See ControlRenderStaging.h.
int renderTimeoutMs()
{
	bool ok = false;
	const int requested = qEnvironmentVariableIntValue("ZENE_CONTROL_RENDER_TIMEOUT_MS", &ok);
	return ok && requested > 0 ? requested : 600000;
}

//! See ControlRenderStaging.h.
bool finishCliRender(QProcess& renderer, bool finished, const QString& staged,
	const QString& target, const QString& out, int* exitCode, QString* diagnostics)
{
	*exitCode = renderExitCode(renderer, finished);
	if (*exitCode != 0)
	{
		abandonCliRender(renderer, finished, staged, diagnostics);
		return finished;
	}
	if (!publishStagedRender(staged, target, out))
	{
		*exitCode = -1;
		*diagnostics = QStringLiteral("the finished render could not be moved to %1").arg(out);
		abandonCliRender(renderer, finished, staged, diagnostics);
	}
	return finished;
}

} // namespace lmms
