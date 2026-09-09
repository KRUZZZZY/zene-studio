/*
 * StemSplitController.cpp - GUI glue for the "Split to stems" action
 *
 * Copyright (c) 2026 LMMS Developers
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
	if (!ExternalProcessStemSeparator::isAvailable(&error))
	{
		return error;
	}
	return QString();
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

	QString error;
	if (!ExternalProcessStemSeparator::isAvailable(&error))
	{
		const auto spec = StemModelStore::defaultModelSpec();
		QMessageBox::warning(m_parent, tr("Split to stems"),
			tr("Stem separation is not ready.\n\n%1\n\n"
				"Model: %2 (optional download, never bundled)\nDownload page: %3")
				.arg(error, spec.name, spec.modelCardUrl));
		return;
	}

	const auto buffer = clip->sample().buffer();
	if (!buffer || buffer->empty())
	{
		QMessageBox::warning(m_parent, tr("Split to stems"), tr("This clip has no audio."));
		return;
	}
	if (buffer->sampleRate() != StemModelSampleRate)
	{
		QMessageBox::warning(m_parent, tr("Split to stems"),
			tr("Stem separation needs %1 Hz audio; this clip is %2 Hz. "
				"Resampling is not implemented yet.")
				.arg(StemModelSampleRate).arg(buffer->sampleRate()));
		return;
	}

	m_clip = clip;
	m_jobId = m_manager.submit(buffer, buffer->sampleRate(), HTDemucsSegmentFrames);

	m_dialog = new QProgressDialog(tr("Separating stems (offline job)..."),
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

	QString error;
	const auto stems = m_manager.result(jobId);
	const auto tracks = StemTrackBuilder::createStemTracks(Engine::getSong(), stems,
		m_clip->startPosition(), m_clip->name(), &error);

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
	m_clip = nullptr;
}




void StemSplitController::handleCancelled(int jobId)
{
	if (jobId != m_jobId)
	{
		return;
	}
	closeDialog();
	m_jobId = -1;
	m_clip = nullptr;
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
	m_clip = nullptr;
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

} // namespace lmms::gui
