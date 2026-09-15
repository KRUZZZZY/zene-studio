/*
 * ControlCommandsStems.cpp - the `stem.*` command group: the offline
 *                            stem-separation engine, drivable (SPEC A11-A16).
 *
 * Row 26 of docs/FEATURE-LIST-0.3.0.md ("Stem separation", section 5 "Audio
 * engine and DSP") and board task #653. The engine landed without an id: the
 * only route to HTDemucs-over-ONNX-Runtime was `tools/stem_split_cli.py`
 * (outside the control socket) and the GUI's "Split to stems" clip action
 * (src/gui/StemSplitController.cpp, a widget-owned singleton a headless agent
 * instance does not have). This file is the registration and the schemas; the
 * engine it drives is the one already in the tree and already proven by the
 * five registered tests (OnnxRuntimeStemSeparatorTest, StemExportTest,
 * StemJobManagerTest, StemModelStoreTest, StemSplitPipelineTest):
 *
 *   include/StemSeparation/StemJobManager.h:61-142  the one-worker job manager
 *   include/StemSeparation/StemModelStore.h:51-92   the model store (never
 *                                                   bundled, HTTPS only,
 *                                                   pinned SHA-256)
 *   include/StemSeparation/StemTypes.h:35-73        the 4 stems, 44.1 kHz, the
 *                                                   343980-frame (7.8 s) segment
 *
 * WHERE THE JOBS RUN, and why this group does NOT carry the render-bound defect:
 * `stem.job_start` decodes the source, hands the mix to StemJobManager and
 * returns the id; the separation runs on the manager's own worker thread and is
 * polled with `stem.job_status` (atomics behind the manager's mutex,
 * src/core/StemJobManager.cpp:167-204). The application thread - the thread
 * every control handler runs on - is never held for the inference, so
 * `control.ping` keeps answering while a job runs. The one exception is declared
 * rather than hidden, and it is the model download (see `stem.model_download`).
 *
 * REALTIME IS IMPOSSIBLE. HTDemucs is a hybrid transformer that needs the whole
 * 7.8 s segment as context (findings-ai-dsp.md 1.2; the 343980-frame constant is
 * this fork's own, include/StemSeparation/StemTypes.h:66-69), so it can never
 * run on the audio thread and there is no live mode to expose. `stem.get_state`
 * reports `realtime: false` and the lookahead; no verb here offers a streaming
 * or monitoring variant.
 *
 * THE FEATURE IS OFF IN THE DEFAULT RELEASE CONFIGURATION. WANT_STEM_SPLIT
 * defaults to OFF (CMakeLists.txt:120, "Include offline stem separation
 * (HTDemucs via ONNX Runtime, opt-in)"), so this group - and the job manager,
 * the backends and the model store behind it - is not compiled into a default
 * build at all, and the registry carries no `stem.*` id whose handler could not
 * exist (the rule the telemetry.*, session.* and wasm.* groups follow;
 * src/core/ControlRegistryRegistrations.cpp). The A16 rows are guarded by the
 * same macro, so the table and the registry stay consistent in both directions
 * (tests/src/core/ReversibilityContractTest.cpp asserts exactly that). A build
 * WITH the feature still needs the model present before a job can start: models
 * are never bundled.
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlStemSupport.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! One absolute-path check, shared by the source and the output directory: the
//! same rule `render.stems` applies to its `out` (`render.stems` refuses a
//! relative path rather than guessing a destination next to the project).
QString requireAbsolutePath(const QString& value, const QString& name)
{
	if (value.isEmpty())
	{
		return QStringLiteral("'%1' is required").arg(name);
	}
	if (!value.startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("'%1' must be an absolute path, and '%2' is not").arg(name, value);
	}
	return QString();
}

ControlResult stemGetState(const QJsonObject& args)
{
	Q_UNUSED(args);
	return ControlResult::success(stemState());
}

/*! `stem.job_start`: the mix comes from a FILE.
 *
 *  Over the socket there is no way to hand the engine a clip's audio, and the
 *  GUI's own route is a SampleClip the agent does not have; what an agent can
 *  name is a path. So the source is an absolute path to a stereo file at the
 *  model's own 44.1 kHz - `render.render`'s output is exactly that, which makes
 *  "bounce the session, then split the bounce" the composable flow. The mix is
 *  decoded here, on the application thread, and the separation runs on the
 *  manager's worker.
 */
