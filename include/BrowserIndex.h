/*
 * BrowserIndex.h - the library's persistent file index and each sound's features
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

#ifndef LMMS_BROWSER_INDEX_H
#define LMMS_BROWSER_INDEX_H

#include <atomic>
#include <vector>

#include <QMutex>
#include <QString>
#include <QStringList>

#include "BrowserCatalog.h"
#include "lmms_export.h"

namespace lmms
{

class TaskToken;

/*! 0.5.0 L11 (WF-020): what a sound is like, as a handful of numbers measured from the start of the file
 *  (at most FeatureSeconds): its length, loudness (RMS, dBFS), brightness (mean spectral centroid),
 *  noisiness (zero crossings per second), and the tempo and key the import detection finds
 *  (ImportDetectionDsp.h; 0 and -1 when it finds none). Two sounds are similar when these are close. */
struct LMMS_EXPORT BrowserFeatures
{
	static constexpr double FeatureSeconds = 20.0;

	bool valid = false;
	double durationSeconds = 0.0;
	double loudnessDb = -120.0;
	double centroidHz = 0.0;
	double zeroCrossings = 0.0;
	double bpm = 0.0;
	int keyPitchClass = -1;

	//! Measure @a path (decodes it: never on the audio thread). valid false when it cannot be read.
	static BrowserFeatures analyse(const QString& path);
	//! What distance() reports when either record was never measured: further
	//! apart than any two real measurements can be, so an unmeasured file sorts
	//! last instead of tying for first.
	static constexpr double InvalidDistance = 1e9;
	//! How far apart two sounds are: 0 for the same numbers, about 1 for clearly
	//! different ones, InvalidDistance when either was never measured.
	static double distance(const BrowserFeatures& a, const BrowserFeatures& b);
};

//! One indexed file.
struct LMMS_EXPORT BrowserIndexEntry
{
	QString path;
	QString name;
	BrowserKind kind = BrowserKind::Other;
	qint64 sizeBytes = 0;
	qint64 modifiedMs = 0;
	BrowserFeatures features;
};

/*! 0.5.0 L11 (WF-012): the library's file index - every file under the library's folders with its kind,
 *  size and date, and (when asked) each audio file's features - kept in the user's config directory
 *  (<config>/browser-index.json; ZENE_BROWSER_INDEX names another file), so a search over the whole
 *  library is a lookup instead of a walk, and a similar-sound search has something to compare with.
 *  A scan runs on a worker thread (never the audio thread) and replaces the index when it finishes and its store is written (a scan whose store cannot be written keeps the previous index and reports the error); a
 *  file whose size and date are unchanged keeps the features it had, so a rescan only measures what is
 *  new. Bounded: MaxEntries files (a longer walk says `truncated`) and MaxAnalysed measured per scan. */
class LMMS_EXPORT BrowserIndex
{
public:
	static constexpr int MaxEntries = 50000;
	static constexpr int MaxAnalysed = 5000;

	struct Status
	{
		bool running = false;
		int scanned = 0;
		int analysed = 0;
		int entries = 0;
		bool truncated = false;
		qint64 builtMs = 0;
		QStringList dirs;
		QString error;
	};

	static BrowserIndex& instance();
	QString storePath() const;

	//! Scan @a dirs on a worker thread, measuring audio files when @a features; false when a scan runs.
	bool start(const QStringList& dirs, bool features);
	//! Scan @a dirs here, now, measuring at most @a maxAnalysed files, never more than MaxAnalysed (a caller that waits for it).
	//! False, scanning nothing, when another scan is already running (review W3-agent-library #4).
	bool scanNow(const QStringList& dirs, bool features, int maxAnalysed);
	//! Ask a running scan to stop after the file it is on (its partial result is dropped).
	void cancel() { m_cancel.store(true); }
	Status status() const;
	//! A copy of the index (read from the store file the first time).
	std::vector<BrowserIndexEntry> entries() const;
	//! Forget the index in memory and read the store file again (a test points the store elsewhere).
	void reload();

private:
	BrowserIndex() = default;
	//! Publish a scan of @a dirs as running, its progress zeroed; false when one already runs.
	bool claim(const QStringList& dirs);
	void scan(const QStringList& dirs, bool features, int maxAnalysed);
	//! 0.5.0 theme 24: a background scan's end, reported to the task area (TaskCenter.h).
	void reportScanEnd(const TaskToken& token);
	void loadLocked() const;
	bool save(const std::vector<BrowserIndexEntry>& entries, const Status& status) const;

	mutable QMutex m_mutex;
	mutable std::vector<BrowserIndexEntry> m_entries;
	mutable Status m_status;
	mutable bool m_loaded = false;
	std::atomic<bool> m_cancel{false};
};

} // namespace lmms

#endif // LMMS_BROWSER_INDEX_H
