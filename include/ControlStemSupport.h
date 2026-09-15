/*
 * ControlStemSupport.h - the socket-facing half of the offline stem-separation
 *                        engine (`stem.*`, SPEC A11-A16).
 *
 * Row 26 of docs/FEATURE-LIST-0.3.0.md ("Stem separation") and board task #653.
 * The ENGINE half was already in the tree and already proven
 * (include/StemSeparation/StemJobManager.h:61 - the one-worker job manager;
 * include/StemSeparation/StemModelStore.h:51 - the model store; the five
 * registered tests OnnxRuntimeStemSeparatorTest, StemExportTest,
 * StemJobManagerTest, StemModelStoreTest, StemSplitPipelineTest). What did not
 * exist was any way for a client to drive it: the only route to the engine was
 * `tools/stem_split_cli.py`, which is OUTSIDE the control socket, and the GUI
 * route (src/gui/StemSplitController.cpp, the "Split to stems" clip action) is
 * a widget-owned singleton a headless agent instance does not have.
 *
 * WHAT THIS FILE HOLDS, and why it is not inside a command handler:
 *
 *  * the instance's ONE StemJobManager, and the backend it drives. The manager
 *    owns a worker thread, so it has to outlive a single command and there has
 *    to be exactly one of it per process - the same reason
 *    gui::StemSplitController::instance() is a singleton (and, like that one,
 *    it is created on first use and never destroyed: joining a worker thread
 *    during static destruction is not safe);
 *  * the ledger of the jobs THIS surface started, which is what makes "no such
 *    job" answerable at all - StemJobManager::state() reads an unknown id as
 *    `Queued` by design (src/core/StemJobManager.cpp:171-173, so GUI polling of
 *    a just-submitted id is race-free), so the manager alone cannot tell a
 *    caller that an id was never issued;
 *  * the availability probe, and the WAV writer the result verb needs - the
 *    writer itself lives in ControlStemWav.cpp and the model-store verbs in
 *    ControlStemModel.cpp, because the file-length ratchet reads a file as a
 *    unit (the same reason the A16 table is split across files).
 *
 * THE THREADING CONTRACT, in one place. Control handlers run on the application
 * thread (ControlRegistry::invoke -> runOnUiThread, ControlRegistry.cpp:183).
 * Nothing here blocks it: `stem.job_start` decodes the source, submits and
 * returns the id; the separation itself runs on StemJobManager's own worker
 * thread; every read is an atomic behind the manager's mutex. That is the whole
 * point of the design - an offline job must NOT freeze the control surface the
 * way the child-process renders do (docs/RENDER-CHILD-WAIT.md).
 *
 * REALTIME IS IMPOSSIBLE, and this surface says so instead of implying
 * otherwise: HTDemucs needs the whole 7.8 s segment as context
 * (include/StemSeparation/StemTypes.h:66-69, HTDemucsSegmentFrames = 343980 at
 * 44100 Hz), so there is no chunk size that fits an audio block. `stem.get_state`
 * reports `realtime: false` with that lookahead, and no verb here pretends to a
 * live mode.
 *
 * THE FEATURE IS OFF IN THE RELEASE CONFIGURATION. WANT_STEM_SPLIT defaults to
 * OFF (CMakeLists.txt:120) and this whole group is compiled only when it is ON:
 * without LMMS_HAVE_STEM_SPLIT there is no job manager, no backend and no model
 * store, so the registry carries no `stem.*` id whose handler could not exist -
 * the rule the telemetry.*, session.* and wasm.* groups follow
 * (src/core/ControlRegistryRegistrations.cpp). The model store is stricter
 * still: models are never bundled, and the default spec is UNPINNED in v1
 * (src/core/StemModelStore.cpp:78-92), so a default build that has the feature
 * compiled in still refuses a download until a spec is pinned from the model
 * card. docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md carry
 * both sentences.
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

#ifndef LMMS_CONTROL_STEM_SUPPORT_H
#define LMMS_CONTROL_STEM_SUPPORT_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;

namespace control
{

/*! The engine's own facts, for `stem.get_state`: the backend this instance
 *  drives (`backend`), whether it can run at all right now (`available`, with
 *  the reason in `error` when it cannot), the model file the store resolves
 *  (`model_path`, `model_dir`, `model_present`, `model_bytes`), the model
 *  contract's constants (`sample_rate`, `segment_frames`, `lookahead_seconds`,
 *  `stem_order`) and `realtime: false` with the reason no live mode exists.
 *
 *  THE PROBE IS REMEMBERED WHEN IT SUCCEEDS and re-run while it fails: the
 *  discovery and isAvailable helpers read the environment and the filesystem once and
 *  a successful probe cannot go stale in a way that matters, while a caller
 *  that has just placed the model on disk must not be told "absent" by a cached
 *  answer. `stem.job_start` uses the same answer.
 */
LMMS_EXPORT QJsonObject stemState();

/*! Queues one separation job for \a source (an absolute path to a stereo audio
 *  file) and returns its id in \p jobId plus the job's own object in \p job.
 *
 *  Refusals are the engine's own, never invented here: an undecodable or
 *  missing file, an input rate that is not the model's (resampling is not
 *  implemented - SPEC-stem-split.md OQ-1), and "no backend" (no interpreter
 *  with onnxruntime, no in-process runtime, or no model file) each come back as
 *  false with the engine's message. \a segmentFrames <= 0 means the model's own
 *  HTDemucsSegmentFrames.
 */
LMMS_EXPORT bool stemStartJob(const QString& source,
	int segmentFrames,
	int* jobId,
	QJsonObject* job,
	QString* error);

