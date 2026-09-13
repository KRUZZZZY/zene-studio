/*
 * ControlCommandsBrowser.cpp - the browser.* command group (SPEC A11-A16),
 *                              read half: the roots, the query that finds an
 *                              item by name, tag or metadata, the tag
 *                              vocabulary, and the waveform peak cache.
 *
 * The engine half landed with this file: include/BrowserCatalog.h reads the
 * same directories the browser tabs read, include/BrowserPeakCache.h caches
 * waveform peaks for a file the browser has not loaded, and the tag store
 * persists to the user's config directory. What this file adds is the AGENT
 * SURFACE, which is what makes the feature part of this release at all: with no
 * browser.* commands, a tag could only be authored by editing a JSON file by
 * hand and a peak map could not be observed at all.
 *
 * The two mutating verbs (browser.tag.add / browser.tag.remove) live in
 * ControlCommandsBrowserTags.cpp - the same read/mutating split automation.*
 * and warp.* use, forced by the file-length ratchet.
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "BrowserCatalog.h"
#include "BrowserPeakCache.h"
#include "ControlBrowserSupport.h"
#include "ControlRegistry.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The snapshot of the browser.roots result - one object, so the two places
//! that report a root (this command and the query's `roots` list) cannot drift.
QJsonArray rootsJson(QStringList* ids)
{
	QJsonArray out;
	for (const BrowserRoot& root : browserRoots())
	{
		out.append(browserRootJson(root));
		if (ids != nullptr) { ids->append(root.id); }
	}
	return out;
}

QJsonObject storeSummaryJson()
{
	BrowserTagStore& store = BrowserTagStore::instance();
	QJsonObject out;
	out.insert(QStringLiteral("store_path"), store.storePath());
	out.insert(QStringLiteral("store_loaded"), QFileInfo::exists(store.storePath()));
	out.insert(QStringLiteral("store_error"), store.lastError());
	return out;
}

ControlResult browserRootsHandler(const QJsonObject&)
{
	QStringList ids;
	const QJsonArray roots = rootsJson(&ids);
	QJsonObject result;
	result.insert(QStringLiteral("roots"), roots);
	result.insert(QStringLiteral("count"), roots.size());
	result.insert(QStringLiteral("ids"), QJsonArray::fromStringList(ids));
	const QJsonObject summary = storeSummaryJson();
	for (auto it = summary.begin(); it != summary.end(); ++it) { result.insert(it.key(), it.value()); }
	return ControlResult::success(result);
}

ControlResult browserTagsHandler(const QJsonObject&)
{
	BrowserTagStore& store = BrowserTagStore::instance();
	// Read the file before answering: the store is the user's library, and a
	// second instance may have edited it since this one last looked.
	const bool loaded = store.load();

	QJsonArray tags;
	for (const QString& tag : store.allTags())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("tag"), tag);
		entry.insert(QStringLiteral("files"), store.filesWithTag(tag));
		tags.append(entry);
	}
	QJsonObject result;
	result.insert(QStringLiteral("tags"), tags);
	result.insert(QStringLiteral("distinct"), tags.size());
	result.insert(QStringLiteral("entries"), store.entryCount());
	result.insert(QStringLiteral("limit"), MaxDistinctTags);
	const QJsonObject summary = storeSummaryJson();
	for (auto it = summary.begin(); it != summary.end(); ++it) { result.insert(it.key(), it.value()); }
	result.insert(QStringLiteral("read_ok"), loaded);
	return ControlResult::success(result);
}

ControlResult browserQueryHandler(const QJsonObject& args)
{
	BrowserQuery query;
	ControlResult error;
	if (!readBrowserQueryArguments(args, &query, &error)) { return error; }

	const BrowserQueryResult found = runBrowserQuery(query);
	QJsonArray items;
	for (const BrowserItem& item : found.items) { items.append(browserItemJson(item, query.probe)); }

	QJsonObject result;
	result.insert(QStringLiteral("items"), items);
	result.insert(QStringLiteral("scanned"), found.scanned);
	result.insert(QStringLiteral("matched"), found.matched);
	result.insert(QStringLiteral("returned"), items.size());
	result.insert(QStringLiteral("probed"), found.probed);
	result.insert(QStringLiteral("truncated"), found.truncated);
	result.insert(QStringLiteral("probe"), query.probe);
	result.insert(QStringLiteral("scan_limit"), BrowserMaxScanEntries);
	result.insert(QStringLiteral("probe_limit"), BrowserMaxProbes);
	result.insert(QStringLiteral("roots"), QJsonArray::fromStringList(found.roots));
	return ControlResult::success(result);
}

ControlResult browserPeaksHandler(const QJsonObject& args)
{
	QString key;
	ControlResult error;
	if (!readBrowserFileArgument(args, &key, &error)) { return error; }

	const int buckets = qBound(1,
		args.value(QStringLiteral("buckets")).toInt(BrowserPeakCache::BaseBuckets),
		BrowserPeakCache::BaseBuckets);
	BrowserPeakCache::Answer answer;
	QString why;
	if (!BrowserPeakCache::instance().peaksFor(key, buckets, &answer, &why))
	{
		// A typed refusal, never an empty waveform: a flat line drawn for a file
		// nobody could read is a lie about the audio.
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no peak map for %1: %2").arg(key, why));
	}

	QJsonArray peaks;
	for (const BrowserPeak& peak : answer.peaks) { peaks.append(browserPeakJson(peak)); }

	QJsonObject result;
	result.insert(QStringLiteral("path"), key);
	result.insert(QStringLiteral("cached"), answer.cached);
	result.insert(QStringLiteral("buckets"), answer.buckets);
	result.insert(QStringLiteral("bucket_frames"), static_cast<double>(answer.bucketFrames));
	result.insert(QStringLiteral("frames"), static_cast<double>(answer.frames));
	result.insert(QStringLiteral("channels"), answer.channels);
	result.insert(QStringLiteral("sample_rate"), answer.sampleRate);
	result.insert(QStringLiteral("duration_seconds"), answer.sampleRate > 0
		? static_cast<double>(answer.frames) / answer.sampleRate : 0.0);
	result.insert(QStringLiteral("peaks"), peaks);
	result.insert(QStringLiteral("cache"), browserPeakCacheJson(BrowserPeakCache::instance()));
	return ControlResult::success(result);
}

//! The schema of every result of this half that reports the store's location.
QJsonObject withStoreSummary(QJsonObject properties)
{
	properties.insert(QStringLiteral("store_path"), stringProperty());
	properties.insert(QStringLiteral("store_loaded"), booleanProperty());
	properties.insert(QStringLiteral("store_error"), stringProperty());
	return properties;
}

} // namespace

void registerBrowserCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.roots");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("roots");
		cmd.description = QStringLiteral("The directories the file browser reads - the same "
			"tabs the sidebar has: the user's samples, the factory samples, the user and "
			"factory presets and projects - with a stable id per root, whether each exists on "
			"this machine, and the path of the tag store. Read-only.");
		cmd.argsSchema = objectSchema();
		cmd.resultSchema = objectSchema(withStoreSummary({
			{QStringLiteral("roots"), arrayProperty()},
			{QStringLiteral("count"), integerProperty(0, 64)},
			{QStringLiteral("ids"), arrayProperty()},
		}));
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return browserRootsHandler(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.query");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("query");
		cmd.description = QStringLiteral("Find files in the browser by name, by tag, or - "
			"with 'probe' - by what the audio file itself says it is (sample rate, channels, "
			"length, and the embedded title/artist/album/comment/genre tags). Every 'tags' "
			"entry must be present (AND). Sorted by path, so paging with offset is stable. "
			"The walk is bounded by scan_limit entries and the probe by probe_limit files, "
			"and a result that hit either bound says truncated: true. Read-only.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("text"), stringProperty()},
			{QStringLiteral("tags"), browserStringListProperty()},
			{QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("root"), stringProperty()},
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("probe"), booleanProperty()},
			{QStringLiteral("limit"), integerProperty(1, 500)},
			{QStringLiteral("offset"), integerProperty(0, 2147483647)},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("items"), browserItemsProperty(true)},
			{QStringLiteral("scanned"), integerProperty(0, BrowserMaxScanEntries)},
			{QStringLiteral("matched"), integerProperty(0, BrowserMaxScanEntries)},
			{QStringLiteral("returned"), integerProperty(0, 500)},
			{QStringLiteral("probed"), integerProperty(0, BrowserMaxProbes)},
			{QStringLiteral("truncated"), booleanProperty()},
			{QStringLiteral("probe"), booleanProperty()},
			{QStringLiteral("scan_limit"), integerProperty(0, BrowserMaxScanEntries)},
			{QStringLiteral("probe_limit"), integerProperty(0, BrowserMaxProbes)},
			{QStringLiteral("roots"), arrayProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return browserQueryHandler(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.tags");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("tags");
		cmd.description = QStringLiteral("The tag vocabulary of the library: every distinct "
			"tag with the number of files carrying it, how many files carry at least one tag, "
			"the store's own path, and whether the store could be read. Read-only.");
		cmd.argsSchema = objectSchema();
		cmd.resultSchema = objectSchema(withStoreSummary({
			{QStringLiteral("tags"), arrayProperty()},
			{QStringLiteral("distinct"), integerProperty(0, MaxDistinctTags)},
			{QStringLiteral("entries"), integerProperty(0, 2147483647)},
			{QStringLiteral("limit"), integerProperty(0, MaxDistinctTags)},
			{QStringLiteral("read_ok"), booleanProperty()},
		}));
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return browserTagsHandler(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("browser.peaks");
		cmd.group = QStringLiteral("browser");
		cmd.verb = QStringLiteral("peaks");
		cmd.description = QStringLiteral("The waveform peaks of an audio file: 'buckets' "
			"min/max pairs over the interleaved samples, the frames each bucket covers, the "
			"file's rate, channels and length, and whether the answer came out of the cache "
			"('cached') or off the disk. The cache is bounded (capacity entries, base_buckets "
			"per file) and its hit and miss counts are reported. Read-only: it writes no "
			"project state and nothing at all on disk.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("buckets"), integerProperty(1, BrowserPeakCache::BaseBuckets)},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("cached"), booleanProperty()},
			{QStringLiteral("buckets"), integerProperty(1, BrowserPeakCache::BaseBuckets)},
			{QStringLiteral("bucket_frames"), numberProperty()},
			{QStringLiteral("frames"), numberProperty()},
			{QStringLiteral("channels"), integerProperty(0, 1024)},
			{QStringLiteral("sample_rate"), integerProperty(0, 4294967)},
			{QStringLiteral("duration_seconds"), numberProperty()},
			{QStringLiteral("peaks"), arrayProperty()},
			{QStringLiteral("cache"), objectProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return browserPeaksHandler(args); };
		registry.registerCommand(cmd);
	}

	registerBrowserTagCommands(registry);
}

} // namespace lmms
