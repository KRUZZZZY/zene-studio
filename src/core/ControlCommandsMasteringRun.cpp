/*
 * ControlCommandsMasteringRun.cpp - the WRITING half of the `mastering.*`
 *                                   command group (SPEC A11-A16): mastering.run.
 *
 * Feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md. What this verb registers
 * is the wave-1 ENGINE, whole: one project render feeding N candidates
 * (MasteringJob::run, the render-once/branch-many job) and every candidate
 * measured with the BS.1770-4 meter against its own named target. The reads are
 * in ControlCommandsMastering.cpp; this file is the one verb that writes.
 *
 * THE RENDER IS A CHILD PROCESS, and that is the tree's rule rather than this
 * lane's preference: an in-process render drives THIS instance's audio engine
 * (ProjectRenderer::startProcessing calls audioEngine()->startProcessing() /
 * stopProcessing(), which owns the device thread) and Song::startExport stops
 * playback and re-measures the song. render.render
 * (src/core/ControlCommandsProject.cpp) and bounce-in-place
 * (include/BounceInPlace.h) both run the shipped CLI in a child process for the
 * reason their comments give, so this verb runs `zene master` the same way, on a
 * serialised copy of the session - the render is then the audio truth of exactly
 * the state the caller built, and the running instance is not touched at all.
 *
 * THE REPORT COMES BACK THROUGH --report, not from a parse of the printed
 * table: the child writes the run's own JSON document (MasteringReport.cpp,
 * `zene master ... --report <path>`) and this verb reads it back, so the numbers
 * an agent sees over the socket are the run's own measurements, in the shape the
 * engine produced them.
 *
 * REVERSIBILITY (SPEC A16). The one thing a run writes is FILES, in a directory
 * the caller names, outside the project: no Song checkpoint carries them and no
 * live object restores them. The recorded inverse is therefore an ACTION
 * checkpoint (control::addUndoStep, the same mechanism the chain-preset store
 * and the groove pool use) that removes every file the run created and writes
 * back every revision it replaced - captured BEFORE the first write, bounded,
 * and a run whose capture would exceed the bound is REFUSED rather than performed
 * without an inverse. The removal half is the half that matters on a first run:
 * a file that never existed cannot be restored, only deleted.
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
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "ControlEdit.h"
#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"

#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

/*! The rate the render runs at. 44100 is what render.render and
 *  BounceInPlace::renderTrack render at from a live instance, and the candidate
 *  set is one render branched N ways, so every candidate is measured at the same
 *  rate whatever the session's own device rate is.
 */
constexpr int MasteringRenderSampleRate = 44100;

//! The child's stderr is kept, bounded: it carries the engine's own refusal
//! text (a candidate with no measurable signal, a project that will not load).
constexpr int ChildErrorKeep = 600;

/*! Everything that can refuse, checked BEFORE anything is written or launched
 *  (SPEC A16: a refusal leaves nothing behind). Empty when the request is
 *  usable, else the reason.
 */
QString validateRunArgs(const QJsonObject& args, QString* outDir)
{
	*outDir = args.value(QStringLiteral("out_dir")).toString();
	if (outDir->isEmpty())
	{
		return QStringLiteral("'out_dir' is required: a candidate set is several files, so "
			"mastering writes into a directory the caller names");
	}
	if (!outDir->startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("'out_dir' must be an absolute path");
	}
	if (Engine::getSong() == nullptr)
	{
		return QStringLiteral("this instance has no song to render");
	}
	if (Engine::getSong()->isEmpty())
	{
		return QStringLiteral("the session is empty: there is nothing to master (add a track "
			"carrying audio first)");
	}
	return QString();
}

//! Serialises the session, so the render is the audio truth of exactly the state
//! the caller built (render.render and bounce-in-place's own rule).
bool serialiseSession(const QString& projectPath, ControlResult* error)
{
	if (Engine::getSong()->saveProjectFile(projectPath)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("could not serialise the session for rendering"));
	return false;
}

/*! The instance's own --config on the child's command line, when this instance
 *  was started with one: the child then renders with the SAME settings (the
 *  configured audio device, the working directory) instead of creating a fresh
 *  default config of its own.
 */
