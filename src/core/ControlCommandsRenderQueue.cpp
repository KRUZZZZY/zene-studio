/*
 * ControlCommandsRenderQueue.cpp - the render queue, batch conversion, render reports and the background task list
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
 *
 */
/*! 0.5.0 L12:
 *  - render.queue_add / queue_list / queue_cancel / queue_clear / queue_report and render.convert (WF-040):
 *    renders of the session (a snapshot taken when queued) or of other project files, and conversions
 *    of audio files to another format, rate or sample size, run one after another without holding the
 *    control surface (RenderQueue.h), each ending with a report (bytes, hash, time, loudness);
 *  - app.tasks / app.task_cancel (WF-046): every background job in one list - the render queue, the
 *    library scan, stem separation and auto-mastering - and cancelling one from it.
 *  Output artefacts and session state, not project state: nothing is journalled. */

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QVector>

#include "BrowserIndex.h"
#include "ControlExportPresetSupport.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "Song.h"

namespace lmms
{

void registerRenderBounceCommand(ControlRegistry& registry);

using namespace control;

namespace
{

RenderQueue& queue() { return RenderQueue::instance(); }

QJsonObject jobJson(const RenderJob& job)
{
	QJsonArray targetIds;
	for (const int id : job.targetIds) { targetIds.append(id); }
	return {{QStringLiteral("job"), job.id}, {QStringLiteral("kind"), RenderQueue::kindName(job.kind)},
		{QStringLiteral("state"), RenderQueue::stateName(job.state)}, {QStringLiteral("source"), job.source}, {QStringLiteral("out"), job.out},
		{QStringLiteral("operation"), job.operation}, {QStringLiteral("target_ids"), targetIds},
		{QStringLiteral("phase"), job.phase}, {QStringLiteral("progress"), job.progress},
		{QStringLiteral("format"), job.format}, {QStringLiteral("error"), job.error}, {QStringLiteral("exit_code"), job.exitCode},
		{QStringLiteral("queued_ms"), static_cast<double>(job.queuedMs)}, {QStringLiteral("started_ms"), static_cast<double>(job.startedMs)},
		{QStringLiteral("finished_ms"), static_cast<double>(job.finishedMs)},
		{QStringLiteral("report"), job.report}};
}

//! The project a render job reads: 'project' (absolute, existing) or a snapshot of the session now.
bool renderSource(const QJsonObject& args, RenderJob* job, ControlResult* error)
{
	const QString project = args.value(QStringLiteral("project")).toString();
	if (!project.isEmpty())
	{
		job->source = project;
		const bool usable = QFileInfo(project).isAbsolute() && QFileInfo(project).isFile();
		if (!usable) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("'project' must be an existing absolute file")); }
		return usable;
	}
	if (Engine::getSong()->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the session is empty: there is nothing to render"));
		return false;
	}
	job->source = QDir::tempPath() + QStringLiteral("/zene-queue-%1-%2.mmp").arg(QCoreApplication::applicationPid())
		.arg(QDateTime::currentMSecsSinceEpoch());
	job->ownsSource = true;
	if (Engine::getSong()->saveProjectFile(job->source)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("could not snapshot the session for the queue"));
	return false;
}

ControlResult queueFull()
{
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the render queue holds %1 jobs; clear finished ones first")
		.arg(RenderQueue::MaxJobs));
}

ControlResult queued(const RenderJob& job, bool start)
{
	const int id = queue().add(job);
	if (id < 0)
	{
		if (job.ownsSource) { QFile::remove(job.source); }
		return queueFull();
	}
	if (start) { queue().start(); }
	return ControlResult::success(jobJson(*queue().job(id)));
}

