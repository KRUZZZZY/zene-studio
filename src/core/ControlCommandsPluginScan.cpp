/*
 * ControlCommandsPluginScan.cpp - the READ half of the plugin.* scan-cache and
 *                                 quarantine surface (SPEC A11-A16).
 *
 * Feature row 46 of docs/FEATURE-LIST-0.3.0.md ("Plugin scan cache and
 * quarantine"). The engine side is in the tree and proven: include/
 * PluginScanCache.h (the class the cache and the quarantine list are),
 * src/core/PluginScanCache.cpp (the JSON store), PluginFactory's scan that
 * fills it (src/core/PluginFactory.cpp - m_scanCache, planPluginScan(),
 * dropQuarantinedPlugins()) and the registered ctest PluginScanCacheTest. The
 * audit's row 46 says the group is missing "and the documented quarantine route
 * is hand-editing a JSON file" - so this half and its edit sibling exist to make
 * the cache and the quarantine OBSERVABLE and OPERABLE over --control-socket,
 * which is what retires the hand-edit route.
 *
 * THE ENUMERATION IS NEW, AND IT IS THE ONE ENGINE ADDITION. The cache could
 * already answer `fileCount()` and a fingerprint-checked `lookup(QFileInfo)`,
 * but it could not LIST what it holds - so "the cache's contents" was not
 * reachable by any client, and a report that could only say "how many" would
 * have been a count pretending to be the thing. PluginScanCache::records() and
 * PluginScanCache::record(path) were added for this (include/PluginScanCache.h,
 * with a case in tests/src/core/PluginScanCacheTest.cpp); the record's SHAPE
 * follows the cache's own serialiser (recordToJson, src/core/PluginScanCache.cpp)
 * rather than a second opinion about what a record is.
 *
 * WHAT THIS HALF DOES NOT DO, and why (the honest half of the row rather than a
 * trimmed deliverable): no command EDITS a cache record. A record is the
 * remembered RESULT of a scan; the only thing that may write one is a scan
 * (PluginFactory::discoverPlugins(), reached as `plugin.rescan` in the edit
 * half), which is what keeps the file a cache rather than a second, editable
 * source of truth about which plugins exist. The cache's own contract says the
 * same about itself: "Discovery results must never depend on the state of this
 * file - it only decides how much work a scan repeats."
 * docs/KNOWN-LIMITATIONS.md carries the absence line, including that nothing in
 * src/gui/ shows any of it.
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

#include "ControlCommandsPluginScanShared.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "PluginFactory.h"
#include "PluginScanCache.h"

namespace lmms
{

// ---------------------------------------------------------------------------
// the shared vocabulary of the two halves (declared in the shared header)
// ---------------------------------------------------------------------------

namespace control
{

PluginScanCache* scanCacheOrNull()
{
	PluginFactory* factory = getPluginFactory();
	if (factory == nullptr) { return nullptr; }
	return &factory->scanCache();
}

PluginFactory* pluginFactoryOrNull()
{
	return getPluginFactory();
}

QJsonObject scanRecordJson(const PluginScanRecord& record)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), record.filePath);
	out.insert(QStringLiteral("size"), double(record.size));
	out.insert(QStringLiteral("mtime"), double(record.mtimeMs));
	out.insert(QStringLiteral("status"),
		record.status == PluginScanRecord::Status::HasDescriptor
			? QStringLiteral("has-descriptor")
			: record.status == PluginScanRecord::Status::LoadFailed
				? QStringLiteral("load-failed")
				: QStringLiteral("not-a-plugin"));
	if (!record.error.isEmpty()) { out.insert(QStringLiteral("error"), record.error); }
	// The descriptor fields are recorded only for a plugin, exactly as the
	// cache's own serialiser writes them (recordToJson): a record that is not a
	// plugin has no name to report, and an empty one would read as a plugin
	// called "".
	if (record.status != PluginScanRecord::Status::HasDescriptor) { return out; }
	out.insert(QStringLiteral("name"), record.name);
	out.insert(QStringLiteral("display_name"), record.displayName);
	out.insert(QStringLiteral("description"), record.description);
	out.insert(QStringLiteral("author"), record.author);
	out.insert(QStringLiteral("version"), record.version);
	out.insert(QStringLiteral("type"), record.type);
	out.insert(QStringLiteral("supported_file_types"), record.supportedFileTypes);
	out.insert(QStringLiteral("logo_name"), record.logoName);
	out.insert(QStringLiteral("logo_has_inline_pixmap"), record.logoHasInlinePixmap);
	// True means the cache may never SERVE this file: rebuilding a sub-plugin
	// enumeration needs the library, so the scan loads it again next run
	// (PluginFactory::descriptorFromCacheRecord's rule).
	out.insert(QStringLiteral("has_sub_plugin_features"), record.hasSubPluginFeatures);
	return out;
}

QJsonObject scanStatsJson(const PluginFactory& factory)
{
	const PluginFactory::ScanStats& stats = factory.scanStats();
	QJsonObject out;
	out.insert(QStringLiteral("candidate_files"), stats.candidateFiles);
	out.insert(QStringLiteral("quarantined"), stats.quarantined);
	out.insert(QStringLiteral("served_from_cache"), stats.servedFromCache);
	out.insert(QStringLiteral("negative_from_cache"), stats.negativeFromCache);
	out.insert(QStringLiteral("scanned"), stats.scanned);
	out.insert(QStringLiteral("descriptors"), stats.descriptors);
	out.insert(QStringLiteral("quarantined_paths"),
		QJsonArray::fromStringList(stats.quarantinedPaths));
	out.insert(QStringLiteral("quarantined_reasons"),
		QJsonArray::fromStringList(stats.quarantinedReasons));
	out.insert(QStringLiteral("report"), factory.scanReport());
	return out;
}

QJsonObject quarantineEntryJson(const QString& path, const QString& reason)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), path);
	out.insert(QStringLiteral("reason"), reason);
	// Measured here, not by the cache: the list is a list of PATHS, and whether
	// the file is still there is what tells an operator that an entry has become
	// a no-op (a scan can only skip a file it found).
	out.insert(QStringLiteral("file_exists"), QFileInfo::exists(path));
	return out;
}

QJsonArray quarantineReport(const PluginScanCache& cache)
{
	QJsonArray entries;
	for (const auto& entry : cache.quarantineEntries())
	{
		entries.append(quarantineEntryJson(entry.first, entry.second));
	}
	return entries;
}

} // namespace control

// ---------------------------------------------------------------------------
// the read handlers
// ---------------------------------------------------------------------------

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

//! Everything a client needs about the cache FILE itself, so a refusal to write
//! (the edit half's) can be predicted from a read rather than discovered by one.
void appendCacheFileFields(const PluginScanCache& cache, QJsonObject& out)
{
	out.insert(QStringLiteral("cache_file"), cache.filePath());
	out.insert(QStringLiteral("persistent"), cache.isPersistent());
	out.insert(QStringLiteral("dirty"), cache.isDirty());
	out.insert(QStringLiteral("format_version"), PluginScanCache::s_formatVersion);
}

/*! plugin.scan_cache_get_state - the cache, the quarantine list and the last
 *  scan, in one report. Read-only.
 */
