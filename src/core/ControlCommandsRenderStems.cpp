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
 * handler runs ON that thread, so the control surface is DEAD for the whole lifetime of
 * the render child - `control.ping` included. docs/RENDER-CHILD-WAIT.md:120-126 states
 * it as a product defect and :128-141 designs the deferred-reply fix. THIS LANE DOES NOT
 * BUILD THAT FIX. `render.stems` therefore carries the identical bound and says so in
 * its own description and in docs/KNOWN-LIMITATIONS.md.
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
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>

#include "ControlEdit.h"
#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectRenderer.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The stem export's own bound, in milliseconds: the child's start (30 s, the
//! whole engine construction) plus the render itself. Named here rather than inlined
//! so the number in the code and the number in the row's limits line cannot drift.
constexpr int kStemChildStartMs = 30000;
constexpr int kStemChildRenderMs = 600000;
//! How much of the child's own words a refusal keeps: the TAIL of its
//! standard error. The child's error text used to be discarded together with
//! its output, so a caller learned only THAT the export failed and never WHY.
constexpr int kStemChildErrorBytes = 2048;

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
	//! 0.5.0 L12 (Q050-009): the export settings in force - the applied render
	//! preset's rate, bit depth and stereo mode, or the render path's own
	//! defaults. Read like render.render reads them, so a stem export follows
	//! the same selected export rate and bit depth.
	ControlExportPreset settings;
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
	if (!QFileInfo(request->directory).isAbsolute())
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
	// The settings the next render uses: the preset applied through
	// export.preset_apply, or the render path's own defaults. Read HERE, like
	// render.render, so a selected export preset's rate and bit depth reach the
	// stems instead of the fixed 44.1 kHz this verb used to pass.
	request->settings = ControlExportPresetSettings::effective();
	return QString();
}

//! The last \a maxBytes of the file at \a path, as text. The child's own output
//! is read back from a FILE and not from a pipe: a pipe fills up and blocks the
//! child (the reason the whole-project render reads a file, too -
//! src/core/ControlCommandsProject.cpp:326).
QString tailOfFile(const QString& path, int maxBytes)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	const qint64 size = file.size();
	if (size > maxBytes) { file.seek(size - maxBytes); }
	return QString::fromLocal8Bit(file.read(maxBytes)).trimmed();
}

/*! Runs the shipped `exportstems` CLI path as a child process.
 *
 *  \a outputDirectory is the directory the child writes into - the caller's own,
 *  or the staging directory this verb renders into first. \a errorTail receives
 *  the child's OWN last words, bounded: a refusal that says only "the export
 *  failed" leaves the caller with the exit code and nothing to act on, while
 *  the reason it failed is exactly what the child wrote to its standard error.
 */
bool runCliStemExport(const QString& projectPath, const StemRequest& request,
	const QString& outputDirectory, int* exitCode, QString* errorTail)
{
	QStringList arguments{QStringLiteral("exportstems"), projectPath,
		QStringLiteral("-o"), outputDirectory,
		QStringLiteral("-f"), request.formatName};
	arguments += controlExportPresetRenderArgs(request.settings);
	arguments << QStringLiteral("--tail-bars") << QString::number(request.tailBars);

	// The child's error text goes to a FILE, not to a pipe (see tailOfFile):
	// the child must never block on a full pipe, and the file is read back
	// afterwards, bounded.
	QTemporaryFile errorFile;
	errorFile.open();
	errorFile.close();

	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(errorFile.fileName());
	renderer.start(QCoreApplication::applicationFilePath(), arguments);
	const bool finished = renderer.waitForStarted(kStemChildStartMs)
		&& renderer.waitForFinished(kStemChildRenderMs);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;
	*errorTail = tailOfFile(errorFile.fileName(), kStemChildErrorBytes);
	return finished;
}

