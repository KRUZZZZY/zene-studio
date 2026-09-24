/*
 * ControlMasteringSupport.h - the shared helpers of the `mastering.*` command
 *                             group (SPEC A11-A16).
 *
 * Feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md ("Mastering chain /
 * auto-mastering" and "Auto-mastering wave 1 (#610) - candidate generation +
 * objective scoring"). The ENGINE half of wave 1 is in the tree and is proven
 * (include/MasteringJob.h, src/core/MasteringJob.cpp: the render-once /
 * branch-many job, its candidate set and its objective scoring against named
 * targets; include/MasteringChain.h, src/core/MasteringChain.cpp: the chain and
 * the BS.1770-4 measurement it reports through; tests/src/core/MasteringTest.cpp:
 * the whole path on a real .mmp). What did not exist was any way for a client to
 * drive it: no `mastering.` id was registered at either base.
 *
 * WHAT THIS FILE HOLDS, and why it is not inside a command handler:
 *
 *  * the run's OWN report, read back from the child process (the one
 *    implementation of that read, so `mastering.run` and `mastering.get_state`
 *    cannot disagree about it);
 *  * the instance's memory of its last run, which is what get_state reports;
 *  * the OUTPUT-DIRECTORY capture that makes `mastering.run` reversible: the
 *    .wav entries the directory already holds, bounded, so the recorded undo
 *    step can remove what the run created and write back what it replaced.
 *
 * The child-process rule is the tree's, not this lane's: an in-process render
 * drives THIS instance's audio engine (ProjectRenderer::startProcessing calls
 * audioEngine()->startProcessing()/stopProcessing(), which owns the device
 * thread) and Song::startExport stops playback and re-measures the song, so
 * render.render (src/core/ControlCommandsProject.cpp) and bounce-in-place
 * (include/BounceInPlace.h) both run the shipped CLI in a child process for the
 * reason their comments give. mastering.run runs `zene master` the same way, and
 * reads its numbers back through --report rather than parsing the printed table.
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

#ifndef LMMS_CONTROL_MASTERING_SUPPORT_H
#define LMMS_CONTROL_MASTERING_SUPPORT_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QProcess;
class QTemporaryDir;

namespace lmms
{

//! The reply to one command, defined as a `struct` in ControlRegistry.h:70.
//! The class-key here MUST match that definition: msvc-x64 builds with /WX, so
//! C4099 ("type name first seen using 'class' now seen using 'struct'") is
//! promoted to C2220 and fails the job. Run 34870198514 failed three TUs on
//! exactly this disagreement, both ways round:
//!   * ControlMasteringSupport.cpp.obj and ControlCommandsMastering.cpp.obj saw
//!     THIS declaration first (header :26 / :69, registry :36 / :70) and were
//!     reported at ControlRegistry.h:70;
//!   * ControlCommandsMasteringRun.cpp.obj saw ControlEdit.h:44 - also a
//!     `struct` - first and was reported at THIS line.
//! `struct` wherever the name is declared is the only spelling that is legal in
//! either order ([dcl.type.elab]: the class-key may not disagree with it).
struct ControlResult;

namespace control
{

//! The rate a mastering render runs at. One number, named once, and reported in
//! the run's own document so a caller never has to assume it - render.render and
//! BounceInPlace::renderTrack render at the same rate from a live instance.
constexpr int MasteringRenderSampleRate = 44100;

/*! Everything ONE in-flight `mastering.run` owns. mastering.run starts the
 *  render child and answers with an ACK, so this outlives the request: the
 *  QProcess that started the child, the scratch directory the run writes into
 *  (its serialised session, the child's report and its stderr) and the captured
 *  inverse.
 *
 *  The destructor OWNS both pointers: deleting a QProcess that is still running
 *  kills its child, and QTemporaryDir's destructor removes the directory. That
 *  is what makes a run cut short clean up after itself - the pre-fix shape wrote
 *  into the shared temp directory and left the files there.
 */
struct LMMS_EXPORT MasteringPendingRun
{
	MasteringPendingRun();
	~MasteringPendingRun();

	MasteringPendingRun(const MasteringPendingRun&) = delete;
	MasteringPendingRun& operator=(const MasteringPendingRun&) = delete;

	QProcess* process = nullptr;
	QTemporaryDir* scratch = nullptr;
	QString sessionPath;
	QString reportPath;
	QString errorPath;
	QString outDir;
	QMap<QString, QByteArray> before;
};

/*! The run in flight, or null. At most ONE, so a non-null answer doubles as the
 *  busy fact `mastering.run` refuses a second concurrent run on.
 */
LMMS_EXPORT MasteringPendingRun* masteringPendingRun();
LMMS_EXPORT void setMasteringPendingRun(MasteringPendingRun* run);

/*! Finish one run, on the application thread the child's QProcess lives on: read
 *  the report the child wrote, record the inverse of the files it created and
 *  publish the result for `mastering.get_state`. A failure is published too - the
 *  ACK has already left the socket, so this is the only place a client can learn
 *  that the run produced no candidate set.
 *
 *  A no-op unless \a run is still the pending run, which is what makes a signal
 *  already queued for a completed or aborted run harmless. Takes ownership of
 *  \a run and deletes it, which removes the run's scratch directory.
 */