ControlResult stemJobStart(const QJsonObject& args)
{
	const QString source = args.value(QStringLiteral("source")).toString();
	const QString invalid = requireAbsolutePath(source, QStringLiteral("source"));
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}
	const int segmentFrames = args.contains(QStringLiteral("segment_frames"))
		? args.value(QStringLiteral("segment_frames")).toInt()
		: 0;

	int jobId = -1;
	QJsonObject job;
	QString error;
	if (!stemStartJob(source, segmentFrames, &jobId, &job, &error))
	{
		// Every refusal here is the engine's own: a missing file, a file the
		// decoders cannot read, an input rate that is not the model's (no
		// resampler - SPEC-stem-split.md OQ-1), or the backend not being able
		// to run at all (no model file / no interpreter with onnxruntime).
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success(job);
}

//! `stem.job_status`: one job by id, or every job this instance has run.
ControlResult stemJobStatus(const QJsonObject& args)
{
	const bool anyJob = args.contains(QStringLiteral("job_id"));
	const int jobId = anyJob ? args.value(QStringLiteral("job_id")).toInt() : 0;
	bool found = false;
	const QJsonObject jobs = stemJobs(jobId, anyJob, &found);
	if (!found)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no stem job with id %1 was issued by this instance").arg(jobId));
	}
	return ControlResult::success(jobs);
}

/*! `stem.job_result`: the four stems as files in a directory the caller names.
 *
 *  Writing `<stem>.wav` into a directory is what `render.stems` does per track,
 *  and it is what makes the result observable through the socket at all - a
 *  StemSet is four audio buffers and there is no audio on the wire. The command
 *  refuses anything but a COMPLETED job rather than writing three stems and a
 *  hole.
 */
