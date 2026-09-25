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
 * THE REPLY IS AN ACK, AND THE RUN FINISHES ON THE EVENT LOOP (the agent-API
 * rule: no request may go >30 s without a reply a client can act on). This
 * command used to START the render child and then WAIT for it on the dispatch
 * thread - `waitForStarted(30000) && waitForFinished(600000)` - so the control
 * surface answered nothing, `control.ping` included, until the child was done.
 * That is the defect docs/RENDER-CHILD-WAIT.md:120-126 records for the whole
 * render-running family (render.render, render.stems, the three bounce/freeze
 * commands and stem.model_download), and it was measured twice in the 2026-09-24
 * certification sweep as DEFECT-D3: a mastering.run on a grown session whose
 * child rendered for 19.27 s (the child's own PERFLOG line) left the request
 * unanswered past the client's 30 s bound.
 *
 * So this verb now does what the stems' own long work does
 * (`stem.job_start` -> `stem.job_status` -> `stem.job_result`;
 * src/core/StemJobManager.cpp runs it on its own worker thread so "the control
 * surface keeps answering while the job runs"): the handler validates, captures
 * the inverse, serialises the session, STARTS the child and RETURNS - in
 * milliseconds, whatever the render costs - reporting `state: "running"`. The
 * child's completion is delivered by the ordinary event loop, and the run's own
 * document is then readable through `mastering.get_state`, whose `state` leaves
 * `running` for `completed` (or `failed`, with the child's own reason in
 * `error`). A second run while one is in flight is REFUSED with `busy` rather
 * than queued behind it.
 *
 * This does NOT build the deferred-reply design of docs/RENDER-CHILD-WAIT.md
 * :128-141 (a reply sink in ControlServer, which that document scopes to its own
 * lane): it needs no dispatch-core change and no new id, because
 * `mastering.get_state` is already the poll verb for this family.
 *
 * THE RUN'S SCRATCH IS A QTemporaryDir. The serialised session, the child's
 * report and its stderr used to be written to /tmp/zene-master-* where nothing
 * removed them (the same certification sweep saw them left behind when a run was
 * cut short). They now live in a temporary directory the run owns and deletes -
 * including on an instance quit that interrupts a run, which is when the old
 * shape leaked them.
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
#include <QTemporaryDir>
#include <QTimer>

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


/*! How long the child is given to EXEC before the request is refused. This is
 *  the fork/exec handshake, not the child's engine construction: `started` is
 *  emitted once the child is running, so a successful start spends milliseconds
 *  here however long the render that follows takes. A child that cannot exec at
 *  all is a REFUSAL (nothing has been written yet) rather than a run that never
 *  finishes.
 */
constexpr int MasteringChildStartMs = 10000;

/*! How long one run's child may live before it is killed and the run is
 *  reported failed. This is the bound the old blocking handler expressed as
 *  `waitForFinished(600000)`; it is now enforced off the dispatch thread, by the
 *  timer startMasteringChild() arms, so it costs the control surface nothing.
 */
constexpr int MasteringChildLifeMs = 600000;

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

/*! Starts the shipped CLI mastering action as a child process and RETURNS - it
 *  never waits for the render. The QProcess, its standard-error file and the
 *  scratch directory are owned by \a run, which the completion deletes.
 *
 *  False with \a error set when the child could not be started at all; the child
 *  is then killed and nothing has been written, so the caller refuses the
 *  request without an inverse having to be recorded.
 */
bool startMasteringChild(MasteringPendingRun* run, ControlResult* error)
{
	QStringList arguments{QStringLiteral("master"), run->sessionPath,
		QStringLiteral("-o"), run->outDir,
		QStringLiteral("-f"), QStringLiteral("wav"),
		QStringLiteral("-s"), QString::number(MasteringRenderSampleRate),
		QStringLiteral("--report"), run->reportPath};
	arguments += configArgs();

	run->process = new QProcess();
	run->process->setStandardOutputFile(QProcess::nullDevice());
	run->process->setStandardErrorFile(run->errorPath);
	run->process->start(QCoreApplication::applicationFilePath(), arguments);
	if (!run->process->waitForStarted(MasteringChildStartMs))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not start the mastering renderer (%1): the session is unchanged "
				"and nothing was written").arg(run->process->errorString()));
		run->process->kill();
		delete run;
		return false;
	}
	return true;
}

