/*
 * ControlStemSupport.cpp - the socket-facing half of the offline stem-separation
 *                          engine (`stem.*`). See include/ControlStemSupport.h
 *                          for what this layer owns and why.
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

#include "ControlStemSupport.h"

#include <algorithm>
#include <memory>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QStringList>

#include "lmmsconfig.h"

#include "SampleBuffer.h"
#include "StemSeparation/StemJobManager.h"
#include "StemSeparation/StemModelStore.h"
#include "StemSeparation/StemTypes.h"

#ifdef LMMS_HAVE_ONNXRUNTIME
#include "StemSeparation/OnnxRuntimeStemSeparator.h"
#else
#include "StemSeparation/ExternalProcessStemSeparator.h"
#endif

namespace lmms
{
namespace control
{

namespace
{


//! The fixed stem order of the model contract, as wire names
//! (include/StemSeparation/StemTypes.h:35-46).
QStringList stemOrder()
{
	QStringList names;
	for (int i = 0; i < NumStems; ++i)
	{
		names << QString::fromLatin1(stemName(static_cast<Stem>(i)));
	}
	return names;
}

QString stateName(StemJobManager::State state)
{
	switch (state)
	{
	case StemJobManager::State::Queued: return QStringLiteral("queued");
	case StemJobManager::State::Running: return QStringLiteral("running");
	case StemJobManager::State::CancelRequested: return QStringLiteral("cancel_requested");
	case StemJobManager::State::Completed: return QStringLiteral("completed");
	case StemJobManager::State::Cancelled: return QStringLiteral("cancelled");
	case StemJobManager::State::Failed: return QStringLiteral("failed");
	}
	return QStringLiteral("unknown");
}

bool isTerminal(StemJobManager::State state)
{
	return state == StemJobManager::State::Completed
		|| state == StemJobManager::State::Cancelled
		|| state == StemJobManager::State::Failed;
}

/*! Writes \a buffer as float32 RIFF/WAVE. The buffer IS the interleaved stereo
 *  stream: SampleFrame is two sample_t (float) with no padding
 *  (include/SampleFrame.h:50-53, include/LmmsTypes.h:39), which the static
 *  assertion below pins so a future frame layout cannot silently corrupt a stem.
 */
//! One job this surface issued. The manager holds the job itself; this is what
//! only the surface knows: where the mix came from and when it was submitted.
struct JobRecord
{
	QString source;
	int sampleRate = StemModelSampleRate;
	int frames = 0;
	int segmentFrames = HTDemucsSegmentFrames;
	qint64 submittedMs = 0;
};

/*! The instance's ONE job manager and its ledger.
 *
 *  Created on first use and never destroyed, for the reason
 *  gui::StemSplitController's singleton is (src/gui/StemSplitController.cpp:71-85):
 *  the manager owns a worker thread, and joining it during static destruction
 *  is not safe. It is created from a command handler, so QCoreApplication
 *  exists by then.
 */
class StemSurface
{
public:
	StemSurface()
	{
		// The backend travels with the build option, not with the caller: the
		// in-process runtime is used when it was compiled in, otherwise the
		// external-process backend (python + onnxruntime), which needs no C++
		// ORT SDK and cannot take this process down with a crashing model.
#ifdef LMMS_HAVE_ONNXRUNTIME
		m_manager.setSeparator(std::make_unique<OnnxRuntimeStemSeparator>());
#else
		m_manager.setSeparator(std::make_unique<ExternalProcessStemSeparator>());
#endif
		m_backend = m_manager.backendName();
	}

	const QString& backend() const { return m_backend; }

	/*! True when the backend can run right now. A SUCCESSFUL probe is
	 *  remembered; a failing one is re-run on every call, so a model that
	 *  appears on disk between two calls is seen (and a caller polling
	 *  `stem.get_state` while the model is absent pays one interpreter probe,
	 *  which is the price of not caching a lie).
	 */
	bool available(QString* error)
	{
		if (m_available) { return true; }
		QString local;
		if (!probe(&local))
		{
			if (error != nullptr) { *error = local; }
			return false;
		}
		m_available = true;
		return true;
	}

