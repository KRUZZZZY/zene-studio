/*
 * BrowserIndex.cpp - the library's persistent file index and each sound's features
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

#include "BrowserIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>

#include "ConfigManager.h"
#include "ImportDetection.h"
#include "ImportDetectionDsp.h"
#include "TaskCenter.h"

namespace lmms
{

namespace
{

constexpr int ProgressEvery = 64;

bool finiteFeatureNumber(const QJsonObject& o, const QString& key, double* value)
{
	const QJsonValue field = o.value(key);
	if (!field.isDouble()) { return false; }
	*value = field.toDouble();
	return std::isfinite(*value);
}

bool inFeatureRange(double value, double minimum, double maximum)
{
	return value >= minimum && value <= maximum;
}

bool validFeatureTempo(double bpm)
{
	return bpm == 0.0 || inFeatureRange(bpm, detection::MinDetectionBpm, detection::MaxDetectionBpm);
}

bool validFeatureKey(double key)
{
	return key == -1.0 || (inFeatureRange(key, 0.0, 11.0) && std::floor(key) == key);
}

bool validFeatureValues(const BrowserFeatures& f, double key)
{
	if (!inFeatureRange(f.durationSeconds, std::numeric_limits<double>::min(),
		static_cast<double>(std::numeric_limits<std::size_t>::max()))) { return false; }
	if (!inFeatureRange(f.loudnessDb, -120.0, 20.0 * std::log10(std::numeric_limits<float>::max()))) { return false; }
	if (!inFeatureRange(f.centroidHz, 0.0, std::numeric_limits<int>::max() / 2.0)) { return false; }
	if (!inFeatureRange(f.zeroCrossings, 0.0, std::numeric_limits<int>::max())) { return false; }
	if (!validFeatureTempo(f.bpm)) { return false; }
	return validFeatureKey(key);
}

QJsonObject featuresJson(const BrowserFeatures& f)
{
	return {{QStringLiteral("dur"), f.durationSeconds}, {QStringLiteral("db"), f.loudnessDb}, {QStringLiteral("cent"), f.centroidHz},
		{QStringLiteral("zcr"), f.zeroCrossings}, {QStringLiteral("bpm"), f.bpm}, {QStringLiteral("key"), f.keyPitchClass}};
}

BrowserFeatures featuresFrom(const QJsonObject& o)
{
	BrowserFeatures f;
	double key = -1.0;
	if (!finiteFeatureNumber(o, QStringLiteral("dur"), &f.durationSeconds)
		|| !finiteFeatureNumber(o, QStringLiteral("db"), &f.loudnessDb)
		|| !finiteFeatureNumber(o, QStringLiteral("cent"), &f.centroidHz)
		|| !finiteFeatureNumber(o, QStringLiteral("zcr"), &f.zeroCrossings)
		|| !finiteFeatureNumber(o, QStringLiteral("bpm"), &f.bpm)
		|| !finiteFeatureNumber(o, QStringLiteral("key"), &key)
		|| !validFeatureValues(f, key)) { return f; }
	f.keyPitchClass = static_cast<int>(key);
	f.valid = true;
	return f;
}

QJsonObject entryJson(const BrowserIndexEntry& e)
{
	QJsonObject out{{QStringLiteral("p"), e.path}, {QStringLiteral("k"), browserKindName(e.kind)},
		{QStringLiteral("s"), static_cast<double>(e.sizeBytes)}, {QStringLiteral("m"), static_cast<double>(e.modifiedMs)}};
	if (e.features.valid) { out.insert(QStringLiteral("f"), featuresJson(e.features)); }
	return out;
}

BrowserIndexEntry entryFrom(const QJsonObject& o)
{
	BrowserIndexEntry e;
	e.path = o.value(QStringLiteral("p")).toString();
	e.name = QFileInfo(e.path).fileName();
	browserKindFromName(o.value(QStringLiteral("k")).toString(), &e.kind);
	e.sizeBytes = static_cast<qint64>(o.value(QStringLiteral("s")).toDouble());
	e.modifiedMs = static_cast<qint64>(o.value(QStringLiteral("m")).toDouble());
	e.features = featuresFrom(o.value(QStringLiteral("f")).toObject());
	return e;
}

//! What one scan carries while it walks.
struct Walk
{
	QHash<QString, BrowserIndexEntry> previous;
	QSet<QString> seen;  //!< the paths found so far: roots that overlap list a file once
	bool features = false;
	int maxAnalysed = 0;
	std::vector<BrowserIndexEntry> found;
	BrowserIndex::Status status;
};

//! The entry for @a info at @a path: its features kept when the file is unchanged, measured when the walk may.
BrowserIndexEntry entryOf(const QFileInfo& info, const QString& path, Walk* walk)
{
	BrowserIndexEntry entry{path, info.fileName(), browserKindForPath(info.fileName()), info.size(),
		info.lastModified().toMSecsSinceEpoch(), {}};
	if (entry.kind != BrowserKind::Audio) { return entry; }
	// An unchanged file keeps its features in any scan; only measuring a new one waits for a features scan.
	const auto old = walk->previous.constFind(entry.path);
	if (old != walk->previous.constEnd() && old->features.valid && old->sizeBytes == entry.sizeBytes && old->modifiedMs == entry.modifiedMs)
	{
		entry.features = old->features;
	}
	else if (walk->features && walk->status.analysed < walk->maxAnalysed)
	{
		entry.features = BrowserFeatures::analyse(entry.path);
		++walk->status.analysed;
	}
	return entry;
}

//! Count the file @a info and add it to the walk - once, however many roots hold it - unless the walk
//! is full: then it is truncated (a file past the limit, so exactly MaxEntries files is not truncated).
void admit(const QFileInfo& info, Walk* walk)
{
	// Keyed as the tag store keys a file (its canonical path), so an indexed file's tags are found.
	const QString canonical = info.canonicalFilePath();
	const QString path = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
	const bool known = walk->seen.contains(path);
	if (!known && static_cast<int>(walk->found.size()) >= BrowserIndex::MaxEntries) { walk->status.truncated = true; return; }
	++walk->status.scanned;
	if (known) { return; }
	walk->seen.insert(path);
	walk->found.push_back(entryOf(info, path, walk));
}

} // namespace

BrowserIndex& BrowserIndex::instance()
{
	static BrowserIndex* index = new BrowserIndex();
	return *index;
}

QString BrowserIndex::storePath() const
{
	const QString overridden = qEnvironmentVariable("ZENE_BROWSER_INDEX");
	return !overridden.isEmpty() ? overridden : QDir(ConfigManager::inst()->workingDir()).filePath(QStringLiteral("browser-index.json"));
}

bool BrowserIndex::claim(const QStringList& dirs)
{
	QMutexLocker lock(&m_mutex);
	loadLocked();
	if (m_status.running) { return false; }
	m_cancel.store(false);  // before the scan is published: a cancel() from here on is kept
	m_status.running = true;
	m_status.dirs = dirs;
	m_status.scanned = 0;  // this scan's progress, not the last one's
	m_status.analysed = 0;
	m_status.error.clear();
	return true;
}

bool BrowserIndex::start(const QStringList& dirs, bool features)
{
	if (!claim(dirs)) { return false; }
	candidateScales();  // the key vocabulary is built here, once, not first on the worker
	static const bool stopsOnQuit = QCoreApplication::instance() != nullptr
		&& QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [] { BrowserIndex::instance().cancel(); });
	Q_UNUSED(stopsOnQuit)
	// 0.5.0 theme 24: a background scan is listed in the task area, with this scan's own cancel.
	TaskCenter::instance().begin(this, 0, {QStringLiteral("index"), QObject::tr("Index the library"), [] { BrowserIndex::instance().cancel(); },
		{}, false});
	QThreadPool::globalInstance()->start([this, dirs, features] {
		scan(dirs, features, MaxAnalysed);
		reportScanEnd();
	});
	return true;
}

bool BrowserIndex::scanNow(const QStringList& dirs, bool features, int maxAnalysed)
{
	if (!claim(dirs)) { return false; }
	scan(dirs, features, std::clamp(maxAnalysed, 0, MaxAnalysed));
	return true;
}

void BrowserIndex::scan(const QStringList& dirs, bool features, int maxAnalysed)
{
	Walk walk;
	walk.features = features;
	walk.maxAnalysed = maxAnalysed;
	walk.status.running = true;
	walk.status.dirs = dirs;
	{
		QMutexLocker lock(&m_mutex);
		for (const BrowserIndexEntry& entry : m_entries) { walk.previous.insert(entry.path, entry); }
	}
	for (const QString& dir : dirs)
	{
		QDirIterator it(dir, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
		while (it.hasNext() && !m_cancel.load() && !walk.status.truncated)
		{
			it.next();
			admit(it.fileInfo(), &walk);
			if (walk.status.scanned % ProgressEvery != 0) { continue; }
			QMutexLocker lock(&m_mutex);
			m_status.scanned = walk.status.scanned;
			m_status.analysed = walk.status.analysed;
		}
	}
	std::sort(walk.found.begin(), walk.found.end(), [](const BrowserIndexEntry& a, const BrowserIndexEntry& b) { return a.path < b.path; });
	walk.status.running = false;
	walk.status.entries = static_cast<int>(walk.found.size());
	walk.status.builtMs = QDateTime::currentMSecsSinceEpoch();
	const bool cancelled = m_cancel.load();
	const bool saved = !cancelled && save(walk.found, walk.status);
	QMutexLocker lock(&m_mutex);
	if (cancelled)
	{
		m_status.running = false;
		m_status.error = QStringLiteral("the scan was cancelled; the previous index is kept");
		return;
	}
	walk.status.error = saved ? QString() : QStringLiteral("the index could not be written to %1").arg(storePath());
	m_entries = std::move(walk.found);
	m_status = walk.status;
}

void BrowserIndex::reportScanEnd()
{
	const Status ended = status();
	const TaskState state = m_cancel.load() ? TaskState::Cancelled : ended.error.isEmpty() ? TaskState::Done : TaskState::Failed;
	TaskCenter::instance().finish(this, 0, state, state == TaskState::Done ? QObject::tr("%n file(s)", "", ended.entries) : ended.error);
}

BrowserIndex::Status BrowserIndex::status() const
{
	QMutexLocker lock(&m_mutex);
	loadLocked();
	Status out = m_status;
	if (!out.running) { out.entries = static_cast<int>(m_entries.size()); }
	return out;
}

std::vector<BrowserIndexEntry> BrowserIndex::entries() const
{
	QMutexLocker lock(&m_mutex);
	loadLocked();
	return m_entries;
}

void BrowserIndex::reload()
{
	QMutexLocker lock(&m_mutex);
	if (m_status.running) { return; }
	m_loaded = false;
	m_entries.clear();
	m_status = Status{};
	loadLocked();
}

void BrowserIndex::loadLocked() const
{
	if (m_loaded) { return; }
	m_loaded = true;
	QFile file(storePath());
	if (!file.open(QIODevice::ReadOnly)) { return; }
	const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
	m_status.truncated = all.value(QStringLiteral("truncated")).toBool();
	for (const QJsonValue& value : all.value(QStringLiteral("entries")).toArray())
	{
		// A damaged or hand-edited store is held to the scan's own bounds: no pathless rows, at most MaxEntries.
		BrowserIndexEntry entry = entryFrom(value.toObject());
		if (entry.path.isEmpty()) { continue; }
		if (static_cast<int>(m_entries.size()) >= MaxEntries) { m_status.truncated = true; break; }
		m_entries.push_back(std::move(entry));
	}
	m_status.builtMs = static_cast<qint64>(all.value(QStringLiteral("built_ms")).toDouble());
	for (const QJsonValue& dir : all.value(QStringLiteral("dirs")).toArray()) { m_status.dirs << dir.toString(); }
	m_status.entries = static_cast<int>(m_entries.size());
}

bool BrowserIndex::save(const std::vector<BrowserIndexEntry>& entries, const Status& status) const
{
	QJsonArray list;
	for (const BrowserIndexEntry& entry : entries) { list.append(entryJson(entry)); }
	const QJsonObject all{{QStringLiteral("version"), 1}, {QStringLiteral("built_ms"), static_cast<double>(status.builtMs)},
		{QStringLiteral("truncated"), status.truncated}, {QStringLiteral("dirs"), QJsonArray::fromStringList(status.dirs)},
		{QStringLiteral("entries"), list}};
	QDir().mkpath(QFileInfo(storePath()).absolutePath());
	QSaveFile file(storePath());
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
	return file.commit();
}

} // namespace lmms