//! What the run created: everything the directory holds now and did not hold
ControlResult handleMasteringRun(const QJsonObject& args)
{
	QString outDir;
	const QString invalid = validateRunArgs(args, &outDir);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}

	// ONE run at a time. A second render cannot see the first, so queueing it
	// would be queueing behind work the caller knows nothing about; the refusal
	// says which verb reports the run that is already going.
	if (masteringPendingRun() != nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("a mastering run is already in flight in this instance: poll "
				"mastering.get_state until its `state` leaves \"running\", then ask again"));
	}

	// The inverse is captured BEFORE the first write; an oversized capture is a
	// typed refusal rather than a run recorded without one (SPEC A16).
	QMap<QString, QByteArray> before;
	ControlResult error;
	if (!captureMasteringWavDirectory(outDir, &before, &error)) { return error; }

	auto* run = new MasteringPendingRun;
	run->outDir = outDir;
	run->before = before;
	run->scratch = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/zene-master-XXXXXX"));
	if (!run->scratch->isValid())
	{
		delete run;
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not create a scratch directory for this run's serialised "
				"session: the session is unchanged and nothing was written"));
	}
	run->sessionPath = run->scratch->filePath(QStringLiteral("session.mmp"));
	run->reportPath = run->scratch->filePath(QStringLiteral("report.json"));
	run->errorPath = run->reportPath + QStringLiteral(".stderr");

	if (!serialiseSession(run->sessionPath, &error))
	{
		delete run;
		return error;
	}
	if (!startMasteringChild(run, &error)) { return error; }

	// The run is in flight from here on: publishing it before the connections
	// below is what makes `busy` mean "a run is going", and it is what the
	// completion's own guard tests.
	setMasteringPendingRun(run);

	// The child is running. Everything from here is delivered by the ordinary
	// event loop, on this same application thread, so nothing is shared and the
	// dispatch thread is free the moment this returns.
	QProcess* process = run->process;
	QObject::connect(process, &QProcess::finished, process,
		[run](int exitCode, QProcess::ExitStatus) { completeMasteringRun(run, true, exitCode); });
	QObject::connect(process, &QProcess::errorOccurred, process,
		[run](QProcess::ProcessError kind)
		{
			// Not `finished`: a child that never ran at all reports here, and a
			// run nobody completes would sit at `running` for the rest of the
			// instance's life pretending to render.
			if (kind != QProcess::Crashed) { completeMasteringRun(run, false, -1); }
		});
	// The life bound the blocking handler used to hold as waitForFinished(600000).
	QTimer::singleShot(MasteringChildLifeMs, process, [run] {
		if (run->process->state() != QProcess::NotRunning) { run->process->kill(); }
	});
	// A quit that interrupts a run must not leave the child rendering into a
	// directory nobody will read, nor the scratch behind: kill it and take the
	// run (and its QTemporaryDir) down with the instance.
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, process, [run] {
		if (masteringPendingRun() != run) { return; }
		// The slot is cleared BEFORE the run is taken down, and that order is
		// the fix: ~MasteringPendingRun deletes its QProcess, and deleting a
		// QProcess whose child is still running kills and reaps it - which can
		// deliver the child's own completion to completeMasteringRun on this
		// same stack. Its one guard compares the pending slot with the run, so
		// clearing the slot first makes that nested completion the no-op it must
		// be. With the slot still set it passed the guard and deleted this run a
		// SECOND time: measured SIGSEGV in ~QTemporaryDir (~MasteringPendingRun's
		// `delete scratch` on already-freed memory) after a clean
		// `control.quit` while a run was in flight (BUG-MASTER-QUIT-CRASH).
		setMasteringPendingRun(nullptr);
		run->process->kill();
		delete run;
	});

	setMasteringRunState(QStringLiteral("running"));
	const QString stamp = QStringLiteral("%1-%2").arg(QCoreApplication::applicationPid())
		.arg(QDateTime::currentMSecsSinceEpoch());

	QJsonObject ack;
	ack.insert(QStringLiteral("state"), QStringLiteral("running"));
	ack.insert(QStringLiteral("out_dir"), outDir);
	ack.insert(QStringLiteral("poll"), QStringLiteral("mastering.get_state"));
	ack.insert(QStringLiteral("since_submit_seconds"), 0.0);
	// The A16 record travels with the REPLY (ControlRegistry records it there),
	// so the ACK carries it with the one fact it cannot have yet declared.
	ack.insert(QStringLiteral("__transaction"), masteringRunInverse(outDir, QStringList(), before, false));
	ack.insert(QStringLiteral("note"),
		QStringLiteral("ACCEPTED, not finished: the render runs in a child process and this reply "
			"is the acknowledgement, so the control surface stays answerable for as long as the "
			"render takes (a blocking reply was DEFECT-D3, measured 2026-09-24). Poll "
			"`mastering.get_state`: `state` is \"running\" now and becomes \"completed\" with the "
			"run's own document in `last_run` (or \"failed\", with the child's reason in `error`). "
			"Nothing has been written yet and no inverse has been recorded, so `control.undo` "
			"must not be asked to take this run back until `state` leaves \"running\". The session "
			"is not modified. Run id %1.").arg(stamp));
	return ControlResult::success(ack);
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
		"ASYNCHRONOUS, like the stems' own long work: this verb STARTS the render and answers with "
		"`state: \"running\"` in milliseconds, whatever the render costs - it never waits for it, so "
		"the control surface (control.ping included) stays answerable while a mastering run is in "
		"flight. The run's own document - the candidate measurements, the files and the counts - "
		"appears in `mastering.get_state`'s `last_run` once its `state` leaves \"running\", so the "
		"caller polls that verb; a run that fails reports `state: \"failed\"` and the child's own "
		"reason in `error` there. A second run while one is in flight is refused with `busy` rather "
		"than queued. "
		"REVERSIBLE through a recorded action checkpoint: the files this run created are removed "
		"and the revisions the directory already held are written back, byte for byte (bounded at "
		"64 MiB of pre-existing wav files - beyond that the run is refused rather than performed "
		"without an inverse). The inverse is recorded when the run COMPLETES, so `control.undo` must "
		"not be asked to take a run back until `mastering.get_state` reports it completed. "
		"Names no best candidate: there is no validated preference scorer, so "
		"the choice is the user's.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("out_dir"), stringProperty()},
	}, {QStringLiteral("out_dir")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("state"), enumProperty({QStringLiteral("running")})},
		{QStringLiteral("out_dir"), stringProperty()},
		{QStringLiteral("poll"), stringProperty()},
		{QStringLiteral("since_submit_seconds"), numberProperty()},
		{QStringLiteral("note"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return handleMasteringRun(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