	QJsonObject stateObject()
	{
		QString reason;
		const bool ok = available(&reason);
		const QString modelPath = StemModelStore::defaultModelPath();
		const QFileInfo modelInfo(modelPath);

		QJsonObject out;
		out.insert(QStringLiteral("backend"), m_backend);
		out.insert(QStringLiteral("available"), ok);
		out.insert(QStringLiteral("error"), ok ? QString() : reason);
		out.insert(QStringLiteral("model_path"), modelPath);
		out.insert(QStringLiteral("model_dir"), StemModelStore::defaultModelDir());
		out.insert(QStringLiteral("model_present"), StemModelStore::isModelPresent(modelPath));
		out.insert(QStringLiteral("model_bytes"), static_cast<double>(modelInfo.size()));
		out.insert(QStringLiteral("sample_rate"), StemModelSampleRate);
		out.insert(QStringLiteral("segment_frames"), HTDemucsSegmentFrames);
		out.insert(QStringLiteral("lookahead_seconds"),
			static_cast<double>(HTDemucsSegmentFrames) / StemModelSampleRate);
		out.insert(QStringLiteral("realtime"), false);
		out.insert(QStringLiteral("realtime_reason"), QStringLiteral(
			"HTDemucs needs the whole %1-frame segment (%2 s) as context, so no chunk fits an "
			"audio block: separation is an offline job only")
			.arg(HTDemucsSegmentFrames)
			.arg(static_cast<double>(HTDemucsSegmentFrames) / StemModelSampleRate, 0, 'f', 1));
		QJsonArray order;
		for (const QString& name : stemOrder()) { order.append(name); }
		out.insert(QStringLiteral("stem_order"), order);
		out.insert(QStringLiteral("jobs"), jobCounts());
		return out;
	}

	bool start(const QString& source,
		int segmentFrames,
		int* jobId,
		QJsonObject* job,
		QString* error)
	{
		QString reason;
		if (!available(&reason))
		{
			// The engine's own sentence, not a paraphrase: it names what is
			// missing (the interpreter with onnxruntime, the CLI, the model).
			*error = reason;
			return false;
		}
		const QFileInfo info(source);
		if (!info.exists() || !info.isFile())
		{
			*error = QStringLiteral("there is no source file at '%1'").arg(source);
			return false;
		}
		auto mix = SampleBuffer::fromFile(source);
		if (!mix || mix->empty())
		{
			*error = QStringLiteral("could not decode '%1' as stereo audio (the decoders this "
				"build carries are the browser's own; a mono or unsupported file cannot be split)")
				.arg(source);
			return false;
		}
		// No resampler, by decision, and the refusal says which value is wrong
		// (SPEC-stem-split.md OQ-1, the same refusal
		// ExternalProcessStemSeparator::separate makes):
		if (mix->sampleRate() != StemModelSampleRate)
		{
			*error = QStringLiteral("'%1' is %2 Hz; stem separation needs %3 Hz and resampling is "
				"not implemented yet (SPEC-stem-split.md OQ-1)")
				.arg(source).arg(mix->sampleRate()).arg(StemModelSampleRate);
			return false;
		}
		JobRecord record;
		record.source = source;
		record.sampleRate = StemModelSampleRate;
		record.frames = static_cast<int>(mix->size());
		record.segmentFrames = segmentFrames > 0 ? segmentFrames : HTDemucsSegmentFrames;
		record.submittedMs = QDateTime::currentMSecsSinceEpoch();

		const int id = m_manager.submit(std::move(mix), record.sampleRate, record.segmentFrames);
		if (id < 0)
		{
			*error = QStringLiteral("the stem engine refused the job");
			return false;
		}
		m_jobs.insert(id, record);
		*jobId = id;
		*job = jobObject(id, record, m_manager.state(id));
		return true;
	}

	bool has(int jobId) const { return m_jobs.contains(jobId); }

