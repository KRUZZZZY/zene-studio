/*
 * StemSplitController.h - GUI glue for the "Split to stems" action
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

#ifndef LMMS_GUI_STEM_SPLIT_CONTROLLER_H
#define LMMS_GUI_STEM_SPLIT_CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "TimePos.h"

class QJsonObject;
class QMenu;
class QProgressDialog;
class QTemporaryFile;
class QTimer;
class QWidget;

namespace lmms
{

class SampleBuffer;
class SampleClip;
class Song;
struct ControlResult;

namespace gui
{

/*! The "Split to stems" clip action, driven through the SAME registered
 *  `stem.*` commands the agent surface calls - `stem.job_start` to queue the
 *  offline job, `stem.job_status` to poll it, `stem.job_cancel` for the
 *  dialog's Cancel, and `stem.tracks_create` to materialise the four tracks as
 *  ONE undo step. This class owns no engine: it is a thin client of
 *  ControlRegistry, so a click and an agent command take the identical path
 *  (SPEC A11, "every user-facing action is one registered command").
 *
 *  It never touches audio: the separation runs on StemJobManager's worker
 *  thread inside the control surface, progress is read by polling, and the
 *  tracks are created on the GUI thread by the command.
 */
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

	/*! Asynchronous entry point of the clip context-menu action. \a quality is
	 *  a `stem.job_start` quality name ("fast", "standard", "high"); empty
	 *  omits it, so the command applies its own default (Q050-062.2). */
	void splitClipToStems(SampleClip* clip, const QString& quality = QString());

	/*! Q050-062.2: the clip menu's "Split to stems" submenu - one checkable
	 *  item per quality `stem.job_start` accepts, the last one used checked.
	 *  Picking an item splits \a clip at that quality through instance(). */
	static void addSplitToStemsMenu(QMenu* menu, SampleClip* clip);

	//! The quality the menu last started a job at (the command's default
	//! before the first one).
	QString lastQuality() const { return m_lastQuality; }

	/*! How the job UI names a job's effective setting, from its `stem.*` job
	 *  object: "high quality, 75% overlap". Uses `inference_quality` once the
	 *  worker has run, else `quality`; empty when the job carries neither. */
	static QString qualityText(const QJsonObject& job);

	//! How pollJob reads one job's `stem.job_status` result. Empty means the
	//! registry; the pipeline test replaces it to script the states a poll sees.
	using StatusReader = std::function<ControlResult(int jobId)>;
	static void setStatusReaderForTest(StatusReader reader);

private slots:
	void pollJob();

private:
	struct ClipSnapshot
	{
		QPointer<Song> song;
		int trackId = -1;
		TimePos position;
		QString name;
		std::shared_ptr<const SampleBuffer> source;
	};

	/*! Owner decision 14: true when a job may be submitted - the engine is
	 *  ready, or only the model is missing and the user agreed to fetch it
	 *  (\a fetching set). False after telling the user why not. */
	bool ensureBackendReady(bool* fetching);
	bool captureClipSnapshot(SampleClip* clip, ClipSnapshot* snapshot) const;
	bool targetStillAvailable(const ClipSnapshot& snapshot) const;
	//! The absolute file the engine should decode: the clip's own audio file
	//! when there is one, else a temporary WAV of the clip's buffer.
	bool resolveSourceFile(const ClipSnapshot& snapshot, QString* path, QString* error);
	bool startJob(const ClipSnapshot& snapshot, const QString& source, const QString& quality);
	//! The progress label for \a job's phase and effective quality.
	QString progressLabel(const QJsonObject& job) const;
	void completeJob(const QJsonObject& job);
	void failJob(const QString& error);

	void stopPolling();
	void closeDialog();
	void clearTarget();
	void resetJob();
	void releaseTemp();

	QWidget* m_parent = nullptr;
	QProgressDialog* m_dialog = nullptr;
	QTimer* m_poll = nullptr;
	QTemporaryFile* m_temp = nullptr;
	QPointer<Song> m_song;
	int m_trackId = -1;
	TimePos m_clipPosition;
	QString m_clipName;
	QString m_lastQuality;
	int m_jobId = -1;
};

} // namespace gui
} // namespace lmms

#endif // LMMS_GUI_STEM_SPLIT_CONTROLLER_H