//! The refusal a failed export answers with: the exit code, what the run wrote,
//! and the child's OWN words - the reason the export failed, which used to be
//! discarded with the child's output.
QString stemExportFailureMessage(int exitCode, int stemCount, const QString& directory,
	const QString& errorTail)
{
	QString message = QStringLiteral("the stem export failed (exit %1, %2 new file(s) in %3); "
		"the session is unchanged").arg(exitCode).arg(stemCount).arg(directory);
	if (!errorTail.isEmpty())
	{
		message += QStringLiteral("; the child said: ") + errorTail;
	}
	return message;
}

//! The staging directory for one export: INSIDE the output directory, so the
//! promotion below is a rename on the SAME filesystem, and so the shipped CLI
//! still creates the output directory itself (src/core/main.cpp:1476).
QString stagingDirectoryFor(const QString& directory)
{
	return QDir(directory).filePath(QStringLiteral(".zene-stems-staging-%1-%2")
		.arg(QCoreApplication::applicationPid())
		.arg(QDateTime::currentMSecsSinceEpoch()));
}

//! Removes the staging directory. Best effort: one left behind is litter, not a failure.
void removeStagingDirectory(const QString& staging)
{
	QDir(staging).removeRecursively();
}

//! Installs one rendered file over \a target. The earlier file is moved ASIDE
//! first and put back when the new one cannot be installed, so a failed
//! promotion never leaves a half-written stem behind.
bool installStagedFile(const QString& staged, const QString& target, const QString& staging,
	QString* error)
{
	const QString name = QFileInfo(target).fileName();
	const QString aside = QDir(staging).filePath(QStringLiteral(".zene-aside-") + name);
	const bool hadTarget = QFile::exists(target);
	if (hadTarget && !QFile::rename(target, aside))
	{
		*error = QStringLiteral("could not move the earlier %1 aside").arg(name);
		return false;
	}
	if (!QFile::rename(staged, target))
	{
		if (hadTarget) { QFile::rename(aside, target); }
		*error = QStringLiteral("could not install the rendered %1").arg(name);
		return false;
	}
	if (hadTarget) { QFile::remove(aside); }
	return true;
}

//! Moves the rendered stems out of the staging directory into the output
//! directory, overwriting what is there, and reports the names it installed.
bool promoteStagedFiles(const QString& staging, const QString& directory,
	QStringList* promoted, QString* error)
{
	const QFileInfoList entries = QDir(staging).entryInfoList(QDir::Files | QDir::Hidden, QDir::Name);
	for (const QFileInfo& entry : entries)
	{
		const QString name = entry.fileName();
		if (name.startsWith(QLatin1String(".zene-"))) { continue; }  // our own aside file
		if (!installStagedFile(entry.absoluteFilePath(), QDir(directory).filePath(name),
			staging, error))
		{
			return false;
		}
		promoted->append(name);
	}
	return true;
}

//! One file in an export directory: its CONTENT digest and its stamp.
struct StemFileState
{
	QString digest;      //!< the SHA-256 of the file's content, hex.
	QDateTime modified;  //!< the stamp - a SECONDARY signal only (see writtenStems).
};

//! The SHA-256 of a file's content, hex; empty when it cannot be read. Hashed
//! in CHUNKS, so a long stem is never held in memory whole.
QString digestOfFile(const QString& path)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	while (!file.atEnd())
	{
		const QByteArray chunk = file.read(64 * 1024);
		if (chunk.isEmpty()) { break; }
		hash.addData(chunk);
	}
	return QString::fromLatin1(hash.result().toHex());
}

