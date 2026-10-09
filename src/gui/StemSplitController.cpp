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
#include "SampleBufferResample.h"

#include <memory>

#include <QMessageBox>
#include <QProgressDialog>

#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "Song.h"
#include "StemSeparation/ExternalProcessStemSeparator.h"
#include "StemSeparation/StemModelStore.h"
#include "StemSeparation/StemTrackBuilder.h"
#include "Track.h"

namespace lmms::gui
{

StemSplitController::StemSplitController(QWidget* parent) :
	QObject(parent),
	m_parent(parent)
{
	// The external-process backend is always compiled in with the feature; it
	// isolates the model from the LMMS process. The optional in-process ONNX
	// Runtime backend can be swapped in here when available.
	m_manager.setSeparator(std::make_unique<ExternalProcessStemSeparator>());
	// Owner decision 14: a first split fetches the pinned model on the job's
	// worker (after the user agreed to it, below), never on the GUI thread.
	m_manager.setModelProvisioner(&StemModelStore::fetchDefaultModelIfMissing);

	connect(&m_manager, &StemJobManager::jobProgress,
		this, &StemSplitController::handleProgress);
	connect(&m_manager, &StemJobManager::jobFinished,
		this, &StemSplitController::handleFinished);
	connect(&m_manager, &StemJobManager::jobCancelled,
		this, &StemSplitController::handleCancelled);
	connect(&m_manager, &StemJobManager::jobFailed,
		this, &StemSplitController::handleFailed);
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




QString StemSplitController::availabilityError()
{
	QString error;
	if (!ExternalProcessStemSeparator().isAvailable(&error))
	{
		return error;
	}
	return QString();
}




bool StemSplitController::ensureBackendReady(bool* fetching)
{
	QString error;
	if (ExternalProcessStemSeparator().isAvailable(&error)) { return true; }
	if (StemModelStore::canFetchDefaultModel()
		&& !ExternalProcessStemSeparator::locatePython().isEmpty()
		&& !ExternalProcessStemSeparator::locateCli().isEmpty())
	{
		// Only the model is missing, and it is fetchable: ask once, because a
		// 166 MB download is the user's decision, not a side effect of a click.
		const auto spec = StemModelStore::defaultModelSpec();
		const auto answer = QMessageBox::question(m_parent, tr("Split to stems"),
			tr("Stem separation needs the %1 model (%2 MB, %3 licence), which is not on this "
				"computer yet.\n\nDownload it now from %4? It is verified by SHA-256 before use "
				"and kept in %5.")
				.arg(spec.name).arg(spec.sizeBytes / 1000000).arg(spec.license, spec.modelCardUrl,
					StemModelStore::defaultModelDir()));
		*fetching = answer == QMessageBox::Yes;
		return *fetching;
	}
	const auto spec = StemModelStore::defaultModelSpec();
	QMessageBox::warning(m_parent, tr("Split to stems"),
		tr("Stem separation is not ready.\n\n%1\n\n"
			"Model: %2 (optional download, never bundled)\nDownload page: %3")
			.arg(error, spec.name, spec.modelCardUrl));
	return false;
}




void StemSplitController::splitClipToStems(SampleClip* clip)
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
	if (!snapshot.source || snapshot.source->empty())
	{
		QMessageBox::warning(m_parent, tr("Split to stems"), tr("This clip has no audio."));
		return;
	}
	startJob(snapshot, fetching);
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




void StemSplitController::startJob(const ClipSnapshot& snapshot, bool fetching)
{
	// E27: a clip at another rate is resampled to the model's 44.1 kHz first, not refused.
	const auto buffer = resampleBuffer(snapshot.source, StemModelSampleRate);

	m_song = snapshot.song;
	m_trackId = snapshot.trackId;
	m_clipPosition = snapshot.position;
	m_clipName = snapshot.name;
	m_jobId = m_manager.submit(buffer, buffer->sampleRate(), HTDemucsSegmentFrames);

	m_dialog = new QProgressDialog(fetching
			? tr("Downloading the stem model, then separating (offline job)...")
			: tr("Separating stems (offline job)..."),
		tr("Cancel"), 0, 100, m_parent);
	m_dialog->setWindowTitle(tr("Split to stems"));
	m_dialog->setWindowModality(Qt::WindowModal);
	m_dialog->setMinimumDuration(0);
	m_dialog->setAutoClose(false);
	m_dialog->setAutoReset(false);
	connect(m_dialog, &QProgressDialog::canceled, this, [this] {
		if (m_jobId >= 0)
		{
			m_manager.cancel(m_jobId);
		}
	});
	m_dialog->show();
}




void StemSplitController::handleProgress(int jobId, float fraction)
{
	if (jobId != m_jobId || m_dialog == nullptr)
	{
		return;
	}
	m_dialog->setValue(qRound(fraction * 100.0f));
}




void StemSplitController::handleFinished(int jobId, double elapsedSeconds)
{
	if (jobId != m_jobId)
	{
		return;
	}
	closeDialog();
	Song* song = m_song.data();
	if (song == nullptr || song != Engine::getSong() || song->findTrackById(m_trackId) == nullptr)
	{
		m_jobId = -1;
		clearTarget();
		return;
	}

	QString error;
	const auto stems = m_manager.result(jobId);
	const auto tracks = StemTrackBuilder::createStemTracks(song, stems,
		m_clipPosition, m_clipName, &error);

	if (tracks.isEmpty())
	{
		QMessageBox::warning(m_parent, tr("Split to stems"),
			tr("Could not create stem tracks: %1").arg(error));
	}
	else
	{
		QMessageBox::information(m_parent, tr("Split to stems"),
			tr("Created %1 stem tracks in %2 s.").arg(tracks.size()).arg(elapsedSeconds, 0, 'f', 1));
	}

	m_jobId = -1;
	clearTarget();
}




void StemSplitController::handleCancelled(int jobId)
{
	if (jobId != m_jobId)
	{
		return;
	}
	closeDialog();
	m_jobId = -1;
	clearTarget();
}




void StemSplitController::handleFailed(int jobId, const QString& error)
{
	if (jobId != m_jobId)
	{
		return;
	}
	closeDialog();
	QMessageBox::warning(m_parent, tr("Split to stems"),
		tr("Stem separation failed:\n\n%1").arg(error));
	m_jobId = -1;
	clearTarget();
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

} // namespace lmms::gui
