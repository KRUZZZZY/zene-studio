/*
 * ControlCommandsRenderStems.cpp - `render.stems`, the stem-export verb
 *                                   (SPEC A11-A16).
 *
 * The engine landed with the stem-export wave (commit 117068e76; docs/STEM-EXPORT.md)
 * and never got an id: a grep for `render.` / `stem.` over the registry returned only
 * `render.render`. This file is the registration only, and it drives the engine exactly
 * as it already stands:
 *
 *   include/RenderManager.h:40-52   struct StemExportOptions { int tailBars = 1; bool alignToProjectLength = true; }
 *   include/RenderManager.h:76      void RenderManager::exportStems(const StemExportOptions& options = {});
 *   src/core/RenderManager.cpp:148-183  the engine body (mute-isolate per track, project length + tail)
 *   src/core/main.cpp:381, :1002-1008, :1033-1036  the shipped `exportstems` CLI
 *   docs/STEM-EXPORT.md:91-93, :137-143  the contract: per-track, post-fader, post-effects, with the
 *                                        project's own one-bar tail by default
 *
 * WHY A CHILD PROCESS, and not `RenderManager::exportStems()` in this instance: the
 * same reason `render.render` uses one. An in-process render drives THIS instance's
 * audio engine (ProjectRenderer calls audioEngine()->startProcessing()/
 * stopProcessing(), which owns the device thread) and leaves the running instance
 * unable to quit cleanly - the rejection is commented at
 * src/core/ControlCommandsProject.cpp:327-333. The session is serialised first, so the
 * stems are the audio truth of exactly the state an agent has built.
 *
 * THE DECLARED BOUND, stated rather than hidden. `src/core/ControlCommandsProject.cpp`
 * :305 blocks the dispatch thread on `waitForFinished(600000)`, and every control
 * handler runs ON that thread (ControlRegistry::invoke runs inline when the caller is
 * the app thread), so the control surface is DEAD for the whole lifetime of the render
 * child - `control.ping` included. docs/RENDER-CHILD-WAIT.md:120-126 states it as a
 * product defect and :128-141 designs the deferred-reply fix. THIS LANE DOES NOT BUILD
 * THAT FIX. `render.stems` therefore carries the identical bound and says so in its own
 * description and in docs/KNOWN-LIMITATIONS.md; the ctest that exercises it
 * (tests/control-stem-export-verb.py) scopes its budget per command, exactly the way
 * tests/freeze_bounce_evidence.py does for the four commands that already render.
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

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectRenderer.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The stem sample rate this surface renders at. One number, named once, and
//! reported in the result so a caller never has to assume it - the same choice
//! `render.render` makes (ControlCommandsProject.cpp:357).
constexpr int kStemSampleRate = 44100;

//! The stem export's own bound, in milliseconds: the child's start (30 s, the
//! whole engine construction) plus the render itself. Named here rather than inlined
//! so the number in the code and the number in the row's limits line cannot drift.
constexpr int kStemChildStartMs = 30000;
constexpr int kStemChildRenderMs = 600000;

bool stemFormatFromName(const QString& name, ProjectRenderer::ExportFileFormat* format)
{
	if (name == QLatin1String("wav")) { *format = ProjectRenderer::ExportFileFormat::Wave; return true; }
	if (name == QLatin1String("flac")) { *format = ProjectRenderer::ExportFileFormat::Flac; return true; }
	if (name == QLatin1String("ogg")) { *format = ProjectRenderer::ExportFileFormat::Ogg; return true; }
	if (name == QLatin1String("mp3")) { *format = ProjectRenderer::ExportFileFormat::MP3; return true; }
	return false;
}

//! What the handler resolved out of its arguments.
struct StemRequest
{
	QString directory;
	QString formatName;
	ProjectRenderer::ExportFileFormat format = ProjectRenderer::ExportFileFormat::Wave;
	int tailBars = 1;
};

/*! Validates the stem args; an empty string means they are usable.
 *
 *  `out` is a DIRECTORY here and not a file, because a stem export writes many files
 *  and the shipped CLI refuses to guess a destination next to the project
 *  (src/core/main.cpp:1002-1008). The refusal is typed and happens before anything is
 *  written or spawned.
 */
QString parseStemArgs(const QJsonObject& args, StemRequest* request)
{
	request->directory = args.value(QStringLiteral("out")).toString();
	if (!request->directory.startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("'out' must be an absolute directory path (the stem export writes "
			"many files, so it never guesses a destination)");
	}
	if (QFileInfo(request->directory).exists() && !QFileInfo(request->directory).isDir())
	{
		return QStringLiteral("'out' names an existing file, not a directory");
	}
	request->formatName = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	if (!stemFormatFromName(request->formatName, &request->format))
	{
		return QStringLiteral("unsupported format '%1'").arg(request->formatName);
	}
	if (args.contains(QStringLiteral("tail_bars")))
	{
		const double bars = args.value(QStringLiteral("tail_bars")).toDouble();
		if (bars < 0.0)
		{
			return QStringLiteral("'tail_bars' %1 is negative: the tail is bars rendered PAST the "
				"project end, so it cannot be negative").arg(bars);
		}
		request->tailBars = static_cast<int>(bars);
	}
	return QString();
}