ControlResult queueAdd(const QJsonObject& args)
{
	RenderJob job;
	job.out = args.value(QStringLiteral("out")).toString();
	job.format = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	if (!QFileInfo(job.out).isAbsolute()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'out' must be absolute")); }
	const bool hasStart = args.contains(QStringLiteral("start_ticks"));
	if (hasStart != args.contains(QStringLiteral("end_ticks")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a range needs both start_ticks and end_ticks"));
	}
	job.rangeBegin = hasStart ? args.value(QStringLiteral("start_ticks")).toInt() : -1;
	job.rangeEnd = hasStart ? args.value(QStringLiteral("end_ticks")).toInt() : -1;
	if (hasStart && job.rangeEnd <= job.rangeBegin)  // refused here, not by the render child after the queue took it
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("end_ticks must be after start_ticks"));
	}
	ControlResult error;
	if (!renderSource(args, &job, &error)) { return error; }
	return queued(job, args.value(QStringLiteral("start")).toBool(true));
}

//! Only what the writer makes: 16, 24, or 32 float in a WAV (review W3-agent-library #1).
bool writableDepth(int bits, const QString& format)
{
	return bits == 16 || bits == 24 || (bits == 32 && format == QLatin1String("wav"));
}

//! Each of @a files with the file it converts to in @a dir - all checked before any is queued: refused for a missing
//! source, an output that is its own source, or two sources that would write one output.
bool convertTargets(const QJsonArray& files, const QString& dir, const QString& format, QVector<QPair<QString, QString>>* out,
	ControlResult* error)
{
	QSet<QString> outputs;
	for (const QJsonValue& value : files)
	{
		const QFileInfo source(value.toString());
		if (!source.isAbsolute() || !source.isFile())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 is not an existing absolute file").arg(value.toString()));
			return false;
		}
		const QString target = QDir::cleanPath(QDir(dir).filePath(source.completeBaseName() + QLatin1Char('.') + format));
		const bool overwritesSource = QFileInfo::exists(target) && QFileInfo(target).canonicalFilePath() == source.canonicalFilePath();
		if (overwritesSource || outputs.contains(target))
		{
			*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 would overwrite %2").arg(source.filePath(),
				overwritesSource ? QStringLiteral("its own source") : QStringLiteral("another file's output (%1)").arg(target)));
			return false;
		}
		outputs.insert(target);
		out->append({source.filePath(), target});
	}
	return true;
}

ControlResult convertFiles(const QJsonObject& args)
{
	const QString dir = args.value(QStringLiteral("out_dir")).toString();
	if (!QFileInfo(dir).isAbsolute()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'out_dir' must be absolute")); }
	RenderJob job;
	job.kind = RenderJob::Kind::Convert;
	job.format = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	job.sampleRate = args.value(QStringLiteral("sample_rate")).toInt(0);
	job.bitDepth = args.value(QStringLiteral("bit_depth")).toInt(24);
	if (!writableDepth(job.bitDepth, job.format))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'bit_depth' is 16, 24, or 32 (wav only)"));
	}
	QVector<QPair<QString, QString>> targets;
	ControlResult error;
	if (!convertTargets(args.value(QStringLiteral("files")).toArray(), dir, job.format, &targets, &error)) { return error; }
	if (static_cast<int>(queue().jobs().size()) + targets.size() > RenderQueue::MaxJobs) { return queueFull(); }  // refused whole, never half queued
	QJsonArray jobs;
	for (const auto& [source, out] : targets)
	{
		job.source = source;
		job.out = out;
		const ControlResult one = queued(job, true);
		if (!one.ok) { return one; }
		jobs.append(one.result);
	}
	return ControlResult::success({{QStringLiteral("jobs"), jobs}});
}

ControlResult queueList(const QJsonObject&)
{
	QJsonArray jobs;
	int open = 0;
	for (const RenderJob& job : queue().jobs())
	{
		jobs.append(jobJson(job));
		open += job.state == RenderJob::State::Queued || job.state == RenderJob::State::Running ? 1 : 0;
	}
	return ControlResult::success({{QStringLiteral("jobs"), jobs}, {QStringLiteral("open"), open}, {QStringLiteral("running"), queue().isRunning()},
		{QStringLiteral("limit"), RenderQueue::MaxJobs},
		{QStringLiteral("capacity_refusals"), static_cast<double>(queue().capacityRefusals())}});
}