ControlResult stemJobResult(const QJsonObject& args)
{
	const int jobId = args.value(QStringLiteral("job_id")).toInt();
	const QString directory = args.value(QStringLiteral("out")).toString();
	const QString invalid = requireAbsolutePath(directory, QStringLiteral("out"));
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}
	const QString format = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	if (format != QLatin1String("wav"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("unsupported format '%1': this verb writes float32 RIFF/WAVE "
				"(`wav`) and nothing else").arg(format));
	}
	if (!stemHasJob(jobId))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no stem job with id %1 was issued by this instance").arg(jobId));
	}
	QJsonObject result;
	QString error;
	if (!stemWriteResult(jobId, directory, &result, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success(result);
}

ControlResult stemJobCancel(const QJsonObject& args)
{
	const int jobId = args.value(QStringLiteral("job_id")).toInt();
	if (!stemHasJob(jobId))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no stem job with id %1 was issued by this instance").arg(jobId));
	}
	QJsonObject job;
	QString error;
	if (!stemCancelJob(jobId, &job, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success(job);
}

ControlResult stemModelGetState(const QJsonObject& args)
{
	return ControlResult::success(stemModelState(args.value(QStringLiteral("hash")).toBool()));
}

/*! `stem.model_download`: the store's one policy-preserving entry point.
 *
 *  With no arguments it refuses - the default spec is unpinned in v1, on
 *  purpose, because the URL and the checksum must come from the model card
 *  rather than from a guess (src/core/StemModelStore.cpp:78-92). With a pinned
 *  spec it performs the transfer, and that transfer BLOCKS the control surface
 *  for its duration: a declared bound, the same category as the child-process
 *  renders, stated in the command's own description, in its A16 row and in
 *  docs/KNOWN-LIMITATIONS.md. No registered proof exercises it (CI has no
 *  pinned artefact to fetch); the refusal path is what the proof covers.
 *
 *  Named ...Command because the engine function it wraps has the same name and
 *  an unqualified call would find this one first.
 */
ControlResult stemModelDownloadCommand(const QJsonObject& args)
{
	QJsonObject result;
	QString error;
	const bool ok = control::stemModelDownload(
		args.value(QStringLiteral("url")).toString(),
		args.value(QStringLiteral("sha256")).toString(),
		static_cast<qint64>(args.value(QStringLiteral("size_bytes")).toDouble()),
		args.value(QStringLiteral("name")).toString(),
		args.value(QStringLiteral("dest_dir")).toString(),
		&result,
		&error);
	if (!ok)
	{
		// A URL that is not HTTPS, a spec that is not pinned, and the unpinned
		// default spec are all refusals of POLICY, not malformed calls; only a
		// dest_dir that is not absolute is an argument error.
		const QString destDir = args.value(QStringLiteral("dest_dir")).toString();
		const QString kind = destDir.isEmpty() ? QString()
			: requireAbsolutePath(destDir, QStringLiteral("dest_dir"));
		if (!kind.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, kind);
		}
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success(result);
}

void registerStemGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.get_state");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("The offline stem-separation engine's own facts: the "
		"inference backend this build drives (`onnxruntime-cpp` when the ONNX Runtime SDK was "
		"found at configure time, otherwise `external-process (python onnxruntime)`), whether it "
		"can run right now (`available`, with the reason in `error` when it cannot - the model "
		"file is usually what is missing, and models are never bundled), the model path the store "
		"resolves, the model contract's constants (44100 Hz, the 343980-frame / 7.8 s segment) and "
		"`realtime: false` with the reason: HTDemucs needs the whole segment as context, so "
		"separation is an OFFLINE job and there is no live mode. NOTE: the whole `stem.*` group is "
		"compiled only when WANT_STEM_SPLIT is ON - it is OFF in the default release configuration, "
		"where these ids do not exist at all (docs/KNOWN-LIMITATIONS.md).");
	cmd.resultSchema = objectSchema({
		{QStringLiteral("backend"), stringProperty()},
		{QStringLiteral("available"), booleanProperty()},
		{QStringLiteral("error"), stringProperty()},
		{QStringLiteral("model_path"), stringProperty()},
		{QStringLiteral("model_dir"), stringProperty()},
		{QStringLiteral("model_present"), booleanProperty()},
		{QStringLiteral("model_bytes"), numberProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("segment_frames"), integerProperty()},
		{QStringLiteral("lookahead_seconds"), numberProperty()},
		{QStringLiteral("realtime"), booleanProperty()},
		{QStringLiteral("realtime_reason"), stringProperty()},
		{QStringLiteral("stem_order"), arrayProperty()},
		{QStringLiteral("jobs"), objectSchema()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemGetState(args); };
	registry.registerCommand(cmd);
}

void registerStemJobStart(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.job_start");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("job_start");
	cmd.description = QStringLiteral("Queue one offline 4-stem separation of an absolute audio "
		"FILE (drums, bass, other, vocals) and return its job id immediately - the engine runs on "
		"its own worker thread, so the control surface keeps answering while the job runs. The "
		"source must be stereo at the model's own 44100 Hz: `render.render`'s output is exactly "
		"that, so \"bounce the session, then split the bounce\" is the composable flow. Refused, "
		"typed, when the file is missing or undecodable, when its rate is not 44100 Hz (resampling "
		"is not implemented - SPEC-stem-split.md OQ-1), or when the engine cannot run at all "
		"(model absent / no python onnxruntime). NOT realtime, by nature: HTDemucs needs a 7.8 s "
		"lookahead. Follow with `stem.job_status`, then `stem.job_result`.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("segment_frames"), integerProperty(1, 44100 * 600)},
	}, {QStringLiteral("source")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty()},
		{QStringLiteral("state"), enumProperty({QStringLiteral("queued"),
			QStringLiteral("running"), QStringLiteral("cancel_requested"),
			QStringLiteral("completed"), QStringLiteral("cancelled"), QStringLiteral("failed")})},
		{QStringLiteral("progress"), numberProperty()},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("seconds"), numberProperty()},
		{QStringLiteral("segment_frames"), integerProperty()},
		{QStringLiteral("since_submit_seconds"), numberProperty()},
		{QStringLiteral("backend"), stringProperty()},
		{QStringLiteral("error"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemJobStart(args); };
	registry.registerCommand(cmd);
}

void registerStemJobStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.job_status");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("job_status");
	cmd.description = QStringLiteral("One separator job by `job_id`, or every job this instance "
		"has run when `job_id` is omitted: each job's `state` (queued, running, cancel_requested, "
		"completed, cancelled, failed), `progress` (0..1), its source, frame count and "
		"`since_submit_seconds` (the surface's own wall clock, not a claim about inference time), "
		"and `error` when it failed. Reads the manager's atomics, so it answers even while a job is "
		"running - this is the poll that proves the control surface is not held by the work.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty(1, 1 << 30)},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("running"), integerProperty()},
		{QStringLiteral("jobs"), arrayProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemJobStatus(args); };
	registry.registerCommand(cmd);
}

void registerStemJobResult(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.job_result");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("job_result");
	cmd.description = QStringLiteral("Write the four stems of a COMPLETED job into an absolute "
		"directory as `<stem>.wav` (drums, bass, other, vocals - the model contract's fixed "
		"order), float32 RIFF/WAVE, and report each file's name, frames, seconds, bytes and "
		"sha256. Refuses a job that is not completed, naming its state (a failed job's message is "
		"in `stem.job_status`), and refuses an id this instance never issued. A directory that does "
		"not exist is created; a file of the same name is overwritten and its reported hash is the "
		"one on disk after the write. This writes OUTPUT ARTEFACTS and no project state - building "
		"tracks from the files is a separate, deliberate step.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty(1, 1 << 30)},
		{QStringLiteral("out"), stringProperty()},
		{QStringLiteral("format"), enumProperty({QStringLiteral("wav")})},
	}, {QStringLiteral("job_id"), QStringLiteral("out")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty()},
		{QStringLiteral("directory"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("sample_format"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("stems"), arrayProperty()},
		{QStringLiteral("source"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemJobResult(args); };
	registry.registerCommand(cmd);
}

void registerStemJobCancel(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.job_cancel");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("job_cancel");
	cmd.description = QStringLiteral("Ask the worker to stop an outstanding separation job and "
		"report its state after the request. Cancellation is cooperative and observed between "
		"inference segments, so a running job answers `cancel_requested` first and settles as "
		"`cancelled` (poll `stem.job_status`); a job still queued is cancelled immediately. A job "
		"that already finished is refused, naming its state, rather than reported as cancelled.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty(1, 1 << 30)},
	}, {QStringLiteral("job_id")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("job_id"), integerProperty()},
		{QStringLiteral("state"), enumProperty({QStringLiteral("queued"),
			QStringLiteral("running"), QStringLiteral("cancel_requested"),
			QStringLiteral("completed"), QStringLiteral("cancelled"), QStringLiteral("failed")})},
		{QStringLiteral("progress"), numberProperty()},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("seconds"), numberProperty()},
		{QStringLiteral("segment_frames"), integerProperty()},
		{QStringLiteral("since_submit_seconds"), numberProperty()},
		{QStringLiteral("backend"), stringProperty()},
		{QStringLiteral("error"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemJobCancel(args); };
	registry.registerCommand(cmd);
}

void registerStemModelGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.model_get_state");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("model_get_state");
	cmd.description = QStringLiteral("The model store's own facts: the directory and path it "
		"resolves (honouring LMMS_STEM_MODEL and LMMS_STEM_MODEL_DIR), whether the file is present "
		"and its size, the spec it would download (`name`, `url`, `sha256`, `size_bytes`, "
		"`license`, `license_url`, `model_card_url`, `pinned`) and whether that spec could be "
		"downloaded at all (`download_allowed` + `download_reason`). The default spec is "
		"deliberately UNPINNED in v1, so a default build reports `download_allowed: false` and "
		"names the model card: models are never bundled with the product. With `hash: true` the "
		"file's SHA-256 is computed and reported (`matches_spec` is null when there is nothing "
		"pinned to compare against) - it is off by default because hashing a 166 MB model must be "
		"a decision, not a side effect of a read.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("hash"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("present"), booleanProperty()},
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("spec"), objectSchema()},
		{QStringLiteral("download_allowed"), booleanProperty()},
		{QStringLiteral("download_reason"), stringProperty()},
		{QStringLiteral("env"), objectSchema()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("hash_error"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemModelGetState(args); };
	registry.registerCommand(cmd);
}

void registerStemModelDownload(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.model_download");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("model_download");
	cmd.description = QStringLiteral("Fetch a model file into the store (default directory, or "
		"`dest_dir`), HTTPS only, verifying the pinned SHA-256 and size BEFORE the file is moved "
		"into place - a partial or mismatched download never replaces a good file. Pinning is not "
		"optional: with no arguments this REFUSES, because the default spec is deliberately "
		"unpinned in v1 (take the URL and checksum from the model card, named in the refusal, and "
		"pass `url`, `sha256` and `size_bytes`). DECLARED BOUND: a performing call is a real "
		"network transfer on the control surface's own thread, so the surface does not answer - "
		"`control.ping` included - until it finishes or fails (the same defect the child-process "
		"renders carry, docs/RENDER-CHILD-WAIT.md); the transfer is not exercised by any "
		"registered proof, because CI has no pinned artefact to fetch.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("url"), stringProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("size_bytes"), integerProperty(1, 1 << 30)},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("dest_dir"), stringProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("verified"), booleanProperty()},
		{QStringLiteral("model_card_url"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemModelDownloadCommand(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerStemCommands(ControlRegistry& registry)
{
	registerStemGetState(registry);
	registerStemJobStart(registry);
	registerStemJobStatus(registry);
	registerStemJobResult(registry);
	registerStemJobCancel(registry);
	registerStemModelGetState(registry);
	registerStemModelDownload(registry);
}

} // namespace lmms
