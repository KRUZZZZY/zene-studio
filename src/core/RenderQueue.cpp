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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThreadPool>
#include <QTimer>

#include <sndfile.h>

#include "BounceInPlace.h"
#include "ControlExportPresetSupport.h"
#include "ControlMeterSupport.h"
#include "ControlRegistry.h"
#include "SampleBuffer.h"
#include "SampleBufferResample.h"
#include "SampleDecoder.h"

namespace lmms
{

namespace
{

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

int sndfileFormat(const RenderJob& job)
{
	const int major = job.format == QLatin1String("flac") ? SF_FORMAT_FLAC : SF_FORMAT_WAV;
	if (job.bitDepth == 16) { return major | SF_FORMAT_PCM_16; }
	// FLAC has no float samples: 32 becomes 24 there.
	return major | (job.bitDepth == 32 && major == SF_FORMAT_WAV ? SF_FORMAT_FLOAT : SF_FORMAT_PCM_24);
}

//! Where a conversion is written first. It is moved over the destination only once complete, so a failed or
//! cancelled conversion never costs the file already there - which can be the source itself, converted in
//! place (review L17 #1, #4).
QString stagedPath(const RenderJob& job) { return job.out + QStringLiteral(".part"); }

QString writeFile(const RenderJob& job, const SampleBuffer& buffer)
{
	SF_INFO info{};
	info.samplerate = static_cast<int>(buffer.sampleRate());
	info.channels = 2;
	info.format = sndfileFormat(job);
	QDir().mkpath(QFileInfo(job.out).absolutePath());
	const QString path = stagedPath(job);
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return QStringLiteral("cannot write %1: %2").arg(job.out, QString::fromLocal8Bit(sf_strerror(nullptr))); }
	std::vector<float> interleaved(buffer.size() * 2);
	for (std::size_t i = 0; i < buffer.size(); ++i)
	{
		interleaved[2 * i] = buffer.data()[i][0];
		interleaved[2 * i + 1] = buffer.data()[i][1];
	}
	const auto frames = static_cast<sf_count_t>(buffer.size());
	const bool written = sf_writef_float(file, interleaved.data(), frames) == frames;
	const bool closed = sf_close(file) == 0;
	if (written && closed) { return QString(); }
	QFile::remove(path);
	return QStringLiteral("writing %1 failed").arg(job.out);
}

//! The staged conversion moved over its destination; empty on success, else the reason.
QString commitStaged(const RenderJob& job)
{
	const QString path = stagedPath(job);
	if ((!QFile::exists(job.out) || QFile::remove(job.out)) && QFile::rename(path, job.out)) { return QString(); }
	QFile::remove(path);
	return QStringLiteral("writing %1 failed").arg(job.out);
}

//! Decode @a job's source and write it, staged; empty on success, else the reason.
QString staged(const RenderJob& job)
{
	std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(job.source);
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0) { return QStringLiteral("%1 could not be read as audio").arg(job.source); }
	std::shared_ptr<const SampleBuffer> buffer = std::make_shared<const SampleBuffer>(std::move(decoded->data), decoded->sampleRate);
	if (job.sampleRate > 0) { buffer = resampleBuffer(buffer, static_cast<sample_rate_t>(job.sampleRate)); }
	return writeFile(job, *buffer);
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

//! A running job's end: done when it wrote a file (with its report), failed otherwise.
void conclude(RenderJob* job, bool ok, int exitCode, const QString& error)
{
	job->finishedMs = now();
	job->exitCode = exitCode;
	const bool written = ok && QFileInfo(job->out).size() > 0;
	job->state = written ? RenderJob::State::Done : RenderJob::State::Failed;
	job->error = written ? QString() : (!error.isEmpty() ? error : QStringLiteral("no file was written (exit %1)").arg(exitCode));
	if (written) { job->report = reportOf(*job); }
}

//! A render cancelled while it ran keeps no output: what its killed process wrote since the job started,
//! whole or in part (review L17 #2). A file older than the job is someone else's and stays.
void discardOutput(const RenderJob& job)
{
	const QFileInfo out(job.out);
	if (out.exists() && out.lastModified().toMSecsSinceEpoch() >= job.startedMs) { QFile::remove(job.out); }
}

//! A job's end once its process or worker is done: a conversion's staged file is committed when the job is
//! still running and wrote it, dropped otherwise; a cancelled render's output is discarded.
void endJob(RenderJob* job, bool ok, int exitCode, QString error)
{
	const bool running = job->state == RenderJob::State::Running;
	if (job->kind == RenderJob::Kind::Convert)
	{
		if (running && ok) { error = commitStaged(*job); ok = error.isEmpty(); }
		else { QFile::remove(stagedPath(*job)); }
	}
	else if (job->state == RenderJob::State::Cancelled) { discardOutput(*job); }
	if (running) { conclude(job, ok, exitCode, error); }
	if (job->ownsSource) { QFile::remove(job->source); }
}

} // namespace

