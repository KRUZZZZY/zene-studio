/*
 * BatchFreeze.h - Q050-327: freeze several tracks as ONE background task that renders them concurrently
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

#ifndef LMMS_BATCH_FREEZE_H
#define LMMS_BATCH_FREEZE_H

#include <vector>

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "BounceInPlace.h"
#include "TaskCenter.h"
#include "TempoMap.h"
#include "lmms_export.h"

class QProcess;
class QTimer;

namespace lmms
{

class Track;

//! One track of a batch: its identity as captured at the start, and its own child render.
struct LMMS_EXPORT BatchFreezeTarget
{
	enum class Phase { Waiting, Rendering, Finalizing, Rendered, Failed };

	int trackId = -1;
	QByteArray fingerprint;  //!< sha256 of the track's XML when the batch started (stale-target check)
	BounceInPlace::PreparedRender prepared;
	BounceInPlace::Result rendered;
	Phase phase = Phase::Waiting;
	QProcess* process = nullptr;
	QTimer* timer = nullptr;
};

//! One batch: every target, the session facts it was started against, and how it ended.
struct LMMS_EXPORT BatchFreezeJob
{
	enum class State { Running, Done, Failed, Cancelled };

	int id = 0;
	State state = State::Running;
	QString error;
	QJsonObject args;
	quint64 documentIdentity = 0;
	int tempo = 0;
	TempoMap tempoMap;
	std::vector<BatchFreezeTarget> targets;
	TaskToken taskToken;
	int inFlight = 0;      //!< child processes and finalising workers not yet reported back
	int peakParallel = 0;  //!< the most child renders that were running at one time
	QJsonObject report;
	bool ended() const { return state != State::Running; }
};

/*! Q050-327. The render queue runs one job at a time and each job is one task and one undo step, so
 *  freezing N tracks through it is N serial renders, N tasks and N steps. A batch instead starts one
 *  child render per selected track (the same shipped CLI render BounceInPlace prepares) up to
 *  parallelLimit() at once, reports them as ONE TaskCenter task with aggregate progress and Cancel,
 *  and installs NOTHING until every render has succeeded: then, on the UI thread, every target is
 *  re-checked (same document, same track XML, same tempo), every take is installed and every track
 *  frozen inside one ProjectJournal edit merged to a single step, so one control.undo takes the
 *  whole batch off. A cancel, a failed render or a changed target ends the batch with every track as
 *  it was and every staged file removed; a previous file at an output path is kept.
 *
 *  Threads: everything here runs on the UI thread (QProcess signals); the trim/hash of a finished
 *  render runs on a QThreadPool worker and reports back with a queued call. Nothing touches the audio
 *  thread except Track::freezeTo's existing take hand-over. Session state only: batches are not saved. */
class LMMS_EXPORT BatchFreeze : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxBatches = 64;

	static BatchFreeze& instance();
	static QString stateName(BatchFreezeJob::State state);
	static QString phaseName(BatchFreezeTarget::Phase phase);
	//! How many child renders one batch runs at once: half the cores, at least 2 and at most 8.
	static int parallelLimit();
	//! sha256 of @a track's own XML (the stale-target check); empty when it cannot be captured.
	static QByteArray trackFingerprint(const Track* track);

	//! Start @a job (prepared renders and fingerprints filled in); its id, or -1 (its renders discarded) when
	//! MaxBatches are still running.
	int start(BatchFreezeJob job);
	//! Stop batch @a id: every child render is killed and nothing is applied. False when it has ended.
	bool cancel(int id);
	const BatchFreezeJob* job(int id) const;
	//! The batch's JSON as task reports and freeze.tracks describe it.
	static QJsonObject toJson(const BatchFreezeJob& job);

private:
	BatchFreeze() = default;
	BatchFreezeJob* find(int id);
	void launchNext(BatchFreezeJob& job);
	void launch(BatchFreezeJob& job, std::size_t index);
	void processEnded(int id, std::size_t index, bool ok, const QString& error);
	void finalized(int id, std::size_t index, const BounceInPlace::Result& rendered, const QString& error);
	void settle(BatchFreezeJob& job);
	bool applyAll(BatchFreezeJob& job, QString* error);
	void fail(BatchFreezeJob& job, BatchFreezeJob::State state, const QString& error);
	void stopProcesses(BatchFreezeJob& job);
	void reportProgress(const BatchFreezeJob& job);
	void forgetEnded();

	std::vector<BatchFreezeJob> m_jobs;
	int m_nextId = 1;
};

} // namespace lmms

#endif // LMMS_BATCH_FREEZE_H