ControlResult handleScanCacheGetState()
{
	PluginScanCache* cache = scanCacheOrNull();
	if (cache == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("plugin.scan_cache_get_state: this instance has no plugin factory"));
	}
	const QJsonArray quarantine = quarantineReport(*cache);

	QJsonObject result;
	appendCacheFileFields(*cache, result);
	result.insert(QStringLiteral("entry_count"), cache->fileCount());
	result.insert(QStringLiteral("quarantine_count"), cache->quarantineCount());
	result.insert(QStringLiteral("quarantine"), quarantine);
	PluginFactory* factory = pluginFactoryOrNull();
	result.insert(QStringLiteral("last_scan"),
		factory == nullptr ? QJsonValue(QJsonValue::Null) : QJsonValue(scanStatsJson(*factory)));
	result.insert(QStringLiteral("note"),
		QStringLiteral("the quarantine list is what a scan SKIPS (PluginFactory::dropQuarantinedPlugins), "
			"so an edit here takes effect on the next scan - plugin.rescan runs one. The cache stores one "
			"record per file: a path plus the size and mtime it had, which is why a rebuilt or replaced "
			"library is scanned again instead of being served from a stale record. Nothing here is project "
			"state and nothing here is saved with the project: the file is derived, and a missing, corrupt "
			"or wrongly-versioned one degrades to a full scan rather than an error. Reading this may CONSTRUCT "
			"the plugin factory (PluginFactory::instance()), and the constructor runs the first scan at that "
			"moment - the same property plugin.list has always had"));
	return ControlResult::success(result);
}