//! The directory's direct child files, by name -> state: two listings can then
//! be diffed by CONTENT and not only by name.
QHash<QString, StemFileState> listingOf(const QString& directory)
{
	QHash<QString, StemFileState> out;
	const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files, QDir::Name);
	for (const QFileInfo& entry : entries)
	{
		StemFileState state;
		state.digest = digestOfFile(entry.absoluteFilePath());
		state.modified = entry.lastModified();
		out.insert(entry.fileName(), state);
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
 *
 *  THE CONTENT IS THE SIGNAL, and the stamp is only a secondary one. A file whose
 *  BYTES changed is one the run rewrote, whatever the filesystem's clock did with
 *  the modification time: a rewrite that lands inside the same clock tick leaves
 *  the stamp where it was, and a stamp comparison then reports nothing at all -
 *  the export refuses a render it had performed, with "0 new file(s)" in the
 *  refusal. The stamp stays, because a rewrite of byte-identical content leaves
 *  no trace in the content either, and that rewrite is a write.
 */
QJsonArray writtenStems(const QHash<QString, StemFileState>& before,
	const QHash<QString, StemFileState>& after)
{
	QStringList names = after.keys();
	names.sort();
	QJsonArray stems;
	for (const QString& name : names)
	{
		if (!before.contains(name) || before.value(name).digest != after.value(name).digest
			|| before.value(name).modified != after.value(name).modified)
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

	// The child renders into a STAGING directory inside the output directory,
	// and the complete set is promoted into the output directory only after
	// the child succeeded. A render that fails - or that is killed on the
	// timeout - then leaves the caller's earlier stems exactly as they were,
	// instead of a half-written set in their place.
	const QString staging = stagingDirectoryFor(request.directory);

	// The CLI creates the directory itself (QDir().mkpath, src/core/main.cpp:1012), so
	// a listing taken before the run is simply empty for a directory that does not
	// exist yet - which is the right baseline for the delta.
	const QHash<QString, StemFileState> before = listingOf(request.directory);
	int exitCode = -1;
	QString errorTail;
	const bool finished = runCliStemExport(tempProject, request, staging, &exitCode, &errorTail);
	QFile::remove(tempProject);

	if (!finished || exitCode != 0)
	{
		// Nothing was promoted: the output directory still holds exactly what
		// it held before this run.
		removeStagingDirectory(staging);
		return ControlResult::failure(ControlErrorKind::Refused,
			stemExportFailureMessage(exitCode, 0, request.directory, errorTail));
	}

	// The render is complete: install the whole set, or report that it could
	// not be installed.
	QStringList promoted;
	QString promoteError;
	if (!promoteStagedFiles(staging, request.directory, &promoted, &promoteError))
	{
		removeStagingDirectory(staging);
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the stem export failed (%1); the session is unchanged")
				.arg(promoteError));
	}
	removeStagingDirectory(staging);

	const QHash<QString, StemFileState> after = listingOf(request.directory);
	QJsonArray stems = writtenStems(before, after);
	if (stems.isEmpty())
	{
		// Nothing in the output directory's CONTENT changed - a re-export of an
		// unchanged project writes the same bytes, and a filesystem whose clock
		// does not move leaves the stamp where it was - so the set the run wrote
		// itself is what the caller is told about.
		for (const QString& name : promoted)
		{
			stems.append(name);
		}
	}
	if (stems.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			stemExportFailureMessage(exitCode, 0, request.directory, errorTail));
	}

	QJsonObject result;
	result.insert(QStringLiteral("directory"), request.directory);
	result.insert(QStringLiteral("format"), request.formatName);
	result.insert(QStringLiteral("sample_rate"), static_cast<int>(request.settings.sampleRate));
	result.insert(QStringLiteral("bit_depth"),
		QString::fromLatin1(controlExportPresetBitDepthName(request.settings.bitDepth)));
	result.insert(QStringLiteral("stereo_mode"),
		QString::fromLatin1(controlExportPresetStereoModeName(request.settings.stereoMode)));
	result.insert(QStringLiteral("applied_preset"), ControlExportPresetSettings::activeName());
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
		"1, the whole-project render's own convention). The stems are written with the export "
		"settings in force - the sample rate, bit depth and stereo mode of the preset applied "
		"through export.preset_apply, or the render path's own defaults (44100 Hz, 16-bit, joint "
		"stereo) when none is applied; the reply names them. Returns the new file names. NOTE the "
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
		{QStringLiteral("bit_depth"), stringProperty()},
		{QStringLiteral("stereo_mode"), stringProperty()},
		{QStringLiteral("applied_preset"), stringProperty()},
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
