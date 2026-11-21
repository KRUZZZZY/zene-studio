/*
 * RenderQueue.cpp - renders and file conversions queued and run one after another, with a report each
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

#include "RenderQueue.h"

#include <algorithm>
#include <memory>
#include <optional>

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include <sndfile.h>

#include "BounceInPlace.h"
#include "ControlExportPresetSupport.h"
#include "ControlMeterSupport.h"
#include "SampleBuffer.h"
#include "SampleBufferResample.h"
#include "SampleDecoder.h"
#include "TaskCenter.h"

namespace lmms
{

namespace
{

constexpr qsizetype RenderDiagnosticLimit = 512;

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

void appendDiagnosticTail(QByteArray& output, const QByteArray& chunk)
{
	if (chunk.size() >= RenderDiagnosticLimit)
	{
		output = chunk.right(RenderDiagnosticLimit);
		return;
	}
	const qsizetype excess = output.size() + chunk.size() - RenderDiagnosticLimit;
	if (excess > 0) { output.remove(0, excess); }
	output.append(chunk);
}

QString withRenderDiagnostic(QString error, const QByteArray& diagnostic)
{
	const QString detail = QString::fromLocal8Bit(diagnostic).trimmed();
	if (detail.isEmpty()) { return error; }
	return error.isEmpty() ? detail : QStringLiteral("%1: %2").arg(error, detail);
}

bool conversionCancelled(const std::shared_ptr<std::atomic_bool>& cancellation)
{
	return cancellation != nullptr && cancellation->load(std::memory_order_acquire);
}

QString renderOutputPath(const RenderJob& job)
{
	if (job.renderDestination.isEmpty())
	{
		const QFileInfo out(job.out);
		job.renderDestination = out.isSymLink() ? out.symLinkTarget() : job.out;
		if (job.renderDestination.isEmpty()) { job.renderDestination = job.out; }
	}
	return job.renderDestination;
}

int sndfileFormat(const RenderJob& job)
{
	const int major = job.format == QLatin1String("flac") ? SF_FORMAT_FLAC : SF_FORMAT_WAV;
	if (job.bitDepth == 16) { return major | SF_FORMAT_PCM_16; }
	// FLAC has no float samples: 32 becomes 24 there.
	return major | (job.bitDepth == 32 && major == SF_FORMAT_WAV ? SF_FORMAT_FLOAT : SF_FORMAT_PCM_24);
}

//! Where a render or conversion is written first. It is moved over the destination only once complete, so a
//! failed or cancelled job never costs the file already there - which can be the source itself, converted in
//! place (review L17 #1, #4). A name of the job's own, made once: a fixed "out.part" would be truncated by
//! every job and removed by every cancellation, taking a file the user had put there with it.
QString stagedPath(const RenderJob& job)
{
	if (job.staged.isEmpty())
	{
		const QString unique = QStringLiteral(".part.") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
		if (job.kind == RenderJob::Kind::Render)
		{
			const QFileInfo out(renderOutputPath(job));
			job.staged = out.dir().filePath(QStringLiteral(".zene-render-")
				+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(12) + QLatin1Char('.') + job.format);
		}
		else { job.staged = job.out + unique; }
	}
	return job.staged;
}

QString writeFile(const RenderJob& job, const SampleBuffer& buffer,
	const std::shared_ptr<std::atomic_bool>& cancellation)
{
	if (conversionCancelled(cancellation)) { return QString(); }
	SF_INFO info{};
	info.samplerate = static_cast<int>(buffer.sampleRate());
	info.channels = 2;
	info.format = sndfileFormat(job);
	QDir().mkpath(QFileInfo(job.out).absolutePath());
	const QString path = stagedPath(job);
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return QStringLiteral("cannot write %1: %2").arg(job.out, QString::fromLocal8Bit(sf_strerror(nullptr))); }
	constexpr std::size_t WriteChunkFrames = 4096;
	std::vector<float> interleaved(std::min(buffer.size(), WriteChunkFrames) * 2);
	bool written = true;
	for (std::size_t first = 0; first < buffer.size(); first += WriteChunkFrames)
	{
		if (conversionCancelled(cancellation)) { written = false; break; }
		const std::size_t frames = std::min(buffer.size() - first, WriteChunkFrames);
		for (std::size_t i = 0; i < frames; ++i)
		{
			interleaved[2 * i] = buffer.data()[first + i][0];
			interleaved[2 * i + 1] = buffer.data()[first + i][1];
		}
		if (sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(frames)) != static_cast<sf_count_t>(frames))
		{
			written = false;
			break;
		}
	}
	const bool closed = sf_close(file) == 0;
	if (conversionCancelled(cancellation))
	{
		QFile::remove(path);
		return QString();
	}
	if (written && closed) { return QString(); }
	QFile::remove(path);
	return QStringLiteral("writing %1 failed").arg(job.out);
}

//! The staged output moved over its destination; empty on success, else the reason. The old
//! destination is moved aside, not removed, until the staged file is in place: a failed rename
//! puts it back instead of leaving neither file.
QString commitStaged(const RenderJob& job)
{
	const QString path = stagedPath(job);
	const QString destination = job.kind == RenderJob::Kind::Render ? renderOutputPath(job) : job.out;
	const bool hadOld = QFile::exists(destination);
	QString aside;
	if (hadOld)
	{
		do
		{
			aside = QFileInfo(destination).dir().filePath(QStringLiteral(".zene-backup-")
				+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(12));
		} while (QFile::exists(aside) || aside == job.source);
		if (!QFile::rename(destination, aside))
		{
			QFile::remove(path);
			return QStringLiteral("writing %1 failed").arg(job.out);
		}
	}
	if (QFile::rename(path, destination))
	{
		if (hadOld) { QFile::remove(aside); }
		return QString();
	}
	if (hadOld && !QFile::exists(destination)) { QFile::rename(aside, destination); }
	QFile::remove(path);
	return QStringLiteral("writing %1 failed").arg(job.out);
}

//! Decode @a job's source and write it, staged; empty on success or cancellation, else the reason.
QString staged(const RenderJob& job, const std::shared_ptr<std::atomic_bool>& cancellation = {})
{
	if (conversionCancelled(cancellation)) { return QString(); }
	std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(job.source);
	if (conversionCancelled(cancellation)) { return QString(); }
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0) { return QStringLiteral("%1 could not be read as audio").arg(job.source); }
	std::shared_ptr<const SampleBuffer> buffer = std::make_shared<const SampleBuffer>(std::move(decoded->data), decoded->sampleRate);
	if (conversionCancelled(cancellation)) { return QString(); }
	if (job.sampleRate > 0) { buffer = resampleBuffer(buffer, static_cast<sample_rate_t>(job.sampleRate)); }
	if (conversionCancelled(cancellation)) { return QString(); }
	return writeFile(job, *buffer, cancellation);
}

QJsonObject reportOf(const RenderJob& job)
{
	QJsonObject report{{QStringLiteral("bytes"), static_cast<double>(QFileInfo(job.out).size())},
		{QStringLiteral("sha256"), BounceInPlace::sha256OfFile(job.out)},
		{QStringLiteral("seconds_taken"), static_cast<double>(job.finishedMs - job.startedMs) / 1000.0}};
	const ControlResult measured = control::meterMeasureFile(job.out);
	if (measured.ok) { report.insert(QStringLiteral("loudness"), measured.result); }
	else { report.insert(QStringLiteral("loudness_error"), measured.errorMessage); }
	return report;
}

//! Whether the job wrote its output: a render's file must be newer than the job (in whole seconds, for a
//! file system that keeps no finer times), so a file already there and a process that exited 0 without
//! writing is not reported as this render. A conversion's output is its own committed staged file.
bool writtenByJob(const RenderJob& job)
{
	const QFileInfo out(job.out);
	return out.size() > 0
		&& (job.kind == RenderJob::Kind::Convert || out.lastModified().toMSecsSinceEpoch() >= job.startedMs / 1000 * 1000);
}

//! A running job's end: done when it wrote a file (with its report), failed otherwise.
void conclude(RenderJob* job, bool ok, int exitCode, const QString& error)
{
	job->finishedMs = now();
	job->exitCode = exitCode;
	const bool written = ok && writtenByJob(*job);
	job->state = written ? RenderJob::State::Done : RenderJob::State::Failed;
	job->error = written ? QString() : (!error.isEmpty() ? error : QStringLiteral("no file was written (exit %1)").arg(exitCode));
	if (written) { job->report = reportOf(*job); }
}

//! A job's end once its process or worker is done: its staged file is committed when it succeeded while
//! running, dropped otherwise. A previous destination stays in place until the staged file is ready.
void endJob(RenderJob* job, bool ok, int exitCode, QString error)
{
	const bool running = job->state == RenderJob::State::Running;
	if (job->kind == RenderJob::Kind::Convert || job->kind == RenderJob::Kind::Render)
	{
		if (running && ok) { error = commitStaged(*job); ok = error.isEmpty(); }
		else { QFile::remove(stagedPath(*job)); }
	}
	if (running) { conclude(job, ok, exitCode, error); }
	if (job->ownsSource) { QFile::remove(job->source); }
}

//! 0.5.0 theme 24: what the task area calls this job.
QString taskTitle(const RenderJob& job)
{
	const QString name = QFileInfo(job.out).fileName();
	if (job.kind == RenderJob::Kind::Bounce)
	{
		const QString title = job.operation == QLatin1String("freeze_track") || job.operation == QLatin1String("freeze_region")
			? QObject::tr("Freeze") : job.operation == QLatin1String("bounce_land") ? QObject::tr("Bounce to track")
			: job.operation == QLatin1String("flatten") ? QObject::tr("Flatten")
			: job.operation == QLatin1String("range_bounce") ? QObject::tr("Range bounce") : QObject::tr("Bounce");
		return QObject::tr("%1 %2").arg(title, name);
	}
	return job.kind == RenderJob::Kind::Render ? QObject::tr("Render %1").arg(name) : QObject::tr("Convert %1").arg(name);
}

//! The task area's state for an ended job.
TaskState taskState(RenderJob::State state)
{
	if (state == RenderJob::State::Done) { return TaskState::Done; }
	return state == RenderJob::State::Cancelled ? TaskState::Cancelled : TaskState::Failed;
}

} // namespace

RenderQueue& RenderQueue::instance()
{
	static RenderQueue* queue = new RenderQueue();
	return *queue;
}

QString RenderQueue::kindName(RenderJob::Kind kind)
{
	if (kind == RenderJob::Kind::Bounce) { return QStringLiteral("bounce"); }
	return kind == RenderJob::Kind::Render ? QStringLiteral("render") : QStringLiteral("convert");
}

QString RenderQueue::stateName(RenderJob::State state)
{
	static const QStringList names{QStringLiteral("queued"), QStringLiteral("running"), QStringLiteral("done"), QStringLiteral("failed"),
		QStringLiteral("cancelled")};
	return names.value(static_cast<int>(state));
}

int RenderQueue::add(RenderJob job)
{
	if (!hasCapacity()) { ++m_capacityRefusals; return -1; }
	job.id = m_nextId++;
	job.state = RenderJob::State::Queued;
	job.phase = QStringLiteral("queued");
	job.queuedMs = now();
	if (job.kind == RenderJob::Kind::Render) { job.settings = ControlExportPresetSettings::effective(); }
	m_jobs.push_back(job);
	// 0.5.0 theme 24: the job is listed in the task area from the moment it is queued, with the queue's own cancel.
	m_jobs.back().taskToken = TaskCenter::instance().begin(this, job.id,
		{kindName(job.kind), taskTitle(job), [id = job.id] { instance().cancel(id); }, {}, true});
	return job.id;
}

const RenderJob* RenderQueue::job(int id) const
{
	const auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [id](const RenderJob& j) { return j.id == id; });
	return it != m_jobs.end() ? &*it : nullptr;
}

RenderJob* RenderQueue::find(int id)
{
	return const_cast<RenderJob*>(job(id));
}

void RenderQueue::start()
{
	if (!isRunning()) { next(); }
}

void RenderQueue::next()
{
	if (isRunning()) { return; }
	const auto queued = std::find_if(m_jobs.begin(), m_jobs.end(), [](const RenderJob& j) { return j.state == RenderJob::State::Queued; });
	if (queued == m_jobs.end()) { return; }
	queued->state = RenderJob::State::Running;
	queued->phase = queued->kind == RenderJob::Kind::Bounce ? QStringLiteral("starting") : QStringLiteral("running");
	queued->startedMs = now();
	m_current = queued->id;
	TaskCenter::instance().progress(queued->taskToken, -1.0,
		queued->kind == RenderJob::Kind::Bounce ? tr("Starting render") : QString());
	if (queued->kind == RenderJob::Kind::Render) { runRender(*queued); }
	else if (queued->kind == RenderJob::Kind::Bounce) { runBounce(*queued); }
	else { runConvert(*queued); }
}

void RenderQueue::runRender(RenderJob& job)
{
	const int id = job.id;
	const TaskToken taskToken = job.taskToken;
	if (QFileInfo(job.out).isDir())
	{
		finish(taskToken, id, false, -1, QStringLiteral("the render output path is a directory"));
		return;
	}
	m_process = new QProcess(this);
	QProcess* const process = m_process;
	auto diagnostics = std::make_shared<QByteArray>();
	m_process->setStandardOutputFile(QProcess::nullDevice());
	connect(m_process, &QProcess::readyReadStandardError, this, [process, diagnostics] {
		appendDiagnosticTail(*diagnostics, process->readAllStandardError());
	});
	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
		[this, id, taskToken, process, diagnostics](int exitCode, QProcess::ExitStatus status) {
		appendDiagnosticTail(*diagnostics, process->readAllStandardError());
		QString error;
		if (status != QProcess::NormalExit) { error = QStringLiteral("the render process crashed"); }
		else if (exitCode != 0) { error = QStringLiteral("the render process exited with code %1").arg(exitCode); }
		finish(taskToken, id, status == QProcess::NormalExit && exitCode == 0, exitCode,
			withRenderDiagnostic(error, *diagnostics));
	});
	connect(m_process, &QProcess::errorOccurred, this, [this, id, taskToken, process, diagnostics](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart)
		{
			appendDiagnosticTail(*diagnostics, process->readAllStandardError());
			finish(taskToken, id, false, -1,
				withRenderDiagnostic(QStringLiteral("the render process could not be started: %1").arg(process->errorString()), *diagnostics));
		}
	});
	m_process->start(QCoreApplication::applicationFilePath(), controlRenderCliArguments(job.source, stagedPath(job), job.format,
		job.settings, job.rangeBegin, job.rangeEnd));
}

void RenderQueue::runConvert(RenderJob& job)
{
	stagedPath(job);  // the worker writes it and finish() commits it: both need this job's own name
	job.conversionCancellation = std::make_shared<std::atomic_bool>(false);
	job.conversionWorkerActive = true;
	const RenderJob copy = job;
	QThreadPool::globalInstance()->start([this, copy] {
		const QString error = staged(copy, copy.conversionCancellation);  // committed by finish(), unless cancelled
		if (conversionCancelled(copy.conversionCancellation)) { QFile::remove(stagedPath(copy)); }
		QMetaObject::invokeMethod(this, [this, id = copy.id, token = copy.taskToken, error] {
			finish(token, id, error.isEmpty(), error.isEmpty() ? 0 : 1, error);
		}, Qt::QueuedConnection);
	});
}

QString RenderQueue::convert(const RenderJob& job)
{
	const QString error = staged(job);
	return error.isEmpty() ? commitStaged(job) : error;
}

void RenderQueue::finish(const TaskToken& token, int id, bool ok, int exitCode, const QString& error)
{
	RenderJob* job = find(id);
	if (job != nullptr)
	{
		if (job->kind == RenderJob::Kind::Convert) { job->conversionWorkerActive = false; }
		endJob(job, ok, exitCode, error);
		job->progress = job->state == RenderJob::State::Done ? 1.0 : job->progress;
		job->phase = job->state == RenderJob::State::Done ? QStringLiteral("done")
			: job->state == RenderJob::State::Cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
		TaskCenter::instance().finish(token, taskState(job->state), job->error);
	}
	if (m_process != nullptr && id == m_current)
	{
		m_process->deleteLater();
		m_process = nullptr;
	}
	releaseCurrent(id);
}

bool RenderQueue::cancel(int id)
{
	RenderJob* job = find(id);
	if (job == nullptr || (job->state != RenderJob::State::Queued && job->state != RenderJob::State::Running)) { return false; }
	const bool running = job->state == RenderJob::State::Running;
	job->state = RenderJob::State::Cancelled;
	job->finishedMs = now();
	job->phase = running && job->kind == RenderJob::Kind::Bounce
		? QStringLiteral("cancelling") : QStringLiteral("cancelled");
	if (!running) { cancelQueued(*job); return true; }
	// A conversion's worker checks its token at stage boundaries and while writing; release the queue now
	// so its next job can start while an in-progress decoder or resampler unwinds.
	if (job->kind == RenderJob::Kind::Convert)
	{
		if (job->conversionCancellation != nullptr) { job->conversionCancellation->store(true, std::memory_order_release); }
		TaskCenter::instance().finish(job->taskToken, TaskState::Cancelled);
		releaseCurrent(id);
	}
	else if (m_process != nullptr) { m_process->kill(); }
	return true;
}

void RenderQueue::cancelQueued(RenderJob& job)
{
	if (job.ownsSource) { QFile::remove(job.source); }
	if (job.kind == RenderJob::Kind::Bounce)
	{
		BounceInPlace::discardPreparedRender(&job.bounce);
		job.validateBounce = {};
		job.applyBounce = {};
	}
	TaskCenter::instance().finish(job.taskToken, TaskState::Cancelled);
}

int RenderQueue::clearFinished()
{
	const auto before = m_jobs.size();
	// A cancelled job whose render or conversion is still winding down stays until finish() has cleaned
	// up after it (its snapshot, its output; review L17 #3).
	m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [this](const RenderJob& j) {
		return j.state != RenderJob::State::Queued && j.state != RenderJob::State::Running
			&& j.id != m_current && !j.conversionWorkerActive;
	}), m_jobs.end());
	return static_cast<int>(before - m_jobs.size());
}

} // namespace lmms
