/*
 * SafeStart.cpp - safe-start mode after a crash: the crash marker, the
 *                 acknowledgement, and the load-time predicate.
 *
 * Feature row 77 of docs/FEATURE-LIST-0.3.0.md ("Safe-start mode after a crash -
 * launch with third-party plugins disabled", OWNER-31 item 31). The design, the
 * definition of "third-party", the marker's write-at-start construction and the
 * acknowledgement's one-shot semantics are all argued in include/SafeStart.h;
 * this file is the implementation of that header and adds nothing to it.
 *
 * The two prerequisites it sits on were already in the tree and are used here
 * rather than reimplemented:
 *   * the crash reporter (feature row 54, include/CrashReporter.h) - the signal
 *     handler and the offered sentinel this module's lifecycle mirrors;
 *   * the plugin scan cache and its quarantine list (feature row 46,
 *     include/PluginScanCache.h) - a quarantined file is already hidden from
 *     DISCOVERY by PluginFactory; safe-start is the other half of the same
 *     question, because a plugin that loads fine and then kills the process
 *     cannot be quarantined by a scan that never returned. Skipping INSTANCES is
 *     what covers that case.
 *
 * Failure tolerance is the contract, exactly as it is for the scan cache: a
 * missing, unreadable, corrupt or partly-malformed marker must degrade to "the
 * previous run did not exit cleanly" - which is the SAFE reading - never to a
 * crash, and never to silently doing nothing. The marker's CONTENT is a
 * diagnostic; its PRESENCE is the signal.
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

#include "SafeStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdlib>
#include <ctime>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace lmms::safestart
{

namespace
{

//! The process-wide state. One session, one working directory: the module is a
//! plain C API over files, and this is the whole of its memory.
struct State
{
	std::string directory;
	bool installed = false;
	//! Snapshot of "a marker was on disk" taken at beginSession().
	bool markerWasPresent = false;
	//! True when this session must skip third-party plugin instances.
	bool safeStart = false;
	//! Consecutive safe starts, 1 for the first one after the crash.
	unsigned long long safeStartRuns = 0;
	//! The session-scoped half of the predicate (safestart.set_skip).
	bool skipEnabled = true;
	SessionRecord previous;
	//! THIS session's own project, recorded in the marker this session writes.
	std::string projectPath;
	std::vector<SkippedInstance> skipped;
};

State& state()
{
	static State s;
	return s;
}

std::string fileInDirectory(const std::string& name)
{
	const State& s = state();
	if (!s.installed || s.directory.empty()) { return std::string(); }
	return s.directory + "/" + name;
}

bool pathExists(const std::string& path)
{
	if (path.empty()) { return false; }
	return QFileInfo::exists(QString::fromStdString(path));
}

unsigned long long unixTimeNow()
{
	return static_cast<unsigned long long>(::time(nullptr));
}

unsigned long long processIdNow()
{
#ifdef Q_OS_WIN
	return static_cast<unsigned long long>(QCoreApplication::applicationPid());
#else
	return static_cast<unsigned long long>(::getpid());
#endif
}

//! Bound a string to `max` bytes for a file this module owns. Truncation is by
//! BYTES and may split a multi-byte character, which is acceptable for a
//! diagnostic hint and is why the marker is read back as bytes.
std::string bounded(const std::string& text, unsigned long max)
{
	if (text.size() <= max) { return text; }
	return text.substr(0, max);
}

// ---------------------------------------------------------------------------
// the marker file
// ---------------------------------------------------------------------------

const char* kMarkerHeader = "Zene Studio safe-start marker v1\n";

std::string markerText()
{
	const State& s = state();
	std::string out = kMarkerHeader;
	out += "pid=" + std::to_string(processIdNow()) + "\n";
	out += "time_unix=" + std::to_string(unixTimeNow()) + "\n";
	out += "safe_start_runs=" + std::to_string(s.safeStartRuns) + "\n";
	out += "project=" + bounded(s.projectPath, kMaxProjectPathBytes) + "\n";
	return out;
}

//! Parse a marker. Tolerant by contract: a line that is not understood is
//! ignored, and a marker that is empty or malformed still parses to
//! present=true, because its presence is the signal and its content is the
//! diagnostic (see the file header).
SessionRecord parseMarker(const std::string& text)
{
	SessionRecord record;
	record.present = true;
	std::size_t start = 0;
	while (start < text.size())
	{
		const std::size_t end = text.find('\n', start);
		const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		const std::size_t eq = line.find('=');
		if (eq != std::string::npos)
		{
			const std::string key = line.substr(0, eq);
			const std::string value = line.substr(eq + 1);
			try
			{
				if (key == "pid") { record.processId = std::stoull(value); }
				else if (key == "time_unix") { record.unixTime = std::stoull(value); }
				else if (key == "safe_start_runs") { record.safeStartRuns = std::stoull(value); }
				// Bounded HERE, not only when the marker is written: the file
				// on disk is not necessarily one this build wrote (an older
				// build, a packager's wrapper, a hand-edited marker), and
				// SessionRecord promises strings bounded on read as well as on
				// write (include/SafeStart.h). Measured by
				// SafeStartTest::boundsTheMarkerCannotExceedItsCap, which
				// plants an 8 KiB project= line: readMarker()'s byte cap alone
				// still admitted a hint larger than kMaxProjectPathBytes.
				else if (key == "project") { record.projectPath = bounded(value, kMaxProjectPathBytes); }
			}
			catch (const std::exception&)
			{
				// A field that does not parse stays at its default; the marker
				// as a whole is still "the previous run did not exit cleanly".
			}
		}
		if (end == std::string::npos) { break; }
		start = end + 1;
	}
	return record;
}

SessionRecord readMarker(const std::string& path)
{
	SessionRecord record;
	if (!pathExists(path)) { return record; }
	QFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::ReadOnly)) { return record; }
	// The cap is a read bound as well as a write bound: a marker grown by
	// something else must not be pulled into memory whole.
	const QByteArray bytes = file.read(static_cast<qint64>(kMaxMarkerBytes));
	file.close();
	(void) bytes.size();
	return parseMarker(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

//! Write the marker atomically: QSaveFile leaves the previous file intact when
//! the write fails, which matters because the file on disk IS the signal.
bool writeMarker(const std::string& path, const std::string& text)
{
	if (path.empty()) { return false; }
	QSaveFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::WriteOnly))
	{
		qWarning("safe-start: cannot write the crash marker at %s", path.c_str());
		return false;
	}
	const QByteArray bytes = QByteArray::fromStdString(text);
	file.write(bytes.constData(), qMin<qint64>(bytes.size(), static_cast<qint64>(kMaxMarkerBytes)));
	if (!file.commit())
	{
		qWarning("safe-start: cannot commit the crash marker at %s", path.c_str());
		return false;
	}
	return true;
}

//! The acknowledgement carries a timestamp and nothing else: it has exactly one
//! meaning ("the next launch is a normal one") and is consumed by that launch.
bool writeAcknowledged(const std::string& path)
{
	if (path.empty()) { return false; }
	QSaveFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	const std::string text = "acknowledged_unix=" + std::to_string(unixTimeNow()) + "\n";
	const QByteArray bytes = QByteArray::fromStdString(text);
	file.write(bytes.constData(), bytes.size());
	return file.commit();
}

} // namespace

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

bool install(const std::string& workingDirectory)
{
	State& s = state();
	s.installed = false;
	s.directory.clear();
	if (workingDirectory.empty()) { return false; }
	const QFileInfo info(QString::fromStdString(workingDirectory));
	if (!info.exists() || !info.isDir()) { return false; }
	s.directory = QDir::cleanPath(info.absoluteFilePath()).toStdString();
	s.installed = true;
	return true;
}

bool isInstalled() { return state().installed; }

std::string workingDirectory() { return state().directory; }

std::string markerPath() { return fileInDirectory(kMarkerName); }

std::string acknowledgedPath() { return fileInDirectory(kAcknowledgedName); }

void beginSession()
{
	State& s = state();
	s.skipped.clear();
	s.safeStartRuns = 0;
	s.safeStart = false;
	s.markerWasPresent = false;
	s.skipEnabled = true;
	if (!s.installed) { return; }

	const std::string marker = markerPath();
	const std::string acknowledgedFile = acknowledgedPath();

	// READ what the previous run left, before this session's marker replaces it.
	s.previous = readMarker(marker);
	s.markerWasPresent = s.previous.present;

	// CONSUME the acknowledgement: it only ever meant "the NEXT launch is a
	// normal one", so it is removed as it is read. A decision taken about one
	// crash therefore cannot mask a second one.
	const bool wasAcknowledged = pathExists(acknowledgedFile);
	if (wasAcknowledged) { QFile::remove(QString::fromStdString(acknowledgedFile)); }

	s.safeStart = s.markerWasPresent && !wasAcknowledged;
	if (s.safeStart) { s.safeStartRuns = s.previous.safeStartRuns + 1; }
	// Start from what the crashed session had open; main() updates it as the
	// project is loaded, exactly as it updates the crash reporter's hint.
	s.projectPath = s.previous.projectPath;

	// THIS session's marker. Writing it is what makes the next launch able to
	// tell a crash from a deliberate quit (see the header).
	if (!writeMarker(marker, markerText()))
	{
		// The marker could not be written: the safe reading of that failure is
		// to hold this session in safe-start mode anyway when a marker was
		// found, which is already the state above. Nothing else changes.
		qWarning("safe-start: this session has no marker on disk, so an abnormal "
			"exit cannot be told from a clean one by the next launch");
	}
}

void endSession()
{
	State& s = state();
	if (!s.installed) { return; }
	// The clean path: the crash marker and the decision it carried both go, so
	// the next launch is a normal one.
	QFile::remove(QString::fromStdString(markerPath()));
	QFile::remove(QString::fromStdString(acknowledgedPath()));
	s.markerWasPresent = false;
	s.safeStart = false;
	s.safeStartRuns = 0;
	s.projectPath.clear();
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

bool markerExists() { return pathExists(markerPath()); }

bool acknowledged() { return pathExists(acknowledgedPath()); }

bool previousRunExitedCleanly() { return !markerExists(); }

bool safeStartActive() { return state().safeStart; }

SessionRecord lastSession() { return state().previous; }

unsigned long long safeStartRunCount() { return state().safeStartRuns; }

std::string projectPath() { return state().projectPath; }

void setProjectPath(const std::string& path)
{
	State& s = state();
	if (!s.installed) { return; }
	s.projectPath = path;
	// Rewrite the marker so the hint is on disk BEFORE anything can die - which
	// is the whole point of it. Only when a marker is already there: this call
	// must never create crash state by itself.
	if (path.empty() || !markerExists()) { return; }
	writeMarker(markerPath(), markerText());
}

void acknowledge()
{
	if (!state().installed) { return; }
	// The marker STAYS: the crash is still there to look at, and the record of
	// which session died is still on disk for the next launch's report. Only the
	// decision is recorded here.
	writeAcknowledged(acknowledgedPath());
}

void clear()
{
	State& s = state();
	if (!s.installed) { return; }
	QFile::remove(QString::fromStdString(markerPath()));
	QFile::remove(QString::fromStdString(acknowledgedPath()));
	// Nothing about THIS session is a crash marker any more, so it is not held
	// in safe-start mode either: a project loaded after this call loads its
	// third-party plugins. Instances already skipped stay skipped - they were
	// never created, and nothing here pretends to create them retroactively.
	s.markerWasPresent = false;
	s.safeStart = false;
	s.skipEnabled = true;
}

// ---------------------------------------------------------------------------
// the load-time predicate
// ---------------------------------------------------------------------------

std::vector<std::string> ownPluginDirectories()
{
	std::vector<std::string> directories;
	auto add = [&directories](const QString& path) {
		if (path.isEmpty()) { return; }
		const QString cleaned = QDir::cleanPath(path);
		if (cleaned.isEmpty()) { return; }
		const std::string text = cleaned.toStdString();
		if (std::find(directories.begin(), directories.end(), text) == directories.end())
		{
			directories.push_back(text);
		}
	};

	// The directories PluginFactory::setupSearchPaths() derives from the
	// executable: the portable layout (application dir + "/plugins") and the
	// installed Unix layout (application dir + "/../lib/zene"). A plugin file
	// under one of these is a file this build ships.
	if (QCoreApplication::instance() != nullptr)
	{
		const QDir appDir(QCoreApplication::applicationDirPath());
		add(appDir.absoluteFilePath(QStringLiteral("plugins")));
		add(appDir.absoluteFilePath(QStringLiteral("../lib/zene")));
	}
#ifdef PLUGIN_DIR
	// A relative directory handed to the build at configure time.
	add(QDir(QCoreApplication::instance() != nullptr
			? QCoreApplication::applicationDirPath() : QDir::currentPath())
			.absoluteFilePath(QString::fromUtf8(PLUGIN_DIR)));
#endif
	// The packager / developer override, and the directory the product's own
	// tests point at the build tree's modules.
	const QByteArray env = qgetenv("LMMS_PLUGIN_DIR");
	if (!env.isEmpty()) { add(QString::fromLocal8Bit(env)); }
	return directories;
}

bool isThirdPartyPluginFile(const std::string& filePath)
{
	if (filePath.empty()) { return false; }

	// Resolve what the file REALLY is where that is possible, so a link to one
	// of our own modules counts as ours (see the header).
	const QFileInfo info(QString::fromStdString(filePath));
	QString resolved = info.canonicalFilePath();
	if (resolved.isEmpty()) { resolved = info.absoluteFilePath(); }
	if (resolved.isEmpty()) { resolved = QDir::cleanPath(QString::fromStdString(filePath)); }

	for (const std::string& directory : ownPluginDirectories())
	{
		const QString own = QString::fromStdString(directory);
		// A component-wise prefix: "/a/lib" owns "/a/lib/x.so" and NOT
		// "/a/library.so", which a bare startsWith() would get wrong.
		if (resolved == own) { return false; }
		if (resolved.startsWith(own + QLatin1Char('/'))) { return false; }
		if (QString::fromStdString(filePath).startsWith(own + QLatin1Char('/'))) { return false; }
	}
	return true;
}

bool shouldSkipPluginInstance(const std::string& pluginName, const std::string& filePath)
{
	(void) pluginName;
	const State& s = state();
	if (!s.safeStart || !s.skipEnabled) { return false; }
	return isThirdPartyPluginFile(filePath);
}

void noteSkippedInstance(const std::string& pluginName, const std::string& filePath,
	const std::string& reason)
{
	SkippedInstance instance;
	instance.pluginName = bounded(pluginName, kMaxProjectPathBytes);
	instance.file = bounded(filePath, kMaxMarkerBytes);
	instance.reason = bounded(reason, kMaxMarkerBytes);
	state().skipped.push_back(instance);
}

const std::vector<SkippedInstance>& skippedInstances() { return state().skipped; }

int skippedCount() { return static_cast<int>(state().skipped.size()); }

void resetSkippedInstances() { state().skipped.clear(); }

bool skipEnabled() { return state().skipEnabled; }

void setSkipEnabled(bool enabled) { state().skipEnabled = enabled; }

} // namespace lmms::safestart