	QJsonObject jobs(int jobId, bool anyJob, bool* found)
	{
		QJsonArray array;
		*found = true;
		if (anyJob)
		{
			if (!m_jobs.contains(jobId))
			{
				*found = false;
				return QJsonObject();
			}
			array.append(jobObject(jobId, m_jobs.value(jobId), m_manager.state(jobId)));
		}
		else
		{
			QList<int> ids = m_jobs.keys();
			std::sort(ids.begin(), ids.end());
			for (const int id : ids)
			{
				array.append(jobObject(id, m_jobs.value(id), m_manager.state(id)));
			}
		}
		QJsonObject out;
		out.insert(QStringLiteral("count"), array.size());
		out.insert(QStringLiteral("running"), m_manager.runningJobCount());
		out.insert(QStringLiteral("jobs"), array);
		return out;
	}

	bool cancel(int jobId, QJsonObject* job, QString* error)
	{
		if (!m_jobs.contains(jobId))
		{
			*error = QStringLiteral("no stem job with id %1 was issued by this instance "
				"(stem.job_status lists the ones that were)").arg(jobId);
			return false;
		}
		const auto state = m_manager.state(jobId);
		if (isTerminal(state))
		{
			*error = QStringLiteral("stem job %1 is already %2: there is nothing to cancel")
				.arg(jobId).arg(stateName(state));
			return false;
		}
		m_manager.cancel(jobId);
		*job = jobObject(jobId, m_jobs.value(jobId), m_manager.state(jobId));
		return true;
	}

	bool writeResult(int jobId, const QString& directory, QJsonObject* result, QString* error)
	{
		if (!m_jobs.contains(jobId))
		{
			*error = QStringLiteral("no stem job with id %1 was issued by this instance "
				"(stem.job_status lists the ones that were)").arg(jobId);
			return false;
		}
		const auto state = m_manager.state(jobId);
		if (state != StemJobManager::State::Completed)
		{
			*error = QStringLiteral("stem job %1 is %2, not completed: only a completed job has "
				"four stems to write (stem.job_status reports the state, and the job's own error "
				"is in it when it failed)").arg(jobId).arg(stateName(state));
			return false;
		}
		const StemSet stems = m_manager.result(jobId);
		QDir().mkpath(directory);
		if (!QFileInfo(directory).isDir())
		{
			*error = QStringLiteral("cannot create the output directory '%1'").arg(directory);
			return false;
		}

		const JobRecord record = m_jobs.value(jobId);
		QJsonArray files;
		for (int i = 0; i < NumStems; ++i)
		{
			const QString name = QString::fromLatin1(stemName(static_cast<Stem>(i)));
			if (stems[static_cast<size_t>(i)] == nullptr)
			{
				*error = QStringLiteral("stem job %1 has no '%2' stem").arg(jobId).arg(name);
				return false;
			}
			const QString path = QDir(directory).filePath(name + QStringLiteral(".wav"));
			if (!stemWriteWavFile(path, *stems[static_cast<size_t>(i)], record.sampleRate, error))
			{
				return false;
			}
			QJsonObject entry;
			entry.insert(QStringLiteral("name"), QFileInfo(path).fileName());
			entry.insert(QStringLiteral("stem"), name);
			entry.insert(QStringLiteral("frames"),
				static_cast<double>(stems[static_cast<size_t>(i)]->size()));
			entry.insert(QStringLiteral("seconds"),
				static_cast<double>(stems[static_cast<size_t>(i)]->size()) / record.sampleRate);
			entry.insert(QStringLiteral("bytes"), static_cast<double>(QFileInfo(path).size()));
			entry.insert(QStringLiteral("sha256"), stemSha256OfFile(path));
			files.append(entry);
		}

		QJsonObject out;
		out.insert(QStringLiteral("job_id"), jobId);
		out.insert(QStringLiteral("directory"), directory);
		out.insert(QStringLiteral("format"), QStringLiteral("wav"));
		out.insert(QStringLiteral("sample_format"), QStringLiteral("float32"));
		out.insert(QStringLiteral("sample_rate"), record.sampleRate);
		out.insert(QStringLiteral("count"), files.size());
		out.insert(QStringLiteral("stems"), files);
		out.insert(QStringLiteral("source"), record.source);
		*result = out;
		return true;
	}

private:
	bool probe(QString* error) const
	{
#ifdef LMMS_HAVE_ONNXRUNTIME
		// The in-process backend needs the model and nothing else (no
		// interpreter, no CLI script).
		if (!StemModelStore::isModelPresent(StemModelStore::defaultModelPath(), error))
		{
			*error += QStringLiteral(" (the in-process ONNX Runtime backend was compiled in; "
				"set LMMS_STEM_MODEL or drop the model into the model directory)");
			return false;
		}
		return true;
#else
		return ExternalProcessStemSeparator::isAvailable(error);
#endif
	}