ControlResult queueCancel(const QJsonObject& args)
{
	int cancelled = 0;
	for (const RenderJob& job : std::vector<RenderJob>(queue().jobs()))
	{
		const bool chosen = args.value(QStringLiteral("all")).toBool(false) || args.value(QStringLiteral("job")).toInt(-1) == job.id;
		cancelled += chosen && queue().cancel(job.id) ? 1 : 0;
	}
	if (cancelled == 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no queued or running job to cancel")); }
	return ControlResult::success({{QStringLiteral("cancelled"), cancelled}});
}

ControlResult queueReport(const QJsonObject& args)
{
	const RenderJob* job = queue().job(args.value(QStringLiteral("job")).toInt());
	if (job == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such job in the render queue")); }
	return ControlResult::success(jobJson(*job));
}

// ---------------------------------------------------------------------------
// app.tasks
// ---------------------------------------------------------------------------

QJsonObject task(const QString& id, const QString& kind, const QString& state, const QString& label, double progress)
{
	return {{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}, {QStringLiteral("state"), state}, {QStringLiteral("label"), label},
		{QStringLiteral("progress"), progress}};
}

void addStemTasks(QJsonArray* tasks)
{
	const ControlResult stems = ControlRegistry::instance()->invoke(QStringLiteral("stem.job_status"));
	for (const QJsonValue& value : stems.result.value(QStringLiteral("jobs")).toArray())
	{
		const QJsonObject job = value.toObject();
		tasks->append(task(QStringLiteral("stem-%1").arg(job.value(QStringLiteral("job_id")).toInt()), QStringLiteral("stem_split"),
			job.value(QStringLiteral("state")).toString(), QStringLiteral("stem separation"), job.value(QStringLiteral("progress")).toDouble()));
	}
}

double renderTaskProgress(const RenderJob& job)
{
	if (job.state == RenderJob::State::Done) { return 1.0; }
	if (job.state == RenderJob::State::Queued) { return 0.0; }
	return job.kind == RenderJob::Kind::Bounce ? job.progress : 0.0;
}

ControlResult tasks(const QJsonObject& args)
{
	const bool activeOnly = args.value(QStringLiteral("active_only")).toBool(false);
	QJsonArray all;
	for (const RenderJob& job : queue().jobs())
	{
		all.append(task(QStringLiteral("render-%1").arg(job.id), RenderQueue::kindName(job.kind), RenderQueue::stateName(job.state), job.out,
			renderTaskProgress(job)));
	}
	const BrowserIndex::Status scan = BrowserIndex::instance().status();
	if (scan.running) { all.append(task(QStringLiteral("library-scan"), QStringLiteral("library_scan"), QStringLiteral("running"),
		QStringLiteral("%1 files scanned").arg(scan.scanned), 0.0)); }
	addStemTasks(&all);
	const QString mastering = ControlRegistry::instance()->invoke(QStringLiteral("mastering.get_state")).result.value(QStringLiteral("state")).toString();
	if (mastering == QLatin1String("running")) { all.append(task(QStringLiteral("mastering"), QStringLiteral("mastering"), mastering, QStringLiteral("auto-master"), 0.0)); }
	QJsonArray out;
	for (const QJsonValue& value : all)
	{
		const QString state = value.toObject().value(QStringLiteral("state")).toString();
		if (!activeOnly || state == QLatin1String("queued") || state == QLatin1String("running")) { out.append(value); }
	}
	return ControlResult::success({{QStringLiteral("tasks"), out}, {QStringLiteral("count"), out.size()}});
}

ControlResult taskCancel(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("id")).toString();
	if (id.startsWith(QStringLiteral("render-"))) { return queueCancel({{QStringLiteral("job"), id.mid(7).toInt()}}); }
	if (id == QLatin1String("library-scan"))
	{
		if (!BrowserIndex::instance().status().running)  // review W3-agent-library #3
		{
			return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no library scan is running"));
		}
		BrowserIndex::instance().cancel();
		return ControlResult::success({{QStringLiteral("cancelled"), id}});
	}
	if (id.startsWith(QStringLiteral("stem-")))
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("stem.job_cancel"), {{QStringLiteral("job_id"), id.mid(5).toInt()}});
	}
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' cannot be cancelled from here").arg(id));
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), false, handler);
}

} // namespace