QStringList configArgs()
{
	const QStringList own = QCoreApplication::arguments();
	for (int i = 0; i + 1 < own.size(); ++i)
	{
		if (own.at(i) == QLatin1String("--config") || own.at(i) == QLatin1String("-c"))
		{
			return {QStringLiteral("-c"), own.at(i + 1)};
		}
	}
	return QStringList();
}

/*! Runs the shipped CLI mastering action as a child process and reads its stderr
 *  back (bounded). Returns false when the child did not finish inside the bound;
 *  \a exitCode is -1 then, and the caller reports the run as failed.
 */
bool runCliMaster(const QString& projectPath, const QString& outDir, const QString& reportPath,
	QString* childError, int* exitCode)
{
	QStringList arguments{QStringLiteral("master"), projectPath,
		QStringLiteral("-o"), outDir,
		QStringLiteral("-f"), QStringLiteral("wav"),
		QStringLiteral("-s"), QString::number(MasteringRenderSampleRate),
		QStringLiteral("--report"), reportPath};
	arguments += configArgs();

	const QString errorPath = reportPath + QStringLiteral(".stderr");
	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(errorPath);
	renderer.start(QCoreApplication::applicationFilePath(), arguments);
	const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;

	QFile errorFile(errorPath);
	if (errorFile.open(QIODevice::ReadOnly))
	{
		*childError = QString::fromUtf8(errorFile.readAll().right(ChildErrorKeep)).trimmed();
	}
	QFile::remove(errorPath);
	return finished;
}

//! What the run created: everything the directory holds now and did not hold
//! before. Derived from the two listings rather than predicted from the engine's
//! naming, so the inverse covers exactly the files that are there.
QStringList filesCreatedBy(const QStringList& before, const QStringList& after)
{
	QStringList created;
	for (const QString& path : after)
	{
		if (!before.contains(path)) { created.append(path); }
	}
	return created;
}

//! The inverse descriptor that travels with the result. There is no command that
//! deletes a candidate set, so the recorded operation is named for what it does
//! and `applies` stays at its default "journal": the action checkpoint on the
//! engine's own undo stack is what control.undo unwinds.
QJsonObject runInverse(const QString& outDir, const QStringList& created,
	const QMap<QString, QByteArray>& before)
{
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("out_dir"), outDir);
	inverseArgs.insert(QStringLiteral("created"), QJsonArray::fromStringList(created));

	QJsonObject inverse;
	inverse.insert(QStringLiteral("op"),
		QStringLiteral("remove the files this run created and write the captured revisions back"));
	inverse.insert(QStringLiteral("args"), inverseArgs);
	inverse.insert(QStringLiteral("applies"), QStringLiteral("journal"));

	QJsonObject payload;
	payload.insert(QStringLiteral("before"), masteringCaptureJson(outDir, before, created));
	payload.insert(QStringLiteral("inverse"), inverse);
	payload.insert(QStringLiteral("reversible"), true);
	payload.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the run's outputs are FILES in a directory outside the "
			"project, so no Song checkpoint carries them and no live object restores them. The "
			"recorded step removes every file the run created (before.created) and writes the "
			"revisions the directory already held (before.held_before) back byte for byte, both "
			"captured before the first write and bounded at before.capture_limit_bytes. ONE-WAY: "
			"there is no redo half (a faithful redo would have to hold the run's own outputs), so "
			"control.redo has nothing to replay - re-issue mastering.run instead"));
	return payload;
}

