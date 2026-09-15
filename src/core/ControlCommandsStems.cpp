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
	const QString invalid = stemRequireAbsolutePath(source, QStringLiteral("source"));
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
	const QString invalid = stemRequireAbsolutePath(directory, QStringLiteral("out"));
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


} // namespace

void registerStemCommands(ControlRegistry& registry)
{
	registerStemGetState(registry);
	registerStemJobStart(registry);
	registerStemJobStatus(registry);
	registerStemJobResult(registry);
	registerStemJobCancel(registry);
	// The model-store half lives in ControlCommandsStemModel.cpp (the group's
	// own read/edit split, for the file-length ratchet).
	registerStemModelCommands(registry);
}

} // namespace lmms