RenderQueue& RenderQueue::instance()
{
	static RenderQueue* queue = new RenderQueue();
	return *queue;
}

QString RenderQueue::kindName(RenderJob::Kind kind)
{
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
	if (static_cast<int>(m_jobs.size()) >= MaxJobs) { return -1; }
	job.id = m_nextId++;
	job.state = RenderJob::State::Queued;
	job.queuedMs = now();
	m_jobs.push_back(job);
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
	queued->startedMs = now();
	m_current = queued->id;
	if (queued->kind == RenderJob::Kind::Render) { runRender(*queued); }
	else { runConvert(*queued); }
}

void RenderQueue::runRender(RenderJob& job)
{
	const int id = job.id;
	m_process = new QProcess(this);
	m_process->setStandardOutputFile(QProcess::nullDevice());
	m_process->setStandardErrorFile(QProcess::nullDevice());
	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, id](int exitCode, QProcess::ExitStatus status) {
		finish(id, status == QProcess::NormalExit && exitCode == 0, exitCode,
			status == QProcess::NormalExit ? QString() : QStringLiteral("the render process crashed"));
	});
	connect(m_process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) { finish(id, false, -1, QStringLiteral("the render process could not be started")); }
	});
	m_process->start(QCoreApplication::applicationFilePath(), controlRenderCliArguments(job.source, job.out, job.format,
		ControlExportPresetSettings::effective(), job.rangeBegin, job.rangeEnd));
}

void RenderQueue::runConvert(RenderJob& job)
{
	const RenderJob copy = job;
	QThreadPool::globalInstance()->start([this, copy] {
		const QString error = staged(copy);  // committed by finish(), unless the job was cancelled meanwhile
		QMetaObject::invokeMethod(this, [this, id = copy.id, error] { finish(id, error.isEmpty(), error.isEmpty() ? 0 : 1, error); },
			Qt::QueuedConnection);
	});
}

QString RenderQueue::convert(const RenderJob& job)
{
	const QString error = staged(job);
	return error.isEmpty() ? commitStaged(job) : error;
}

void RenderQueue::finish(int id, bool ok, int exitCode, const QString& error)
{
	RenderJob* job = find(id);
	if (job != nullptr) { endJob(job, ok, exitCode, error); }
	if (m_process != nullptr)
	{
		m_process->deleteLater();
		m_process = nullptr;
	}
	if (m_current == id) { m_current = -1; }
	QTimer::singleShot(0, this, &RenderQueue::next);
}

bool RenderQueue::cancel(int id)
{
	RenderJob* job = find(id);
	if (job == nullptr || (job->state != RenderJob::State::Queued && job->state != RenderJob::State::Running)) { return false; }
	const bool running = job->state == RenderJob::State::Running;
	job->state = RenderJob::State::Cancelled;
	job->finishedMs = now();
	if (!running && job->ownsSource) { QFile::remove(job->source); }
	// A running render's process is stopped: its finished signal then ends the job (finish() sees it
	// cancelled and removes what it wrote). A running conversion completes on its worker and is then
	// dropped the same way, its staged file never moved into place (review L17 #1, #2).
	if (running && m_process != nullptr) { m_process->kill(); }
	return true;
}

int RenderQueue::clearFinished()
{
	const auto before = m_jobs.size();
	// A cancelled job whose render or conversion is still winding down stays until finish() has cleaned
	// up after it (its snapshot, its output; review L17 #3).
	m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [this](const RenderJob& j) {
		return j.state != RenderJob::State::Queued && j.state != RenderJob::State::Running && j.id != m_current;
	}), m_jobs.end());
	return static_cast<int>(before - m_jobs.size());
}

} // namespace lmms