//! Whether \a jobId is a id THIS surface issued. The manager alone cannot
//! answer it (see the file header).
LMMS_EXPORT bool stemHasJob(int jobId);

/*! One job by id when \a anyJob is true, else every job this surface started.
 *  *\p found is false - and the object empty - when \a anyJob names an id this
 *  surface never issued.
 *
 *  Each job object carries `job_id`, `state` ("queued", "running",
 *  "cancel_requested", "completed", "cancelled", "failed"), `progress` (0..1),
 *  `source`, `sample_rate`, `frames`, `seconds`, `segment_frames`,
 *  `since_submit_seconds` (the SURFACE's wall clock, not the engine's job
 *  time), `backend` and `error` (empty unless the job failed).
 */
LMMS_EXPORT QJsonObject stemJobs(int jobId, bool anyJob, bool* found);

/*! Asks the worker to stop \a jobId, and reports its state after the request.
 *  Cancellation is cooperative and observed between inference segments, so a
 *  running job comes back as `cancel_requested` and settles as `cancelled`
 *  (StemJobManager.h:48-60). A job that already reached a terminal state is
 *  refused, naming the state, rather than reported as cancelled.
 */
LMMS_EXPORT bool stemCancelJob(int jobId, QJsonObject* job, QString* error);

/*! Writes the four stems of a COMPLETED job into \a directory as
 *  `<stem>.wav` (drums, bass, other, vocals - the model contract's fixed order,
 *  StemTypes.h:35-46), float32 RIFF, and reports for each file its `name`,
 *  `stem`, `frames`, `seconds`, `bytes` and `sha256`.
 *
 *  Writing an output artefact is not a project edit: nothing here touches the
 *  session, so the A16 row is `not_mutating` for the same reason
 *  `render.stems`' is. The directory is created if it does not exist, and an
 *  earlier file of the same name IS overwritten and reported as rewritten (its
 *  hash is the one on disk after the write, so a caller can never be told about
 *  a file this call did not produce).
 */
LMMS_EXPORT bool stemWriteResult(int jobId,
	const QString& directory,
	QJsonObject* result,
	QString* error);

/*! The one WAV writer the result verb uses (src/core/ControlStemWav.cpp): \a
 *  buffer as float32 RIFF/WAVE - audio_format 3, 32-bit, stereo, the reference
 *  CLI's own header - with \a sampleRate in it. False with a typed message when
 *  the file cannot be opened or written.
 *
 *  In its own translation unit for the reason the A16 table is split: the
 *  file-length ratchet reads a file as a unit, and the writer is a
 *  self-contained job.
 */
LMMS_EXPORT bool stemWriteWavFile(const QString& path,
	const SampleBuffer& buffer,
	int sampleRate,
	QString* error);

//! Lowercase hex SHA-256 of \a path; empty when it cannot be read. The result
//! verb reports this per stem, so a caller can verify the file it got.
LMMS_EXPORT QString stemSha256OfFile(const QString& path);

/*! The one absolute-path check the group's argument validation uses, for the
 *  source file and for an output directory alike: empty when \a value is an
 *  absolute path, else the typed sentence naming \a name ("'out' must be an
 *  absolute path, and 'stems' is not"). It lives with the surface rather than in
 *  one of the two command files because both halves validate with it - the same
 *  rule `render.stems` applies to its `out`, which refuses a relative path
 *  rather than guessing a destination next to the project.
 */
LMMS_EXPORT QString stemRequireAbsolutePath(const QString& value, const QString& name);

/*! The model store's own facts, for `stem.model_get_state`: the resolved
 *  directory and path, whether the file is present and its size, the spec the
 *  store would download (`name`, `url`, `sha256`, `size_bytes`, `license`,
 *  `license_url`, `model_card_url`, `pinned`), whether that spec could be
 *  downloaded at all (`download_allowed` + `download_reason`), and the two
 *  environment overrides the store honours (`LMMS_STEM_MODEL`,
 *  `LMMS_STEM_MODEL_DIR`).
 *
 *  With \a withHash the file's SHA-256 is computed and reported (`sha256`,
 *  `matches_spec` - true/false against a pinned spec, and `spec_pinned: false`
 *  when there is nothing to compare against). It is OFF by default because
 *  hashing the 166 MB model must be a decision, not a side effect of a read.
 */
LMMS_EXPORT QJsonObject stemModelState(bool withHash);

/*! Downloads a PINNED model spec into \a destDir (empty = the store's own
 *  default model directory) and returns the resulting file's facts.
 *
 *  With no \a url/\a sha256/\a size_bytes the store's own spec is used, and in
 *  v1 that spec is deliberately unpinned - so the default call is REFUSED with
 *  the store's own sentence and the model card URL, which is the policy working
 *  rather than a bug (StemModelStore.h:65-69, docs/STEM-SPLIT.md "Model
 *  handling"). A non-HTTPS URL is refused before any transfer starts.
 *
 *  A performing call is a real network transfer on the application thread, and
 *  that is a DECLARED BOUND, not a hidden one: the control surface does not
 *  answer - `control.ping` included - until the transfer finishes or fails, the
 *  same defect the child-process renders carry (docs/RENDER-CHILD-WAIT.md). It
 *  is stated in the command's description, in its A16 row and in
 *  docs/KNOWN-LIMITATIONS.md, and no registered proof exercises the transfer
 *  itself (CI has no pinned artefact to fetch): what the proof exercises is the
 *  refusal path.
 */
LMMS_EXPORT bool stemModelDownload(const QString& url,
	const QString& sha256,
	qint64 sizeBytes,
	const QString& name,
	const QString& destDir,
	QJsonObject* result,
	QString* error);

} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_STEM_SUPPORT_H