ControlResult handleMasteringRun(const QJsonObject& args)
{
	QString outDir;
	const QString invalid = validateRunArgs(args, &outDir);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}

	// The inverse is captured BEFORE the first write; an oversized capture is a
	// typed refusal rather than a run recorded without one (SPEC A16).
	QMap<QString, QByteArray> before;
	ControlResult error;
	if (!captureMasteringWavDirectory(outDir, &before, &error)) { return error; }

	const QString stamp = QStringLiteral("%1-%2").arg(QCoreApplication::applicationPid())
		.arg(QDateTime::currentMSecsSinceEpoch());
	const QString tempProject = QDir::tempPath() + QStringLiteral("/zene-master-%1.mmp").arg(stamp);
	const QString reportPath = QDir::tempPath() + QStringLiteral("/zene-master-%1.json").arg(stamp);
	if (!serialiseSession(tempProject, &error)) { return error; }

	QString childError;
	int exitCode = -1;
	const bool finished = runCliMaster(tempProject, outDir, reportPath, &childError, &exitCode);
	QFile::remove(tempProject);
	QFile::remove(reportPath + QStringLiteral(".stderr"));

	QJsonObject report;
	ControlResult reportError;
	if (!finished || exitCode != 0 || !readMasteringReportFile(reportPath, &report, &reportError))
	{
		QFile::remove(reportPath);
		const QString why = childError.isEmpty() ? reportError.errorMessage : childError;
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mastering run failed (exit %1) and the session is unchanged: %2")
				.arg(exitCode)
				.arg(why.isEmpty() ? QStringLiteral("the child process reported no reason") : why));
	}
	QFile::remove(reportPath);

	const QStringList created = filesCreatedBy(before.keys(), masteringWavFiles(outDir));
	recordMasteringUndo(created, before);
	setMasteringLastRun(report);

	QJsonObject result = report;
	result.insert(QStringLiteral("out_dir"), outDir);
	result.insert(QStringLiteral("files"), masteringFileFacts(masteringWavFiles(outDir)));
	result.insert(QStringLiteral("created"), QJsonArray::fromStringList(created));
	result.insert(QStringLiteral("created_count"), created.size());
	result.insert(QStringLiteral("replaced_count"), before.size());
	result.insert(QStringLiteral("renderer"),
		QStringLiteral("zene master (the shipped CLI action, in a child process on a serialised "
			"copy of the session; the running instance's audio engine is not touched)"));
	result.insert(QStringLiteral("render_sample_rate"), MasteringRenderSampleRate);
	result.insert(QStringLiteral("note"),
		QStringLiteral("%1 candidate files were written into out_dir by ONE project render, and "
			"every one was measured against its own named target. No candidate is preferred and "
			"none is ranked or called best - see mastering.list_candidates' note. The session is "
			"NOT modified: the child renders a serialised copy. UNDO takes the files back (the "
			"created ones are removed, replaced revisions are restored); `mastering.get_state` "
			"reads the same report back, and its `files` field is hashed live, so an edit made "
			"after the run is visible").arg(result.value(QStringLiteral("candidate_count")).toInt()));
	result.insert(QStringLiteral("__transaction"), runInverse(outDir, created, before));
	return ControlResult::success(result);
}

} // namespace

void registerMasteringRunCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("mastering.run");
	cmd.group = QStringLiteral("mastering");
	cmd.verb = QStringLiteral("run");
	cmd.description = QStringLiteral("Auto-master the session, wave 1: render the mix ONCE "
		"(counted by the render machinery, returned as render_count), branch every candidate of "
		"MasteringJob::defaultCandidates() off that one render, write one wav per candidate into "
		"out_dir and measure each with the BS.1770-4 meter - integrated loudness, the loudest 3 s "
		"window, measured true peak and crest factor, plus the residual against that candidate's "
		"target and its loudness and true-peak verdicts. `out_dir` is required and absolute. The "
		"session is not modified (the render runs in a child process on a serialised copy). "
		"REVERSIBLE through a recorded action checkpoint: the files this run created are removed "
		"and the revisions the directory already held are written back, byte for byte (bounded at "
		"64 MiB of pre-existing wav files - beyond that the run is refused rather than performed "
		"without an inverse). Names no best candidate: there is no validated preference scorer, so "
		"the choice is the user's.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("out_dir"), stringProperty()},
	}, {QStringLiteral("out_dir")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("out_dir"), stringProperty()},
		{QStringLiteral("candidate_count"), integerProperty()},
		{QStringLiteral("candidates"), arrayProperty()},
		{QStringLiteral("render_count"), integerProperty()},
		{QStringLiteral("render_sample_rate"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("source_render_file"), stringProperty()},
		{QStringLiteral("source"), objectProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("files"), arrayProperty()},
		{QStringLiteral("created"), arrayProperty()},
		{QStringLiteral("created_count"), integerProperty()},
		{QStringLiteral("replaced_count"), integerProperty()},
		{QStringLiteral("renderer"), stringProperty()},
		{QStringLiteral("note"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return handleMasteringRun(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