//! One entry of the list, with the counters the caller reads it with.
void appendStatusCounts(const QList<PluginScanRecord>& records, QJsonArray& entries, QJsonObject& result)
{
	int withDescriptor = 0;
	int notAPlugin = 0;
	int loadFailed = 0;
	for (const PluginScanRecord& record : records)
	{
		entries.append(scanRecordJson(record));
		switch (record.status)
		{
		case PluginScanRecord::Status::HasDescriptor: { ++withDescriptor; break; }
		case PluginScanRecord::Status::NotAPlugin:    { ++notAPlugin; break; }
		case PluginScanRecord::Status::LoadFailed:    { ++loadFailed; break; }
		}
	}
	result.insert(QStringLiteral("count"), entries.size());
	result.insert(QStringLiteral("entries"), entries);
	result.insert(QStringLiteral("has_descriptor_count"), withDescriptor);
	result.insert(QStringLiteral("not_a_plugin_count"), notAPlugin);
	result.insert(QStringLiteral("load_failed_count"), loadFailed);
}

/*! plugin.scan_cache_list - every record the cache remembers, sorted by path.
 *  This is the cache's CONTENTS; scan_cache_get_state's entry_count is only how
 *  many of them there are. Read-only.
 */
ControlResult handleScanCacheList()
{
	PluginScanCache* cache = scanCacheOrNull();
	if (cache == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("plugin.scan_cache_list: this instance has no plugin factory"));
	}

	QJsonObject result;
	appendCacheFileFields(*cache, result);
	QJsonArray entries;
	appendStatusCounts(cache->records(), entries, result);
	result.insert(QStringLiteral("note"),
		QStringLiteral("a record means \"this file was looked at\", not \"this file is a plugin\": status "
			"'not-a-plugin' and 'load-failed' records are kept on purpose so a scan does not repeat a load "
			"that already failed. has_sub_plugin_features true means the record can never be SERVED (rebuilding "
			"those needs the library), so that file is loaded again next scan whatever this list says"));
	return ControlResult::success(result);
}

/*! The fingerprint comparison, as the report states it: `cached` is the
 *  engine's own served-from-cache answer (PluginScanCache::lookup, which needs
 *  path, size AND mtime to still match) and `stale` is "a record exists and the
 *  file no longer matches it", which is the reason a scan repeats work.
 */
void appendLookupFields(PluginScanCache& cache, const QString& path, QJsonObject& result)
{
	const QFileInfo file(path);
	const PluginScanRecord* stored = cache.record(path);
	// lookup() is the fingerprinted read; it is called with the file the CALLER
	// named, so this reports what the scanner would decide about that file now.
	const PluginScanRecord* served = cache.lookup(file);

	result.insert(QStringLiteral("file_exists"), file.exists());
	result.insert(QStringLiteral("cached"), served != nullptr);
	result.insert(QStringLiteral("stale"), stored != nullptr && served == nullptr);
	result.insert(QStringLiteral("record"),
		stored == nullptr ? QJsonValue(QJsonValue::Null) : QJsonValue(scanRecordJson(*stored)));
}

