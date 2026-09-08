/*
 * StemSplitController.h - GUI glue for the "Split to stems" action
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

#ifndef LMMS_GUI_STEM_SPLIT_CONTROLLER_H
#define LMMS_GUI_STEM_SPLIT_CONTROLLER_H

#include <QObject>
#include <QString>

#include "StemSeparation/StemJobManager.h"

class QProgressDialog;
class QWidget;

namespace lmms
{

class SampleClip;

namespace gui
{

// Owns the job manager and the progress dialog for the "Split to stems" clip
// action. The controller never touches audio: the heavy work happens on
// StemJobManager's worker thread, results are materialised as tracks on the GUI
// thread once the job has finished.
class LMMS_EXPORT StemSplitController : public QObject
{
	Q_OBJECT
public:
	explicit StemSplitController(QWidget* parent = nullptr);
	~StemSplitController() override;

	// Process-wide instance, created on first use and parented to the main
	// window when one exists.
	static StemSplitController* instance();

	// Empty string when the feature is ready to run; otherwise a user-facing
	// explanation (missing interpreter, missing model, ...).
	static QString availabilityError();

	// Asynchronous entry point of the clip context-menu action.
	void splitClipToStems(SampleClip* clip);

private slots:
	void handleProgress(int jobId, float fraction);
	void handleFinished(int jobId, double elapsedSeconds);
	void handleCancelled(int jobId);
	void handleFailed(int jobId, const QString& error);

private:
	void closeDialog();

	QWidget* m_parent = nullptr;
	StemJobManager m_manager;
	QProgressDialog* m_dialog = nullptr;
	SampleClip* m_clip = nullptr;
	int m_jobId = -1;
};

} // namespace gui
} // namespace lmms

#endif // LMMS_GUI_STEM_SPLIT_CONTROLLER_H
