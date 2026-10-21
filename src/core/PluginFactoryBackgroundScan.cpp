/*
 * PluginFactoryBackgroundScan.cpp - the background plug-in rescan (Q050-090).
 *
 * plugin.scan_start marks the run active, shows it in the task area and
 * returns BEFORE touching a directory or a library. The planning (candidate
 * files, the quarantine list, the per-file plan) and the dependency pre-load
 * then run as scheduled cancellable chunks: the first turn plans, each
 * following pre-load turn loads ONE library, and each scan turn scans ONE
 * file - so the window stays usable throughout, plugin.list keeps serving the
 * PREVIOUS catalogue until the run commits (the same promise the library
 * index scan (BrowserIndex) keeps), and plugin.scan_cancel stops the run at
 * its next turn boundary. plugin.scan_status reads the progress and the
 * per-file outcomes so far. The task area (TaskCenter, task.list) shows the
 * run with its progress and Cancel, like every other long job.
 *
 * Main-thread confinement is the whole threading argument: control handlers
 * run on the UI thread (ControlRegistry::runOnUiThread) and the chunks are
 * QTimer turns on the same thread, so no member here needs a lock and no
 * worker ever loads a library off the thread the synchronous scan uses.
 * Nothing here touches the audio thread.
 *
 * What a run shares with the synchronous scan, and what it does not. The
 * per-file decision, load and record logic is the same code (the detail
 * header both translation units include, through the ScanSink the run stages
 * into), so the two runs cannot churn the cache against each other. Three
 * things differ on purpose, each documented where it happens:
 * - the quarantine list is snapshotted when the run PLANS (its first turn):
 *   an edit made before that takes effect, one made later waits for the NEXT
 *   run (a synchronous scan re-reads the file);
 * - records are stored live as they are taken, so the Modules list shows them
 *   mid-run; a cancelled run's measurements stay cached for the next run;
 * - plugin.rescan is refused while a run is active: both would commit a
 *   catalogue and only the last commit would survive.
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

#include "PluginFactory.h"

#include <algorithm>

#include <QDebug>
#include <QTimer>

#include "ControlDeviceSupport.h"
#include "PluginFactoryScanDetail.h"
#include "TaskCenter.h"

namespace lmms
{

namespace
{

//! The delay between two files, bounded so a test can pace a run without a
//! misclick stalling the window.
int clampPaceMs(int paceMs)
{
	return std::clamp(paceMs, 0, 1000);
}

//! 0..1 for the task area's progress bar, or -1 (the TaskCenter's "the job
//! cannot say") while the run has not planned its files yet: total_files is 0
//! until the first turn runs, and 1.0 there would show a run that has not
//! started as complete. A run that plans no files ends on its next turn, and
//! TaskCenter::finish marks a done task 1.0.
double backgroundScanFraction(int doneFiles, int totalFiles)
{
	if (totalFiles <= 0) { return -1.0; }
	return std::min(1.0, double(doneFiles) / double(totalFiles));
}

} // namespace


bool PluginFactory::startBackgroundScan(int paceMs, QString* error)
{
	if (m_background.active)
	{
		*error = QStringLiteral("a background scan is already running (%1 of %2 files) - "
			"follow it with plugin.scan_status or stop it with plugin.scan_cancel")
			.arg(m_background.doneFiles).arg(m_background.totalFiles);
		return false;
	}

	// Active FIRST, work LATER: nothing below touches a directory or a
	// library - the planning and every load run as scheduled chunks AFTER
	// this returns (see prepareBackgroundScan), so the task, the active
	// status and plugin.scan_cancel exist before the first potentially
	// slow plug-in load. total_files is 0 until the first turn plans the
	// run; the last COMPLETED run's stats stand until this one commits.
	m_backgroundInfos.clear();
	m_backgroundDescriptors.clear();
	m_backgroundByExt.clear();
	m_backgroundErrors.clear();
	m_backgroundRemaining.clear();
	m_backgroundPreloadRemaining.clear();
	m_backgroundStore = std::make_unique<CachedDescriptorStore>();
	m_backgroundPaceMs = clampPaceMs(paceMs);
	m_backgroundPrepared = false;

	m_background = BackgroundScanStatus{};
	m_background.active = true;
	m_background.outcome = QStringLiteral("running");
	// What plugin.list serves: the catalogue built from the PREVIOUS run's
	// descriptors, which this run does not touch until it commits.
	m_background.catalogueCount = controlDeviceCatalogue().size();

	m_backgroundTaskToken = TaskCenter::instance().begin(this, ++m_backgroundTaskLocal,
		{QStringLiteral("plugin_scan"), QStringLiteral("Rescan plug-ins"),
			[this] { QString ignored; requestBackgroundScanCancel(&ignored); },
			[this] {
				return backgroundScanFraction(m_background.doneFiles, m_background.totalFiles);
			},
			false});
	m_background.taskId = TaskCenter::instance().idOf(this, m_backgroundTaskLocal);

	scheduleBackgroundScanStep();
	return true;
}


bool PluginFactory::requestBackgroundScanCancel(QString* error)
{
	if (!m_background.active)
	{
		*error = QStringLiteral("no background scan is running - "
			"plugin.scan_start begins one");
		return false;
	}
	m_background.cancelRequested = true;
	return true;
}


void PluginFactory::stepBackgroundScan()
{
	if (s_instance.get() != this || !m_background.active) { return; }
	if (m_background.cancelRequested)
	{
		endBackgroundScan(true);
		return;
	}
	if (!m_backgroundPrepared)
	{
		prepareBackgroundScan();
	}
	else if (!m_backgroundPreloadRemaining.isEmpty())
	{
		preloadBackgroundLibrary();
	}
	else if (!m_backgroundRemaining.isEmpty())
	{
		scanNextBackgroundFile();
	}
	else
	{
		endBackgroundScan(false);
		return;
	}
	// A finished run deactivates itself (see scanNextBackgroundFile); only a
	// run that still has turns left schedules the next one.
	if (m_background.active) { scheduleBackgroundScanStep(); }
}


//! The run's first turn: list the candidates, drop the quarantine list and
//! count the per-file plan exactly as the synchronous run does them - the
//! counts land in the staging stats and the known-bad errors in the staging
//! errors - queuing the libraries to pre-load and the files to scan. No
//! dlopen: every load waits for its own turn (see preloadBackgroundLibrary),
//! so this turn stays cheap and a cancel requested before it still stops the
//! run with nothing done. The live stats still describe the last COMPLETED
//! run until this one commits.
void PluginFactory::prepareBackgroundScan()
{
	QSet<QFileInfo> files = candidatePluginFiles();
	m_backgroundStats = ScanStats();
	m_backgroundStats.candidateFiles = files.size();
	QList<QFileInfo> remaining;
	for (const QFileInfo& file : files)
	{
		const QString path = file.absoluteFilePath();
		if (m_scanCache.isQuarantined(path))
		{
			m_backgroundStats.quarantined++;
			m_backgroundStats.quarantinedPaths.append(path);
			m_backgroundStats.quarantinedReasons.append(m_scanCache.quarantineReason(path));
			continue;
		}
		remaining.append(file);
	}
	std::sort(remaining.begin(), remaining.end(),
		[](const QFileInfo& left, const QFileInfo& right) {
			return left.absoluteFilePath() < right.absoluteFilePath();
		});

	QHash<QString, ScanPlan> plans;
	QSet<QFileInfo> toLoad;
	m_backgroundErrors.clear();
	planPluginScan(QSet<QFileInfo>(remaining.begin(), remaining.end()), m_scanCache,
		plans, toLoad, m_backgroundStats, m_backgroundErrors);
	m_backgroundPreloadRemaining = QList<QFileInfo>(toLoad.begin(), toLoad.end());
	std::sort(m_backgroundPreloadRemaining.begin(), m_backgroundPreloadRemaining.end(),
		[](const QFileInfo& left, const QFileInfo& right) {
			return left.absoluteFilePath() < right.absoluteFilePath();
		});
	m_backgroundRemaining = remaining;
	m_background.totalFiles = remaining.size();
	m_backgroundPrepared = true;
}


//! One library of the dependency pre-load: exactly one dlopen (see
//! preloadPluginLibraries - zynaddsubfx's helper is loaded before any
//! resolve needs it), then the next turn. One load bounds the turn, so a
//! cancel between two loads stops the run before the next one.
void PluginFactory::preloadBackgroundLibrary()
{
	const QFileInfo file = m_backgroundPreloadRemaining.takeFirst();
	QLibrary(file.absoluteFilePath()).load();
	m_background.currentFile = file.absoluteFilePath();
	TaskCenter::instance().progress(m_backgroundTaskToken,
		backgroundScanFraction(m_background.doneFiles, m_background.totalFiles),
		file.fileName());
}


//! One file of the active run, then the next turn - or the commit when the
//! queue is empty.
void PluginFactory::scanNextBackgroundFile()
{
	const QFileInfo file = m_backgroundRemaining.takeFirst();
	scanBackgroundFile(file);
	m_background.doneFiles++;
	TaskCenter::instance().progress(m_backgroundTaskToken,
		backgroundScanFraction(m_background.doneFiles, m_background.totalFiles),
		file.fileName());

	if (m_backgroundRemaining.isEmpty())
	{
		endBackgroundScan(false);
	}
}


//! The run's next turn after @a paceMs, through the TaskCenter (a QObject
//! that outlives the factory) so no timer of its own is needed.
void PluginFactory::scheduleBackgroundScanStep()
{
	QTimer::singleShot(m_backgroundPaceMs, &TaskCenter::instance(),
		[this] { stepBackgroundScan(); });
}


//! One file of the active run: the synchronous loop's decision table, pointed
//! at the staging sink. The stats were counted when the run was planned, so
//! this only appends and reports.
void PluginFactory::scanBackgroundFile(const QFileInfo& file)
{
	ScanSink sink = backgroundScanSink();
	const QString path = file.absoluteFilePath();
	m_background.currentFile = path;

	BackgroundScanFile entry;
	entry.path = path;
	const PluginScanRecord* record = m_scanCache.lookup(file);
	switch (planForCandidate(record))
	{
	case ScanPlan::Serve:
		appendCacheServedPlugin(*record, file, sink);
		entry.status = QStringLiteral("served-from-cache");
		entry.name = record->name;
		break;
	case ScanPlan::Skip:
		entry.status = record != nullptr
				&& record->status == PluginScanRecord::Status::LoadFailed
			? QStringLiteral("load-failed")
			: QStringLiteral("not-a-plugin");
		if (record != nullptr) { entry.error = record->error; }
		break;
	case ScanPlan::Load:
	{
		const PluginScanRecord::Status stored = scanOnePlugin(file, sink);
		entry.status = stored == PluginScanRecord::Status::HasDescriptor
			? QStringLiteral("scanned")
			: stored == PluginScanRecord::Status::LoadFailed
				? QStringLiteral("load-failed")
				: QStringLiteral("not-a-plugin");
		if (const PluginScanRecord* remembered = m_scanCache.record(path))
		{
			entry.name = remembered->name;
			entry.error = remembered->error;
		}
		break;
	}
	}
	m_background.files.append(entry);
}


//! Throw the staging away: the libraries unload, the rebuilt descriptors go
//! with the discarded store, and the live catalogue never moved.
void PluginFactory::discardBackgroundStaging()
{
	m_backgroundInfos.clear();
	m_backgroundDescriptors.clear();
	m_backgroundByExt.clear();
	m_backgroundErrors.clear();
	m_backgroundRemaining.clear();
	m_backgroundPreloadRemaining.clear();
	m_backgroundPrepared = false;
	m_backgroundStore = std::make_unique<CachedDescriptorStore>();
}


void PluginFactory::endBackgroundScan(bool cancelled)
{
	if (!m_background.active) { return; }
	const int done = m_background.doneFiles;
	const int total = m_background.totalFiles;

	if (cancelled)
	{
		discardBackgroundStaging();
		m_background.active = false;
		m_background.outcome = QStringLiteral("cancelled");
		m_background.currentFile.clear();
		// Unchanged: the previous catalogue is still what plugin.list serves.
		m_background.report = QStringLiteral("cancelled after %1 of %2 file(s) - "
			"the previous catalogue (%3 device(s)) is kept")
			.arg(done).arg(total).arg(m_background.catalogueCount);
		TaskCenter::instance().finish(m_backgroundTaskToken,
			TaskState::Cancelled, m_background.report);
		return;
	}

	m_pluginInfos = m_backgroundInfos;
	m_descriptors = m_backgroundDescriptors;
	m_pluginByExt = m_backgroundByExt;
	for (auto it = m_backgroundErrors.constBegin(); it != m_backgroundErrors.constEnd(); ++it)
	{
		m_errors.insert(it.key(), it.value());
	}
	m_retiredCachedDescriptors.push_back(std::move(m_cachedDescriptors));
	m_cachedDescriptors = std::move(m_backgroundStore);
	discardBackgroundStaging();
	m_scanStats = m_backgroundStats;
	m_scanStats.descriptors = m_pluginInfos.size();

	m_background.active = false;
	m_background.outcome = QStringLiteral("done");
	m_background.currentFile.clear();
	m_background.catalogueCount = controlDeviceCatalogue().size();
	m_background.report = backgroundScanReport();

	if (m_scanCache.isPersistent() && m_scanCache.isDirty())
	{
		m_scanCache.save();
	}

	TaskCenter::instance().finish(m_backgroundTaskToken,
		TaskState::Done, m_background.report);
	qInfo().noquote() << scanReport();
}


QString PluginFactory::backgroundScanReport() const
{
	return QStringLiteral("plugin-scan (background): %1 file(s), %2 served from cache, "
			"%3 known-bad skipped, %4 scanned, %5 device(s)")
		.arg(m_backgroundStats.candidateFiles)
		.arg(m_backgroundStats.servedFromCache)
		.arg(m_backgroundStats.negativeFromCache)
		.arg(m_backgroundStats.scanned)
		.arg(m_background.catalogueCount);
}

} // namespace lmms