//! Runs the shipped `exportstems` CLI path as a child process.
bool runCliStemExport(const QString& projectPath, const StemRequest& request, int* exitCode)
{
	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(QProcess::nullDevice());
	renderer.start(QCoreApplication::applicationFilePath(),
		{QStringLiteral("exportstems"), projectPath,
			QStringLiteral("-o"), request.directory,
			QStringLiteral("-f"), request.formatName,
			QStringLiteral("-s"), QString::number(kStemSampleRate),
			QStringLiteral("--tail-bars"), QString::number(request.tailBars)});
	const bool finished = renderer.waitForStarted(kStemChildStartMs)
		&& renderer.waitForFinished(kStemChildRenderMs);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;
	return finished;
}

//! The directory's direct child files, by name -> last modified, so two listings
//! can be diffed by CONTENT and not only by name.
QHash<QString, QDateTime> listingOf(const QString& directory)
{
	QHash<QString, QDateTime> out;
	const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files, QDir::Name);
	for (const QFileInfo& entry : entries)
	{
		out.insert(entry.fileName(), entry.lastModified());
	}
	return out;
}

/*! The files \p after holds that \p before does not, or that the run REWROTE.
 *
 *  Reporting the DELTA rather than the whole directory matters twice: the directory
 *  may be one a caller shared with an earlier export, and claiming another call's
 *  files would be a lie the socket cannot catch - while a RE-export into the same
 *  directory overwrites the same names, and a name-only diff would then report
 *  nothing and this command would refuse a render it had actually performed.
 */
QJsonArray writtenStems(const QHash<QString, QDateTime>& before,
	const QHash<QString, QDateTime>& after)
{
	QStringList names = after.keys();
	names.sort();
	QJsonArray stems;
	for (const QString& name : names)
	{
		if (!before.contains(name) || before.value(name) != after.value(name))
		{
			stems.append(name);
		}
	}
	return stems;
}

ControlResult stemExportSession(const QJsonObject& args)
{
	StemRequest request;
	const QString invalid = parseStemArgs(args, &request);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}
	if (Engine::getSong()->isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the session is empty: there is nothing to export stems from"));
	}

	// Serialise first, exactly as render.render does: the stems must be the audio
	// truth of the state an agent has built, including edits it has not saved.
	const QString tempProject = QDir::tempPath() +
		QStringLiteral("/zene-stems-%1-%2.mmp").arg(QCoreApplication::applicationPid())
			.arg(QDateTime::currentMSecsSinceEpoch());
	if (!Engine::getSong()->saveProjectFile(tempProject))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not serialise the session for the stem export"));
	}

	// The CLI creates the directory itself (QDir().mkpath, src/core/main.cpp:1012), so
	// a listing taken before the run is simply empty for a directory that does not
	// exist yet - which is the right baseline for the delta.
	const QHash<QString, QDateTime> before = listingOf(request.directory);
	int exitCode = -1;
	const bool finished = runCliStemExport(tempProject, request, &exitCode);
	QFile::remove(tempProject);

	const QHash<QString, QDateTime> after = listingOf(request.directory);
	const QJsonArray stems = writtenStems(before, after);
	if (!finished || exitCode != 0 || stems.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the stem export failed (exit %1, %2 new file(s) in %3); the session is "
				"unchanged").arg(exitCode).arg(stems.size()).arg(request.directory));
	}

	QJsonObject result;
	result.insert(QStringLiteral("directory"), request.directory);
	result.insert(QStringLiteral("format"), request.formatName);
	result.insert(QStringLiteral("sample_rate"), kStemSampleRate);
	result.insert(QStringLiteral("tail_bars"), request.tailBars);
	result.insert(QStringLiteral("count"), stems.size());
	result.insert(QStringLiteral("stems"), stems);
	return ControlResult::success(result);
}

void registerRenderStems(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("render.stems");
	cmd.group = QStringLiteral("render");
	cmd.verb = QStringLiteral("stems");
	cmd.description = QStringLiteral("Export every unmuted track to its own file in an absolute "
		"directory: one stem per track, post-fader and post-effects, including that track's own "
		"sends and their tails, each rendered to the project's length plus 'tail_bars' bars (default "
		"1, the whole-project render's own convention). Returns the new file names. NOTE the "
		"declared bound: the render runs in a child process and this surface does not answer - not "
		"even control.ping - until it finishes (docs/KNOWN-LIMITATIONS.md).");
	// The declared bound, in the description above and in the argument contract here:
	// a caller can lower it by asking for fewer stems or a smaller project, but it
	// cannot be raised away, so there is no `timeout` knob to pretend it is bounded.
	cmd.argsSchema = objectSchema({
		{QStringLiteral("out"), stringProperty()},
		{QStringLiteral("format"), enumProperty({QStringLiteral("wav"), QStringLiteral("flac"),
			QStringLiteral("ogg"), QStringLiteral("mp3")})},
		{QStringLiteral("tail_bars"), integerProperty(0, 64)},
	}, {QStringLiteral("out")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("directory"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("tail_bars"), integerProperty(0, 64)},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("stems"), arrayProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemExportSession(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRenderStemsCommands(ControlRegistry& registry)
{
	registerRenderStems(registry);
}

} // namespace lmms
