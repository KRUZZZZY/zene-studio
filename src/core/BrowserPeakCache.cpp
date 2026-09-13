/*
 * BrowserPeakCache.cpp - the streaming peak reader behind the browser's
 *                        waveform cache.
 *
 * The file is walked once, in blocks, and reduced to at most BaseBuckets
 * min/max pairs. Nothing is held beyond that: a 30-minute stereo file costs the
 * same ~16 KiB as a one-second blip, which is the property the clip view's
 * thumbnail cache gets from `zoomOut()` and this one gets from never storing
 * more than the ladder's top rung.
 *
 * The sample values are the interleaved floats libsndfile hands back (PCM is
 * normalised to -1..1), which is the same quantity SampleThumbnail aggregates -
 * see include/BrowserPeakCache.h for why that class's cache could not be reused.
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

#include "BrowserPeakCache.h"

#include <algorithm>
#include <limits>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QVector>

#include "BrowserCatalog.h"

#include <sndfile.h>

namespace lmms
{

namespace
{

//! Frames read per pass. Big enough that the syscall overhead disappears,
//! small enough that the block is a few hundred KiB at most.
constexpr int kReadBlockFrames = 4096;

//! Reduces the base-resolution peaks to \a buckets output buckets. Every output
//! bucket spans at least one base peak, so a request coarser than the base is a
//! min/max merge and never a resample of samples that are no longer there.
QList<BrowserPeak> aggregatePeaks(const QList<BrowserPeak>& base, int buckets)
{
	QList<BrowserPeak> out;
	if (base.isEmpty() || buckets <= 0) { return out; }
	for (int i = 0; i < buckets; ++i)
	{
		const int begin = static_cast<int>(static_cast<qint64>(i) * base.size() / buckets);
		const int end = static_cast<int>(static_cast<qint64>(i + 1) * base.size() / buckets);
		BrowserPeak merged = base.at(begin);
		for (int j = begin + 1; j < std::max(end, begin + 1); ++j)
		{
			const BrowserPeak& peak = base.at(j);
			merged.min = std::min(merged.min, peak.min);
			merged.max = std::max(merged.max, peak.max);
		}
		out.append(merged);
	}
	return out;
}

//! The bucket a frame belongs to, clamped into range: a file whose header
//! under-reports its length would otherwise index past the array.
int bucketOf(qint64 frame, qint64 frames, int buckets)
{
	if (frames <= 0) { return 0; }
	return std::clamp(static_cast<int>(frame * buckets / frames), 0, buckets - 1);
}

//! The lowest and highest interleaved sample per bucket, over \a frames of
//! \a file, read in blocks. Split out of compute() so each function stays inside
//! this fork's per-method complexity target: the sweep is four loops deep as it
//! stands, and adding the header arithmetic to it measures over 10.
void accumulatePeaks(SNDFILE* file, qint64 frames, int channels, QVector<float>* lowest,
	QVector<float>* highest)
{
	const int buckets = static_cast<int>(lowest->size());
	QVector<float> block(kReadBlockFrames * channels);
	qint64 frame = 0;
	while (frame < frames)
	{
		const sf_count_t read = sf_readf_float(file, block.data(), kReadBlockFrames);
		if (read <= 0) { return; }
		for (sf_count_t i = 0; i < read; ++i)
		{
			const int bucket = bucketOf(frame + i, frames, buckets);
			for (int channel = 0; channel < channels; ++channel)
			{
				// The index is an int: the block is at most
				// kReadBlockFrames * channels frames, and Qt5's QVector::operator[]
				// takes an int while Qt6's takes a qsizetype.
				const float sample = block[static_cast<int>(i) * channels + channel];
				(*lowest)[bucket] = std::min((*lowest).at(bucket), sample);
				(*highest)[bucket] = std::max((*highest).at(bucket), sample);
			}
		}
		frame += read;
	}
}

//! The stored peaks. A bucket no frame landed in (a short file, a truncated
//! tail) is silence, not +/-infinity: the waveform is flat there.
QList<BrowserPeak> peaksFromBounds(const QVector<float>& lowest, const QVector<float>& highest)
{
	QList<BrowserPeak> out;
	out.reserve(lowest.size());
	for (int i = 0; i < lowest.size(); ++i)
	{
		if (lowest.at(i) <= highest.at(i)) { out.append(BrowserPeak{lowest.at(i), highest.at(i)}); }
		else { out.append(BrowserPeak{0.0f, 0.0f}); }
	}
	return out;
}

} // namespace

BrowserPeakCache& BrowserPeakCache::instance()
{
	static BrowserPeakCache cache;
	return cache;
}

bool BrowserPeakCache::compute(const QString& path, Entry* entry, QString* error)
{
	SF_INFO info{};
	const QByteArray encoded = QFile::encodeName(path);
	SNDFILE* file = sf_open(encoded.constData(), SFM_READ, &info);
	if (file == nullptr)
	{
		*error = QString::fromUtf8(sf_strerror(nullptr));
		return false;
	}
	if (info.frames <= 0 || info.channels <= 0)
	{
		// A peak map is placed against the file's length; a stream whose length
		// is not in its header cannot be mapped, and guessing one would draw a
		// waveform that is not the file's.
		*error = QStringLiteral("%1 carries no usable length in its header "
			"(frames=%2, channels=%3)").arg(path).arg(info.frames).arg(info.channels);
		sf_close(file);
		return false;
	}

	const qint64 frames = static_cast<qint64>(info.frames);
	const int channels = info.channels;
	const int buckets = static_cast<int>(std::min<qint64>(BaseBuckets, frames));

	QVector<float> lowest(buckets, std::numeric_limits<float>::infinity());
	QVector<float> highest(buckets, -std::numeric_limits<float>::infinity());
	accumulatePeaks(file, frames, channels, &lowest, &highest);
	sf_close(file);

	entry->path = path;
	entry->sampleRate = info.samplerate;
	entry->channels = channels;
	entry->frames = frames;
	entry->baseBuckets = buckets;
	entry->peaks = peaksFromBounds(lowest, highest);
	return true;
}

bool BrowserPeakCache::entryFor(const QString& path, Entry* entry, bool* cached, QString* error)
{
	const QString key = browserCanonicalKey(path);
	const QFileInfo info(key);
	if (!info.exists()) { *error = QStringLiteral("%1 does not exist").arg(key); return false; }
	if (!info.isFile()) { *error = QStringLiteral("%1 is not a file").arg(key); return false; }

	const qint64 modified = info.lastModified().toMSecsSinceEpoch();
	const qint64 size = info.size();
	const auto found = m_entries.constFind(key);
	if (found != m_entries.constEnd() && found->modifiedMs == modified && found->sizeBytes == size)
	{
		*entry = *found;
		*cached = true;
		++m_hits;
		touch(key);
		return true;
	}

	Entry fresh;
	fresh.modifiedMs = modified;
	fresh.sizeBytes = size;
	if (!compute(key, &fresh, error)) { return false; }
	++m_misses;
	m_entries.insert(key, fresh);
	touch(key);
	evictIfFull();
	*entry = fresh;
	*cached = false;
	return true;
}

bool BrowserPeakCache::peaksFor(const QString& path, int buckets, Answer* answer, QString* error)
{
	Entry entry;
	bool cached = false;
	if (answer == nullptr) { return false; }
	if (!entryFor(path, &entry, &cached, error)) { return false; }

	const int wanted = std::clamp(buckets, 1, std::max(entry.baseBuckets, 1));
	answer->cached = cached;
	answer->buckets = wanted;
	answer->bucketFrames = std::max<qint64>(1, (entry.frames + wanted - 1) / wanted);
	answer->sampleRate = entry.sampleRate;
	answer->channels = entry.channels;
	answer->frames = entry.frames;
	answer->peaks = aggregatePeaks(entry.peaks, wanted);
	return true;
}

int BrowserPeakCache::entryCount() const
{
	return m_entries.size();
}

int BrowserPeakCache::hitCount() const
{
	return m_hits;
}

int BrowserPeakCache::missCount() const
{
	return m_misses;
}

void BrowserPeakCache::clear()
{
	m_entries.clear();
	m_recent.clear();
}

void BrowserPeakCache::forget(const QString& path)
{
	const QString key = browserCanonicalKey(path);
	m_entries.remove(key);
	m_recent.removeAll(key);
}

void BrowserPeakCache::touch(const QString& path)
{
	m_recent.removeAll(path);
	m_recent.append(path);
}

void BrowserPeakCache::evictIfFull()
{
	// Least recently used first, and never the entry that was just touched: the
	// list holds the newest at the back, so `size() > 1` guards the case of a
	// capacity below one.
	while (m_entries.size() > Capacity && m_recent.size() > 1)
	{
		m_entries.remove(m_recent.takeFirst());
	}
}

} // namespace lmms