	QJsonObject jobCounts() const
	{
		QJsonObject counts;
		int completed = 0;
		int failed = 0;
		int cancelled = 0;
		for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it)
		{
			switch (m_manager.state(it.key()))
			{
			case StemJobManager::State::Completed: ++completed; break;
			case StemJobManager::State::Failed: ++failed; break;
			case StemJobManager::State::Cancelled: ++cancelled; break;
			default: break;
			}
		}
		counts.insert(QStringLiteral("known"), m_jobs.size());
		counts.insert(QStringLiteral("running"), m_manager.runningJobCount());
		counts.insert(QStringLiteral("completed"), completed);
		counts.insert(QStringLiteral("failed"), failed);
		counts.insert(QStringLiteral("cancelled"), cancelled);
		return counts;
	}

	QJsonObject jobObject(int jobId, const JobRecord& record, StemJobManager::State state) const
	{
		QJsonObject out;
		out.insert(QStringLiteral("job_id"), jobId);
		out.insert(QStringLiteral("state"), stateName(state));
		out.insert(QStringLiteral("progress"), static_cast<double>(m_manager.progress(jobId)));
		out.insert(QStringLiteral("source"), record.source);
		out.insert(QStringLiteral("sample_rate"), record.sampleRate);
		out.insert(QStringLiteral("frames"), record.frames);
		out.insert(QStringLiteral("seconds"), static_cast<double>(record.frames) / record.sampleRate);
		out.insert(QStringLiteral("segment_frames"), record.segmentFrames);
		// The SURFACE's clock, named as such: the engine keeps its own elapsed
		// time on the job and publishes none of it through an accessor, so this
		// is the wall time since submit and never a claim about inference time.
		out.insert(QStringLiteral("since_submit_seconds"),
			(QDateTime::currentMSecsSinceEpoch() - record.submittedMs) / 1000.0);
		out.insert(QStringLiteral("backend"), m_backend);
		out.insert(QStringLiteral("error"), m_manager.error(jobId));
		return out;
	}

	StemJobManager m_manager;
	QHash<int, JobRecord> m_jobs;
	QString m_backend;
	bool m_available = false;
};

//! The process-wide surface. Created on first use, never destroyed (see the
//! class comment).
StemSurface* surface()
{
	static StemSurface* s_surface = new StemSurface();
	return s_surface;
}

} // namespace


QString stemRequireAbsolutePath(const QString& value, const QString& name)
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


QJsonObject stemState()
{
	return surface()->stateObject();
}

bool stemStartJob(const QString& source,
	int segmentFrames,
	int* jobId,
	QJsonObject* job,
	QString* error)
{
	return surface()->start(source, segmentFrames, jobId, job, error);
}

bool stemHasJob(int jobId)
{
	return surface()->has(jobId);
}

QJsonObject stemJobs(int jobId, bool anyJob, bool* found)
{
	return surface()->jobs(jobId, anyJob, found);
}

bool stemCancelJob(int jobId, QJsonObject* job, QString* error)
{
	return surface()->cancel(jobId, job, error);
}

bool stemWriteResult(int jobId, const QString& directory, QJsonObject* result, QString* error)
{
	return surface()->writeResult(jobId, directory, result, error);
}

} // namespace control
} // namespace lmms