void registerRenderQueueCommands(ControlRegistry& registry)
{
	const QJsonObject formats = enumProperty(controlRenderableFormats());
	add(registry, "render.queue_add", QStringLiteral("Queue a render to 'out' (absolute) as 'format': of the session as it is NOW "
		"(a snapshot is taken, so later edits do not change it) or of another project file ('project'), whole or one range "
		"('start_ticks' and 'end_ticks' together), with the export settings in force. Jobs run one after another in a child "
		"process without holding this surface ('start' false queues it without starting the queue now - it runs when the queue next runs); render.queue_list follows them and each ends "
		"with a report (bytes, sha256, time, loudness)."),
		{{QStringLiteral("out"), stringProperty()}, {QStringLiteral("format"), formats}, {QStringLiteral("project"), stringProperty()},
			{QStringLiteral("start_ticks"), tickProperty()}, {QStringLiteral("end_ticks"), tickProperty()}, {QStringLiteral("start"), booleanProperty()}},
		{QStringLiteral("out")}, queueAdd);
	add(registry, "render.convert", QStringLiteral("Batch conversion: queue every audio file of 'files' (absolute) to be written "
		"into 'out_dir' as 'format' (wav or flac), at 'sample_rate' (left out: the file's own) and 'bit_depth' (16, 24 - the "
		"default - or 32 float, wav only). Runs in the render queue, off the control thread, each with its report."),
		{{QStringLiteral("files"), arrayProperty()}, {QStringLiteral("out_dir"), stringProperty()},
			{QStringLiteral("format"), enumProperty({QStringLiteral("wav"), QStringLiteral("flac")})},
			{QStringLiteral("sample_rate"), integerProperty(8000, 192000)}, {QStringLiteral("bit_depth"), integerProperty(16, 32)}},
		{QStringLiteral("files"), QStringLiteral("out_dir")}, convertFiles);
	add(registry, "render.queue_list", QStringLiteral("The render queue: every job (render, convert or bounce), its state and phase, "
		"bounce progress when known, target ids, file and, once done, its report."), {}, {}, queueList);
	add(registry, "render.queue_cancel", QStringLiteral("Cancel render-queue job 'job' (or every open job with 'all'): a queued one "
		"is dropped, a running render is stopped."),
		{{QStringLiteral("job"), integerProperty()}, {QStringLiteral("all"), booleanProperty()}}, {}, queueCancel);
	add(registry, "render.queue_clear", QStringLiteral("Remove the jobs that have ended from the render queue."), {}, {},
		[](const QJsonObject&) { return ControlResult::success({{QStringLiteral("removed"), queue().clearFinished()}}); });
	add(registry, "render.queue_report", QStringLiteral("One render-queue job's current state, phase, progress and target ids, or its terminal "
		"report: bytes, sha256, seconds taken and the loudness meter's reading of the file (meter.measure_file's payload)."),
		{{QStringLiteral("job"), integerProperty()}}, {QStringLiteral("job")}, queueReport);
	add(registry, "app.tasks", QStringLiteral("Every background task in one list - render-queue jobs, the library scan, stem "
		"separation jobs and auto-mastering - each with an id, kind, state, label and progress (0..1 where it is known); "
		"'active_only' keeps the queued and running ones."), {{QStringLiteral("active_only"), booleanProperty()}}, {}, tasks);
	add(registry, "app.task_cancel", QStringLiteral("Cancel background task 'id' (as app.tasks lists it): a render-queue job, the "
		"library scan or a stem separation job."), {{QStringLiteral("id"), stringProperty()}}, {QStringLiteral("id")}, taskCancel);
	registerRenderBounceCommand(registry);
}

} // namespace lmms
