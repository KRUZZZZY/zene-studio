/*
 * ControlCommandsPluginScanBackground.cpp - the BACKGROUND half of the
 *                                           plugin.* scan surface (Q050-090).
 *
 * plugin.scan_start / plugin.scan_status / plugin.scan_cancel. The read half
 * (ControlCommandsPluginScan.cpp) shows what the last scan remembered and the
 * edit half (ControlCommandsPluginScanEdit.cpp) quarantines files and runs the
 * SYNCHRONOUS rescan; this half runs the same scan without blocking the
 * window. The engine side is PluginFactoryBackgroundScan.cpp - one file per
 * event-loop turn, plugin.list serving the previous catalogue until the run
 * commits, the task area (task.list) showing the run with its Cancel.
 *
 * A16, and why scan_start is `irreversible` while scan_cancel is not mutating
 * at all. A run that finishes re-MEASURES the files on disk and refreshes the
 * cache, and no command puts a file's PREVIOUS fingerprint record back once
 * the run has replaced it - the same reason plugin.rescan is irreversible,
 * with the same fallback (restore the library and scan again). scan_cancel
 * only STOPS a run: like task.cancel it touches session state, never the
 * project, so it records nothing and undoes nothing.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsPluginScanShared.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "PluginFactory.h"

namespace lmms
{

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

const QString ScanStartName = QStringLiteral("plugin.scan_start");
const QString ScanStatusName = QStringLiteral("plugin.scan_status");
const QString ScanCancelName = QStringLiteral("plugin.scan_cancel");

//! The most delay between two files a caller may ask for: enough to watch a
//! run and to cancel one on purpose, never a stall of the window.
constexpr int MaxPaceMs = 1000;

//! The run's progress as scan_status reports it: 0 before the first run, the
//! fraction while running or cancelled mid-run, 1 when there was nothing to
//! do or the run finished.
double scanProgress(const PluginFactory::BackgroundScanStatus& status)
{
	if (status.totalFiles <= 0)
	{
		return status.outcome == QLatin1String("done") ? 1.0 : 0.0;
	}
	return std::min(1.0, double(status.doneFiles) / double(status.totalFiles));
}

//! One finished file: its path, what this run decided about it and what the
//! scan resolved. The status words are the cache's own ("not-a-plugin",
//! "load-failed") plus what only a run can say ("served-from-cache" - the
//! library stayed closed - and "scanned" - it was loaded now).
QJsonObject scanFileJson(const PluginFactory::BackgroundScanFile& file)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), file.path);
	out.insert(QStringLiteral("status"), file.status);
	out.insert(QStringLiteral("name"), file.name);
	out.insert(QStringLiteral("error"), file.error);
	return out;
}

//! The whole run as scan_status reports it, with the proof the window kept
//! working: catalogue_count is what plugin.list serves while the run is on.
QJsonObject scanStatusJson(const PluginFactory::BackgroundScanStatus& status)
{
	QJsonObject out;
	out.insert(QStringLiteral("active"), status.active);
	out.insert(QStringLiteral("cancel_requested"), status.cancelRequested);
	out.insert(QStringLiteral("outcome"), status.outcome);
	out.insert(QStringLiteral("total_files"), status.totalFiles);
	out.insert(QStringLiteral("done_files"), status.doneFiles);
	out.insert(QStringLiteral("progress"), scanProgress(status));
	out.insert(QStringLiteral("current_file"), status.currentFile);
	QJsonArray files;
	for (const PluginFactory::BackgroundScanFile& file : status.files)
	{
		files.append(scanFileJson(file));
	}
	out.insert(QStringLiteral("files"), files);
	out.insert(QStringLiteral("task_id"), status.taskId);
	out.insert(QStringLiteral("catalogue_count"), status.catalogueCount);
	out.insert(QStringLiteral("report"), status.report);
	return out;
}

ControlResult handleScanStart(const QJsonObject& args)
{
	int paceMs = 0;
	if (args.contains(QStringLiteral("pace_ms")))
	{
		const QJsonValue pace = args.value(QStringLiteral("pace_ms"));
		if (!pace.isDouble())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 needs 'pace_ms' as a whole number of milliseconds, 0..%2 - "
					"the delay between two scanned files, so a progress display (or a test) can "
					"watch the run. Got %3")
					.arg(ScanStartName).arg(MaxPaceMs).arg(pace.toString()));
		}
		paceMs = pace.toInt(-1);
		if (paceMs < 0 || paceMs > MaxPaceMs)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 needs 'pace_ms' as a whole number of milliseconds, 0..%2 - "
					"the delay between two scanned files, so a progress display (or a test) can "
					"watch the run. Got %3")
					.arg(ScanStartName).arg(MaxPaceMs).arg(pace.toInt()));
		}
	}

	PluginFactory* factory = pluginFactoryOrNull();
	if (factory == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: this instance has no plugin factory").arg(ScanStartName));
	}

	QString error;
	if (!factory->startBackgroundScan(paceMs, &error))
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: %2").arg(ScanStartName, error));
	}

	const PluginFactory::BackgroundScanStatus status = factory->backgroundScanStatus();
	QJsonObject result;
	result.insert(QStringLiteral("task_id"), status.taskId);
	result.insert(QStringLiteral("total_files"), status.totalFiles);
	result.insert(QStringLiteral("pace_ms"), paceMs);
	result.insert(QStringLiteral("catalogue_count"), status.catalogueCount);
	result.insert(QStringLiteral("note"),
		QStringLiteral("the run is active before any directory or library is touched: the first turn plans "
			"the files (total_files 0 here, planned then), each following turn pre-loads one library or "
			"scans one file, so plugin.list keeps serving the previous catalogue (catalogue_count files it "
			"holds) until the run commits, plugin.scan_status follows the progress and the per-file outcomes, "
			"plugin.scan_cancel stops the run and keeps the previous catalogue, and the task list (task.list) "
			"shows the run with its progress and Cancel"));
	QJsonObject before;
	before.insert(QStringLiteral("outcome"), QStringLiteral("never"));
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: a run that finishes re-measures the files on disk and refreshes "
					"the cache, and no command restores a file's PREVIOUS fingerprint record once the "
					"run has replaced it - the same reason plugin.rescan is irreversible, with the same "
					"fallback. The project is not touched by any of this")}});
	return ControlResult::success(result);
}

ControlResult handleScanStatus()
{
	PluginFactory* factory = pluginFactoryOrNull();
	if (factory == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: this instance has no plugin factory").arg(ScanStatusName));
	}

	QJsonObject result = scanStatusJson(factory->backgroundScanStatus());
	result.insert(QStringLiteral("note"),
		QStringLiteral("outcome is never (no run yet), running, done or cancelled. progress is 0 "
			"before the first run, done/total while running or cancelled mid-run, 1 when the run "
			"finished or there was nothing to scan. files holds one entry per finished file, in scan "
			"order; catalogue_count is what plugin.list serves - the previous catalogue while a run "
			"is active. task_id is the run's row in task.list (-1 before the first run)"));
	return ControlResult::success(result);
}

ControlResult handleScanCancel()
{
	PluginFactory* factory = pluginFactoryOrNull();
	if (factory == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: this instance has no plugin factory").arg(ScanCancelName));
	}

	QString error;
	if (!factory->requestBackgroundScanCancel(&error))
	{
		// No run is active: say whether there never was one or the last one
		// already ended, so the caller can tell "too early" from "too late".
		const PluginFactory::BackgroundScanStatus status = factory->backgroundScanStatus();
		if (status.outcome == QLatin1String("never"))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("%1: %2").arg(ScanCancelName, error));
		}
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: the last run already ended (%2) - there is nothing to stop")
				.arg(ScanCancelName, status.outcome));
	}

	const PluginFactory::BackgroundScanStatus status = factory->backgroundScanStatus();
	QJsonObject result;
	result.insert(QStringLiteral("cancelling"), true);
	result.insert(QStringLiteral("done_files"), status.doneFiles);
	result.insert(QStringLiteral("total_files"), status.totalFiles);
	result.insert(QStringLiteral("task_id"), status.taskId);
	result.insert(QStringLiteral("note"),
		QStringLiteral("the run stops at its next file boundary and keeps the previous catalogue: "
			"plugin.scan_status then reports outcome cancelled with what the run had finished, and "
			"the records it measured stay cached for the next run. The project is not touched"));
	return ControlResult::success(result);
}

} // namespace

void registerPluginScanBackgroundCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = ScanStartName;
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_start");
		cmd.description = QStringLiteral("Start a background plug-in rescan: the same scan "
			"plugin.rescan runs, one file per event-loop turn, so the window stays usable - "
			"plugin.list keeps serving the previous catalogue until the run commits. Follow it with "
			"plugin.scan_status, stop it with plugin.scan_cancel; the task list (task.list) shows it "
			"with its progress and Cancel. 'pace_ms' (0..1000, default 0) is the delay between two "
			"files, so a progress display or a test can watch the run. Not reversible: a finished run "
			"re-measures the files and refreshes the cache, like plugin.rescan.");
		// A background rescan only accesses PluginFactory and its derived
		// cache; it is a valid menu operation before project-edit readiness.
		cmd.requiresEngine = false;
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("pace_ms"), integerProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("task_id"), integerProperty()},
			{QStringLiteral("total_files"), integerProperty()},
			{QStringLiteral("pace_ms"), integerProperty()},
			{QStringLiteral("catalogue_count"), integerProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleScanStart(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = ScanStatusName;
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_status");
		cmd.description = QStringLiteral("The background plug-in rescan's progress: whether a run "
			"is active, its outcome (never, running, done, cancelled), done/total files and the "
			"0..1 progress, the file being scanned, one entry per finished file (path, status, "
			"name, error) in scan order, the run's task.list row and the catalogue_count plugin.list "
			"still serves. total_files is 0 until the run's first turn plans the files. Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("cancel_requested"), booleanProperty()},
			{QStringLiteral("outcome"), stringProperty()},
			{QStringLiteral("total_files"), integerProperty()},
			{QStringLiteral("done_files"), integerProperty()},
			{QStringLiteral("progress"), numberProperty()},
			{QStringLiteral("current_file"), stringProperty()},
			{QStringLiteral("files"), arrayProperty()},
			{QStringLiteral("task_id"), integerProperty()},
			{QStringLiteral("catalogue_count"), integerProperty()},
			{QStringLiteral("report"), stringProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleScanStatus(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = ScanCancelName;
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cancel");
		cmd.description = QStringLiteral("Stop the background plug-in rescan at its next file "
			"boundary and keep the previous catalogue: plugin.scan_status then reports outcome "
			"cancelled with what the run had finished, and the records it measured stay cached for "
			"the next run. Refused when the last run already ended, not found when no run ever "
			"started. The project is not touched.");
		cmd.requiresEngine = false;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("cancelling"), booleanProperty()},
			{QStringLiteral("done_files"), integerProperty()},
			{QStringLiteral("total_files"), integerProperty()},
			{QStringLiteral("task_id"), integerProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleScanCancel(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
