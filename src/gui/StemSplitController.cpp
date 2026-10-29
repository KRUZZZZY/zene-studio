/*
 * StemSplitController.cpp - GUI glue for the "Split to stems" action
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

#include "StemSplitController.h"

#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTemporaryFile>
#include <QTimer>

#include "ControlRegistry.h"
#include "ControlStemSupport.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "PathUtil.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

#include "StemSeparation/StemTypes.h"

namespace lmms::gui
{

namespace
{
//! The poll interval of the progress dialog. The job runs on the surface's own
//! worker; this reads its atomics, so it is cheap and answers in milliseconds.
constexpr int PollIntervalMs = 150;

//! Where pollJob reads a job's status from. Empty means stem.job_status through
//! the registry; the pipeline test replaces it with a scripted reader.
StemSplitController::StatusReader s_statusReader;
} // namespace

StemSplitController::StemSplitController(QWidget* parent) :
	QObject(parent),
	m_parent(parent),
	m_lastQuality(QString::fromLatin1(stemQualityName(DefaultStemQuality)))
{
}




StemSplitController::~StemSplitController() = default;




StemSplitController* StemSplitController::instance()
{
	static StemSplitController* s_instance = nullptr;
	if (s_instance == nullptr)
	{
		QWidget* parent = nullptr;
		if (auto* app = GuiApplication::instance())
		{
			parent = app->mainWindow();
		}
		s_instance = new StemSplitController(parent);
	}
	return s_instance;
}




void StemSplitController::addSplitToStemsMenu(QMenu* menu, SampleClip* clip)
{
	if (menu == nullptr || clip == nullptr) { return; }
	QMenu* sub = menu->addMenu(tr("Split to stems"));
	// One item per quality stem.job_start accepts (Q050-062.1), in its order.
	// The item hands the NAME to the registered command, which validates it -
	// the menu has no quality logic of its own beyond the label.
	const std::pair<StemQuality, QString> items[] = {
		{StemQuality::Fast, tr("Fast (25% overlap)")},
		{StemQuality::Standard, tr("Standard (50% overlap)")},
		{StemQuality::High, tr("High quality (75% overlap, about 2x the time)")}};
	const QString last = instance()->lastQuality();
	auto* group = new QActionGroup(sub);
	QPointer<SampleClip> target(clip);
	for (const auto& [quality, text] : items)
	{
		const QString name = QString::fromLatin1(stemQualityName(quality));
		QAction* action = sub->addAction(text);
		action->setCheckable(true);
		action->setChecked(name == last);
		action->setData(name);
		group->addAction(action);
		QObject::connect(action, &QAction::triggered, sub, [target, name] {
			if (!target.isNull()) { instance()->splitClipToStems(target.data(), name); }
		});
	}
}




QString StemSplitController::qualityText(const QJsonObject& job)
{
	QString name = job.value(QStringLiteral("inference_quality")).toString();
	if (name.isEmpty()) { name = job.value(QStringLiteral("quality")).toString(); }
	StemQuality quality = DefaultStemQuality;
	if (!control::stemQualityFromName(name, &quality)) { return QString(); }
	return tr("%1 quality, %2% overlap")
		.arg(name)
		.arg(qRound(stemOverlapFraction(quality) * 100.0));
}




QString StemSplitController::progressLabel(const QJsonObject& job) const
{
	const QString quality = qualityText(job);
	if (job.value(QStringLiteral("fetching_model")).toBool(false))
	{
		return quality.isEmpty()
			? tr("Downloading the stem model, then separating (offline job)...")
			: tr("Downloading the stem model, then separating at %1 (offline job)...").arg(quality);
	}
	return quality.isEmpty()
		? tr("Separating stems (offline job)...")
		: tr("Separating stems at %1 (offline job)...").arg(quality);
}




QString StemSplitController::availabilityError()
{
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("stem.get_state"));
	if (!result.ok) { return result.errorMessage; }
	if (result.result.value(QStringLiteral("available")).toBool()) { return QString(); }
	return result.result.value(QStringLiteral("error")).toString();
}




bool StemSplitController::ensureBackendReady(bool* fetching)
{
	*fetching = false;
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("stem.get_state"));
	if (state.ok && state.result.value(QStringLiteral("available")).toBool()) { return true; }

	// Owner decision 14: an absent model that the job can fetch itself is not a
	// refusal - but a 166 MB download is the user's decision, not a side effect
	// of a click, so ask once.
	const bool fetchable = state.ok && state.result.value(QStringLiteral("fetch_on_first_use")).toBool();
	if (fetchable)
	{
		const ControlResult model = ControlRegistry::instance()->invoke(QStringLiteral("stem.model_get_state"));
		const QJsonObject spec = model.result.value(QStringLiteral("spec")).toObject();
		const QString name = spec.value(QStringLiteral("name")).toString(QStringLiteral("the stem model"));
		const qint64 sizeBytes = static_cast<qint64>(spec.value(QStringLiteral("size_bytes")).toDouble());
		const QString license = spec.value(QStringLiteral("license")).toString();
		const QString card = spec.value(QStringLiteral("model_card_url")).toString();
		const QString dir = model.result.value(QStringLiteral("dir")).toString();
		const auto answer = QMessageBox::question(m_parent, tr("Split to stems"),
			tr("Stem separation needs the %1 model (%2 MB, %3 licence), which is not on this "
				"computer yet.\n\nDownload it now from %4? It is verified by SHA-256 before use "
				"and kept in %5.")
				.arg(name).arg(sizeBytes / 1000000).arg(license, card, dir));
		*fetching = answer == QMessageBox::Yes;
		return *fetching;
	}

	QMessageBox::warning(m_parent, tr("Split to stems"),
		tr("Stem separation is not ready.\n\n%1")
			.arg(state.ok ? state.result.value(QStringLiteral("error")).toString()
				: state.errorMessage));
	return false;
}




void StemSplitController::splitClipToStems(SampleClip* clip, const QString& quality)
{
	if (clip == nullptr)
	{
		return;
	}
	if (m_jobId >= 0)
	{
		QMessageBox::information(m_parent, tr("Split to stems"),
			tr("A stem separation job is already running."));
		return;
	}
	ClipSnapshot snapshot;
	if (!captureClipSnapshot(clip, &snapshot))
	{
		return;
	}

	bool fetching = false;
	if (!ensureBackendReady(&fetching)) { return; }
	if (!targetStillAvailable(snapshot))
	{
		return;
	}
	QString source;
	QString error;
	if (!resolveSourceFile(snapshot, &source, &error))
	{
		QMessageBox::warning(m_parent, tr("Split to stems"), error);
		return;
	}
	startJob(snapshot, source, quality);
}




bool StemSplitController::captureClipSnapshot(SampleClip* clip, ClipSnapshot* snapshot) const
{
	Song* song = Engine::getSong();
	Track* track = clip->getTrack();
	if (song == nullptr || track == nullptr || song->findTrackById(track->id()) != track)
	{
		return false;
	}
	snapshot->song = song;
	snapshot->trackId = track->id();
	snapshot->position = clip->startPosition();
	snapshot->name = clip->name();
	snapshot->source = clip->sample().buffer();
	return true;
}




bool StemSplitController::targetStillAvailable(const ClipSnapshot& snapshot) const
{
	return !snapshot.song.isNull() && snapshot.song.data() == Engine::getSong()
		&& snapshot.song->findTrackById(snapshot.trackId) != nullptr;
}




bool StemSplitController::resolveSourceFile(const ClipSnapshot& snapshot, QString* path, QString* error)
{
	const auto buffer = snapshot.source;
	if (!buffer || buffer->empty())
	{
		*error = tr("This clip has no audio.");
		return false;
	}
	// The common case: the clip was loaded from a file, and the engine decodes
	// that same file (and resamples it to the model's rate on its own worker).
	const QString stored = buffer->audioFile();
	if (!stored.isEmpty())
	{
		bool pathError = false;
		const QString absolute = PathUtil::toAbsolute(stored, &pathError);
		if (!pathError && QFileInfo(absolute).isFile())
		{
			*path = absolute;
			return true;
		}
	}
	// An in-memory clip (recorded, pasted, generated) has no file. Write the
	// clip's own audio to a temporary WAV so the job still travels through the
	// same registered command; the file is removed when the controller resets.
	releaseTemp();
	m_temp = new QTemporaryFile(QDir(QDir::tempPath()).filePath(
		QStringLiteral("zene-stem-XXXXXX.wav")), this);
	if (!m_temp->open())
	{
		*error = tr("Could not create a temporary file for this clip.");
		releaseTemp();
		return false;
	}
	*path = m_temp->fileName();
	m_temp->close();
	if (!control::stemWriteWavFile(*path, *buffer, buffer->sampleRate(), error))
	{
		releaseTemp();
		*path = QString();
		return false;
	}
	return true;
}




bool StemSplitController::startJob(const ClipSnapshot& snapshot, const QString& source, const QString& quality)
{
	QJsonObject args{{QStringLiteral("source"), source}};
	if (!quality.isEmpty()) { args.insert(QStringLiteral("quality"), quality); }
	const ControlResult started = ControlRegistry::instance()->invoke(QStringLiteral("stem.job_start"), args);
	if (!started.ok)
	{
		releaseTemp();
		QMessageBox::warning(m_parent, tr("Split to stems"),
			tr("Stem separation could not start:\n\n%1").arg(started.errorMessage));
		return false;
	}

	m_jobId = started.result.value(QStringLiteral("job_id")).toInt();
	// The setting the command says it took, not the one asked for.
	const QString effective = started.result.value(QStringLiteral("quality")).toString();
	if (!effective.isEmpty()) { m_lastQuality = effective; }
	m_song = snapshot.song;
	m_trackId = snapshot.trackId;
	m_clipPosition = snapshot.position;
	m_clipName = snapshot.name;

	m_dialog = new QProgressDialog(progressLabel(started.result), tr("Cancel"), 0, 100, m_parent);
	m_dialog->setWindowTitle(tr("Split to stems"));
	m_dialog->setWindowModality(Qt::WindowModal);
	m_dialog->setMinimumDuration(0);
	m_dialog->setAutoClose(false);
	m_dialog->setAutoReset(false);
	connect(m_dialog, &QProgressDialog::canceled, this, [this] {
		if (m_jobId >= 0)
		{
			ControlRegistry::instance()->invoke(QStringLiteral("stem.job_cancel"),
				QJsonObject{{QStringLiteral("job_id"), m_jobId}});
		}
	});
	m_dialog->show();

	m_poll = new QTimer(this);
	m_poll->setInterval(PollIntervalMs);
	connect(m_poll, &QTimer::timeout, this, &StemSplitController::pollJob);
	m_poll->start();
	return true;
}




void StemSplitController::setStatusReaderForTest(StatusReader reader)
{
	s_statusReader = reader;
}




void StemSplitController::pollJob()
{
	if (m_jobId < 0)
	{
		stopPolling();
		return;
	}
	const ControlResult status = s_statusReader
		? s_statusReader(m_jobId)
		: ControlRegistry::instance()->invoke(
			QStringLiteral("stem.job_status"), QJsonObject{{QStringLiteral("job_id"), m_jobId}});
	if (!status.ok)
	{
		failJob(status.errorMessage);
		return;
	}
	const QJsonArray jobs = status.result.value(QStringLiteral("jobs")).toArray();
	if (jobs.isEmpty())
	{
		failJob(tr("The separation job disappeared."));
		return;
	}
	const QJsonObject job = jobs.first().toObject();
	if (m_dialog != nullptr)
	{
		m_dialog->setValue(qRound(job.value(QStringLiteral("progress")).toDouble(0.0) * 100.0));
		m_dialog->setLabelText(progressLabel(job));
	}
	const QString state = job.value(QStringLiteral("state")).toString();
	if (state == QLatin1String("completed")) { completeJob(job); }
	else if (state == QLatin1String("failed")) { failJob(job.value(QStringLiteral("error")).toString()); }
	else if (state == QLatin1String("cancelled")) { closeDialog(); resetJob(); }
}




void StemSplitController::completeJob(const QJsonObject& job)
{
	stopPolling();
	closeDialog();
	Song* song = m_song.data();
	if (song == nullptr || song != Engine::getSong() || song->findTrackById(m_trackId) == nullptr)
	{
		// The source track is gone while the job ran: drop the result, like the
		// old direct path did, rather than materialise tracks for a deleted clip.
		resetJob();
		return;
	}
	QJsonObject args;
	args.insert(QStringLiteral("job_id"), m_jobId);
	args.insert(QStringLiteral("position"), static_cast<double>(m_clipPosition.getTicks()));
	if (!m_clipName.isEmpty()) { args.insert(QStringLiteral("name"), m_clipName); }
	const ControlResult created = ControlRegistry::instance()->invoke(
		QStringLiteral("stem.tracks_create"), args);
	if (!created.ok)
	{
		QMessageBox::warning(m_parent, tr("Split to stems"),
			tr("Could not create stem tracks: %1").arg(created.errorMessage));
	}
	else
	{
		// Q050-062.2: name the setting the worker actually ran, and say where the
		// source is - stem.tracks_create adds tracks and never edits the clip.
		const QString quality = qualityText(job);
		QString text = quality.isEmpty()
			? tr("Created %1 stem tracks in %2 s.")
			: tr("Created %1 stem tracks at %3 in %2 s.");
		text = text.arg(created.result.value(QStringLiteral("track_count")).toInt())
			.arg(job.value(QStringLiteral("since_submit_seconds")).toDouble(0.0), 0, 'f', 1);
		if (!quality.isEmpty()) { text = text.arg(quality); }
		text += QLatin1Char('\n') + (m_clipName.isEmpty()
			? tr("The original clip is unchanged on its track.")
			: tr("The original clip \"%1\" is unchanged on its track.").arg(m_clipName));
		QMessageBox::information(m_parent, tr("Split to stems"), text);
	}
	resetJob();
}




void StemSplitController::failJob(const QString& error)
{
	stopPolling();
	closeDialog();
	QMessageBox::warning(m_parent, tr("Split to stems"),
		tr("Stem separation failed:\n\n%1").arg(error));
	resetJob();
}




void StemSplitController::stopPolling()
{
	if (m_poll != nullptr)
	{
		m_poll->stop();
		m_poll->deleteLater();
		m_poll = nullptr;
	}
}




void StemSplitController::closeDialog()
{
	if (m_dialog != nullptr)
	{
		m_dialog->close();
		m_dialog->deleteLater();
		m_dialog = nullptr;
	}
}




void StemSplitController::clearTarget()
{
	m_song.clear();
	m_trackId = -1;
	m_clipPosition = TimePos(0);
	m_clipName.clear();
}




void StemSplitController::resetJob()
{
	m_jobId = -1;
	clearTarget();
	releaseTemp();
}




void StemSplitController::releaseTemp()
{
	if (m_temp != nullptr)
	{
		m_temp->remove();
		delete m_temp;
		m_temp = nullptr;
	}
}

} // namespace lmms::gui
