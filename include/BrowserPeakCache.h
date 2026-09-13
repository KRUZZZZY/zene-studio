/*
 * BrowserPeakCache.h - the waveform peak cache the browser reads through.
 *
 * WHAT WAS ALREADY HERE, AND WHY IT IS NOT THIS. src/gui/SampleThumbnail.cpp
 * holds a peak/thumbnail cache used by the clip view, and the question the
 * release contract asks is whether the browser can reuse it. It cannot, and the
 * reason is structural rather than a preference:
 *   - it lives in `lmms::gui` and its cache is private: the nested `Thumbnail`
 *     type, `m_thumbnailCache` and the static `s_sampleThumbnailCacheMap` are
 *     all under `private:` in include/SampleThumbnail.h, and the class exposes
 *     exactly one operation, `visualize(parameters, QPainter&)` - there is no
 *     accessor for peak DATA, so no caller can read peaks out of it at all;
 *   - its constructor takes `const Sample&` and dereferences
 *     `sample.buffer()`: reaching it means fully DECODING the file into a
 *     SampleBuffer, which is the cost a browser peak cache exists to avoid -
 *     the browser must answer for a file it has not loaded;
 *   - it is built for painting, so it carries one aggregation step
 *     (AggregationPerZoomStep = 10) tuned to the clip view's zoom levels.
 * What IS reused is the design, deliberately and by name: min/max peaks over
 * interleaved samples `(Peak{min, max})`, a zoom ladder for downsampling, a
 * cache keyed on (path, last modified), and a capacity of 32 entries.
 *
 * WHAT THIS IS. A streaming peak reader: it opens the file with libsndfile,
 * walks it in blocks, and keeps at most `BaseBuckets` min/max pairs per file -
 * about 16 KiB - so a 30-minute stereo file and a one-second blip cost the same.
 * Memory is bounded by `Capacity` files, least-recently-used first.
 *
 * Thread affinity: the control registry runs handlers on the UI thread and
 * nothing on the audio thread may allocate or lock, so this cache is
 * UI-thread-only by construction. It is not thread-safe and does not pretend
 * to be.
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

#ifndef LMMS_BROWSER_PEAK_CACHE_H
#define LMMS_BROWSER_PEAK_CACHE_H

#include <QHash>
#include <QList>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! One bucket of a waveform: the lowest and highest interleaved sample in it.
struct LMMS_EXPORT BrowserPeak
{
	float min = 0.0f;
	float max = 0.0f;
};

/*! The peak cache. One entry per file, at a fixed fine resolution, aggregated
 *  on read to whatever resolution the caller asked for.
 */
class LMMS_EXPORT BrowserPeakCache
{
public:
	//! Files kept at once - the same bound the clip view's thumbnail cache has.
	static constexpr int Capacity = 32;
	/*! Peaks stored per file. 2048 buckets is ~16 KiB per entry, is finer than
	 *  any browser row is wide, and is the ceiling a caller may ask for: the
	 *  command's schema refuses a larger `buckets` rather than silently
	 *  answering a coarser waveform than was asked for. */
	static constexpr int BaseBuckets = 2048;

	//! What one cached file carries. `peaks` always holds `baseBuckets` pairs.
	struct Entry
	{
		QString path;
		qint64 modifiedMs = 0;
		qint64 sizeBytes = 0;
		int sampleRate = 0;
		int channels = 0;
		qint64 frames = 0;
		int baseBuckets = 0;
		QList<BrowserPeak> peaks;
	};

	//! What one peaksFor() call resolved to.
	struct Answer
	{
		//! True when the entry came out of the cache rather than off the disk.
		bool cached = false;
		//! The resolution actually answered: the request, clamped to the base.
		int buckets = 0;
		//! Frames per answered bucket (at least 1).
		qint64 bucketFrames = 1;
		int sampleRate = 0;
		int channels = 0;
		qint64 frames = 0;
		QList<BrowserPeak> peaks;
	};

	static BrowserPeakCache& instance();

	/*! Peaks for \a path at \a buckets resolution.
	 *
	 * False with \a error set when the file cannot be read (a directory, a
	 * format libsndfile does not know, a decode error) - a typed refusal, never
	 * an empty success. On success \a answer also reports whether the data came
	 * from the cache, which is what makes the cache observable over the socket.
	 */
	bool peaksFor(const QString& path, int buckets, Answer* answer, QString* error);

	//! How many files are held.
	int entryCount() const;
	//! Requests answered from the cache, and requests that had to open the file.
	int hitCount() const;
	int missCount() const;
	//! Drops every entry, without touching the statistics.
	void clear();
	//! Drops one entry (the file changed under us, or a test wants a cold cache).
	void forget(const QString& path);

private:
	BrowserPeakCache() = default;

	//! The cache entry for \a path, reading the file when it is absent or stale.
	bool entryFor(const QString& path, Entry* entry, bool* cached, QString* error);
	//! Streams \a path and fills \a entry's peaks.
	bool compute(const QString& path, Entry* entry, QString* error);
	//! Drops the least recently used entry when the cache is full.
	void evictIfFull();
	//! Marks \a path as most recently used.
	void touch(const QString& path);

	QHash<QString, Entry> m_entries;
	//! Most-recently-used last; the eviction victim is the front.
	QList<QString> m_recent;
	int m_hits = 0;
	int m_misses = 0;
};

} // namespace lmms

#endif // LMMS_BROWSER_PEAK_CACHE_H