LMMS_EXPORT void completeMasteringRun(MasteringPendingRun* run, bool finished, int exitCode);

/*! The inverse descriptor that travels with a run's reply. \a createdKnown is
 *  FALSE for the ACK, which is written before the run has created anything: the
 *  record then declares `created_not_yet_known` instead of naming files the run
 *  has not written.
 */
LMMS_EXPORT QJsonObject masteringRunInverse(const QString& outDir, const QStringList& created,
	const QMap<QString, QByteArray>& before, bool createdKnown = true);

/*! The bound on what ONE mastering.run can record for its inverse: the total
 *  size of the .wav entries the output directory already holds when the run
 *  starts. A run that would have to capture more than this is REFUSED, typed,
 *  rather than performed without an inverse - the rule
 *  ControlChainPresetSupport.h states for a chain too large to capture.
 *
 *  64 MiB is several times a full song's candidate set at 16-bit stereo
 *  (a 5-minute candidate is ~50 MB at 44.1 kHz, and a wave-1 set shares one
 *  render), and it is the ceiling on a closure held in memory until the step
 *  leaves the undo stack.
 */
constexpr qint64 MasteringCaptureLimitBytes = 64 * 1024 * 1024;

//! The .wav entries of \a directory (top level, case-insensitive suffix),
//! absolute and sorted. A directory that does not exist yet is an empty list:
//! the run creates it.
LMMS_EXPORT QStringList masteringWavFiles(const QString& directory);

/*! Reads every .wav entry of \a directory into \p capture, keyed by absolute
 *  path. False with a typed refusal when the entries together are larger than
 *  MasteringCaptureLimitBytes, naming the directory, the total and the bound;
 *  false with a typed error when one of them cannot be read.
 */
LMMS_EXPORT bool captureMasteringWavDirectory(const QString& directory,
	QMap<QString, QByteArray>* capture, ControlResult* error);

/*! Live facts about a set of files, for the read-back: `path`, `exists`,
 *  `bytes` and `sha256`. The hashes come from BounceInPlace::sha256OfFile, the
 *  same measurement render.render and freeze.* report.
 */
LMMS_EXPORT QJsonArray masteringFileFacts(const QStringList& paths);

/*! The before-state a transaction record carries: the directory, the sha256 of
 *  every file it already held and the names the run created. NO file content:
 *  a record is bounded by MaxTransactionBytes, and the content lives in the
 *  recorded undo step, which is where it is applied from.
 */
LMMS_EXPORT QJsonObject masteringCaptureJson(const QString& directory,
	const QMap<QString, QByteArray>& before, const QStringList& created);

/*! Records the inverse of one run as an ACTION checkpoint on the engine's own
 *  undo stack (control::addUndoStep, so `control.undo` and the GUI's Ctrl+Z
 *  unwind the same step): every file the run CREATED is removed, and every file
 *  the directory already held is written back byte for byte.
 *
 *  THERE IS NO REDO HALF, and that is a decision rather than an omission: a
 *  faithful redo would have to hold the run's own outputs, which are the
 *  products of a render and can be larger than everything captured here put
 *  together. Re-issuing mastering.run is the way back, and
 *  control.redo's own contract already documents a one-way action step ("a
 *  structural step whose inverse was a one-way action has nothing to redo, and
 *  says so"). Also: the removal half is what takes the FIRST run into an empty
 *  directory back - restoring captured revisions alone would leave every file
 *  that had no earlier revision exactly where the run put it.
 */
LMMS_EXPORT void recordMasteringUndo(const QStringList& created,
	const QMap<QString, QByteArray>& before);

//! This instance's memory of its own last mastering.run: the report the child
//! run wrote, verbatim, or an empty object before the first run. It is NOT
//! project state and NOT persisted - it answers "what did the last run in THIS
//! process measure", which is what a client needs to read the candidate set
//! back after another command has answered.
LMMS_EXPORT QJsonObject masteringLastRun();
LMMS_EXPORT void setMasteringLastRun(const QJsonObject& report);

/*! The state of the instance's mastering runs, as `mastering.get_state` reports
 *  it and a client polls it. mastering.run answers with an ACK and finishes on
 *  the event loop, so a client that needs the run's own document reads it back
 *  from mastering.get_state once `state` leaves `running`.
 *
 *  The four values, and nothing else:
 *
 *   * `idle`      - no run has been asked for in this process;
 *   * `running`   - a run is in flight;
 *   * `completed` - the last run finished and its result is in masteringLastRun();
 *   * `failed`    - the last run failed and \a error says why. A failed run
 *                   leaves the session untouched and records no inverse.
 *
 *  A FAILED RUN IS NOT DROPPED, because nothing else can report it: the ACK
 *  mastering.run returned already left the socket, so this is where a client
 *  learns that the child it started did not produce a candidate set.
 */
LMMS_EXPORT QString masteringRunState();
LMMS_EXPORT QString masteringRunError();
LMMS_EXPORT void setMasteringRunState(const QString& state, const QString& error = QString());

/*! Reads the JSON document writeMasteringReport() wrote in the child process
 *  (`zene master ... --report <path>`). False with a typed error when the file
 *  is missing, unreadable or not a JSON object.
 */
LMMS_EXPORT bool readMasteringReportFile(const QString& path, QJsonObject* report,
	ControlResult* error);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_MASTERING_SUPPORT_H