/*! plugin.scan_cache_lookup - what the cache remembers about ONE file, and
 *  whether it would still be trusted for it. Read-only. This is the read that
 *  answers "why is this plugin being scanned again?" without opening the JSON.
 */
ControlResult handleScanCacheLookup(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("plugin.scan_cache_lookup needs 'path': the path of a plugin file, as the cache "
				"stores it (absolute, exactly the path the scan saw)"));
	}
	PluginScanCache* cache = scanCacheOrNull();
	if (cache == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("plugin.scan_cache_lookup: this instance has no plugin factory"));
	}

	QJsonObject result;
	appendCacheFileFields(*cache, result);
	result.insert(QStringLiteral("path"), path);
	appendLookupFields(*cache, path, result);
	// The quarantine answer is part of the same question - "what does this cache
	// say about this file" - and is read from the list rather than inferred from
	// the record, because a path can be quarantined that was never scanned.
	const bool quarantined = cache->isQuarantined(path);
	result.insert(QStringLiteral("quarantined"), quarantined);
	result.insert(QStringLiteral("quarantine_reason"),
		quarantined ? cache->quarantineReason(path) : QString());
	return ControlResult::success(result);
}

} // namespace

void registerPluginScanCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.scan_cache_get_state");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cache_get_state");
		cmd.description = QStringLiteral("The plugin scan cache and its quarantine list: the cache file and "
			"whether it is persistent and dirty, how many records it holds, the quarantine entries with their "
			"reasons and whether the files are still there, and the last scan's own report (files found, "
			"quarantined, served from cache, known-bad skipped, scanned, descriptors). Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("persistent"), booleanProperty()},
			{QStringLiteral("dirty"), booleanProperty()},
			{QStringLiteral("format_version"), integerProperty()},
			{QStringLiteral("entry_count"), integerProperty()},
			{QStringLiteral("quarantine_count"), integerProperty()},
			{QStringLiteral("quarantine"), arrayProperty()},
			// {candidate_files, quarantined, served_from_cache, negative_from_cache,
			//  scanned, descriptors, quarantined_paths[], quarantined_reasons[], report}
			{QStringLiteral("last_scan"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleScanCacheGetState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.scan_cache_list");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cache_list");
		cmd.description = QStringLiteral("Every record the scan cache remembers, sorted by path: the file's "
			"fingerprint (path, size, mtime), its status (has-descriptor / not-a-plugin / load-failed) and, "
			"for a plugin, the descriptor metadata the scan resolved. Read-only; this is the cache's contents, "
			"where scan_cache_get_state only reports how many there are.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("count"), integerProperty()},
			{QStringLiteral("entries"), arrayProperty()},
			{QStringLiteral("has_descriptor_count"), integerProperty()},
			{QStringLiteral("not_a_plugin_count"), integerProperty()},
			{QStringLiteral("load_failed_count"), integerProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleScanCacheList(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.scan_cache_lookup");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cache_lookup");
		cmd.description = QStringLiteral("What the scan cache remembers about one plugin file: whether a "
			"record exists for it, whether that record would still be SERVED (the file's size and mtime must "
			"both still match - `cached`), whether it is stale (a record exists and no longer matches - "
			"`stale`), and whether the quarantine list hides it. Read-only.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("file_exists"), booleanProperty()},
			{QStringLiteral("cached"), booleanProperty()},
			{QStringLiteral("stale"), booleanProperty()},
			{QStringLiteral("quarantined"), booleanProperty()},
			{QStringLiteral("quarantine_reason"), stringProperty()},
			// null when there is no record at all.
			{QStringLiteral("record"), objectProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleScanCacheLookup(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
