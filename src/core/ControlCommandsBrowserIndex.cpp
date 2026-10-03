/*
 * ControlCommandsBrowserIndex.cpp - the library index: scan, status, indexed search and similar sounds
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
/*! 0.5.0 L11 (WF-012, WF-020): the library index (BrowserIndex.h) on the control surface.
 *  - browser.reindex scans the library's folders (or one folder) on a worker thread - or here, when the
 *    caller waits - measuring each audio file's features;
 *  - browser.index_status says what the index holds and whether a scan runs;
 *  - browser.index_query is browser.query over the index: the whole library, no walk, no scan bound;
 *  - browser.similar ranks the indexed sounds by how close their features are to one file's.
 *  The index is a cache of the library in the config directory, not project state: nothing is journalled. */

#include <algorithm>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include "BrowserIndex.h"
#include "BrowserSearch.h"
#include "ControlBrowserSupport.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;

namespace
{

//! Files a caller who waits may have measured; a background scan measures up to BrowserIndex::MaxAnalysed.
constexpr int WaitAnalysed = 500;

//! The folders @a args names: 'path', a root's id ('root'), or every library root that exists.
bool foldersOf(const QJsonObject& args, QStringList* dirs, ControlResult* error)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (!path.isEmpty())
	{
		if (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isDir())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("'path' must be an existing absolute folder"));
			return false;
		}
		*dirs << QDir(path).absolutePath();
		return true;
	}
	const QString root = args.value(QStringLiteral("root")).toString();
	for (const BrowserRoot& each : browserRoots())
	{
		if (each.exists && (root.isEmpty() || each.id == root)) { *dirs << each.path; }
	}
	if (dirs->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such library folder on this machine")); }
	return !dirs->isEmpty();
}

QJsonObject statusJson()
{
	const BrowserIndex::Status status = BrowserIndex::instance().status();
	return {{QStringLiteral("running"), status.running}, {QStringLiteral("scanned"), status.scanned}, {QStringLiteral("analysed"), status.analysed},
		{QStringLiteral("entries"), status.entries}, {QStringLiteral("truncated"), status.truncated},
		{QStringLiteral("built_ms"), static_cast<double>(status.builtMs)}, {QStringLiteral("dirs"), QJsonArray::fromStringList(status.dirs)},
		{QStringLiteral("error"), status.error}, {QStringLiteral("store_path"), BrowserIndex::instance().storePath()},
		{QStringLiteral("limit"), BrowserIndex::MaxEntries}};
}

ControlResult reindex(const QJsonObject& args)
{
	QStringList dirs;
	ControlResult error;
	if (!foldersOf(args, &dirs, &error)) { return error; }
	const bool features = args.value(QStringLiteral("features")).toBool(true);
	if (args.value(QStringLiteral("wait")).toBool(false))
	{
		if (!BrowserIndex::instance().scanNow(dirs, features, WaitAnalysed))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a scan is already running (browser.index_status follows it)"));
		}
		return ControlResult::success(statusJson());
	}
	QJsonObject result = statusJson();
	result.insert(QStringLiteral("started"), BrowserIndex::instance().start(dirs, features));
	return ControlResult::success(result);
}

BrowserItem itemOf(const BrowserIndexEntry& entry)
{
	BrowserItem item;
	item.path = entry.path;
	item.name = entry.name;
	item.kind = entry.kind;
	item.sizeBytes = entry.sizeBytes;
	item.modifiedMs = entry.modifiedMs;
	item.tags = BrowserTagStore::instance().tagsOf(entry.path);
	return item;
}

//! Whether @a item passes the kind and tag filters @a args sets.
bool passes(const BrowserItem& item, const QJsonObject& args)
{
	const QString kind = args.value(QStringLiteral("kind")).toString();
	if (!kind.isEmpty() && browserKindName(item.kind) != kind) { return false; }
	const QJsonArray tags = args.value(QStringLiteral("tags")).toArray();
	return std::all_of(tags.begin(), tags.end(), [&item](const QJsonValue& tag) { return item.tags.contains(tag.toString()); });
}

ControlResult indexQuery(const QJsonObject& args)
{
	BrowserTagStore::instance().load();
	const BrowserSearch search(args.value(QStringLiteral("text")).toString());
	const int limit = args.value(QStringLiteral("limit")).toInt(25);
	const int offset = args.value(QStringLiteral("offset")).toInt(0);
	QJsonArray items;
	int matched = 0;
	for (const BrowserIndexEntry& entry : BrowserIndex::instance().entries())
	{
		const BrowserItem item = itemOf(entry);
		if (!passes(item, args) || (!search.isEmpty() && !search.matches(item))) { continue; }
		if (matched++ >= offset && items.size() < limit) { items.append(browserItemJson(item, false)); }
	}
	QJsonObject result = statusJson();
	result.insert(QStringLiteral("items"), items);
	result.insert(QStringLiteral("matched"), matched);
	result.insert(QStringLiteral("returned"), items.size());
	return ControlResult::success(result);
}

QJsonObject featuresJson(const BrowserFeatures& f)
{
	return {{QStringLiteral("duration_seconds"), f.durationSeconds}, {QStringLiteral("loudness_db"), f.loudnessDb},
		{QStringLiteral("centroid_hz"), f.centroidHz}, {QStringLiteral("zero_crossings"), f.zeroCrossings}, {QStringLiteral("bpm"), f.bpm},
		{QStringLiteral("key_pitch_class"), f.keyPitchClass}};
}

//! @a key's features: the index's when it holds them for the file as it is, else measured now.
BrowserFeatures featuresOf(const QString& key, const std::vector<BrowserIndexEntry>& entries)
{
	const QFileInfo info(key);
	for (const BrowserIndexEntry& entry : entries)
	{
		if (entry.path == key && entry.features.valid && entry.sizeBytes == info.size()
			&& entry.modifiedMs == info.lastModified().toMSecsSinceEpoch()) { return entry.features; }
	}
	return BrowserFeatures::analyse(key);
}

ControlResult similar(const QJsonObject& args)
{
	QString key;
	ControlResult error;
	if (!readBrowserFileArgument(args, &key, &error)) { return error; }
	const std::vector<BrowserIndexEntry> entries = BrowserIndex::instance().entries();
	const BrowserFeatures target = featuresOf(key, entries);
	if (!target.valid) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 could not be read as audio").arg(key)); }
	std::vector<std::pair<double, const BrowserIndexEntry*>> ranked;
	for (const BrowserIndexEntry& entry : entries)
	{
		if (entry.features.valid && entry.path != key) { ranked.emplace_back(BrowserFeatures::distance(target, entry.features), &entry); }
	}
	if (ranked.empty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the library index holds no measured sounds yet: run "
			"browser.reindex (with features) first"));
	}
	const auto limit = static_cast<std::size_t>(args.value(QStringLiteral("limit")).toInt(10));
	std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	QJsonArray items;
	for (std::size_t i = 0; i < ranked.size() && i < limit; ++i)
	{
		QJsonObject item = browserItemJson(itemOf(*ranked[i].second), false);
		item.insert(QStringLiteral("distance"), ranked[i].first);
		item.insert(QStringLiteral("features"), featuresJson(ranked[i].second->features));
		items.append(item);
	}
	return ControlResult::success({{QStringLiteral("path"), key}, {QStringLiteral("features"), featuresJson(target)},
		{QStringLiteral("items"), items}, {QStringLiteral("compared"), static_cast<int>(ranked.size())}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QStringList& required, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("browser.") + QLatin1String(verb), description, objectSchema(args, QJsonArray::fromStringList(required)), false, handler, false);
}

} // namespace

void registerBrowserIndexCommands(ControlRegistry& registry)
{
	add(registry, "reindex", QStringLiteral("Scan the library into its index: every root that exists, one root ('root', an id from "
		"browser.roots) or one folder ('path', absolute). With 'features' (the default) each audio file is measured - length, "
		"loudness, brightness, noisiness, tempo and key - for browser.similar; a file whose size and date are unchanged keeps "
		"the features it had. The scan runs in the background (browser.index_status follows it) unless 'wait', which scans "
		"here and measures at most 500 files. A cache in the config directory, not the project: nothing is journalled."),
		{{QStringLiteral("root"), stringProperty()}, {QStringLiteral("path"), stringProperty()}, {QStringLiteral("features"), booleanProperty()},
			{QStringLiteral("wait"), booleanProperty()}}, {}, reindex);
	add(registry, "index_status", QStringLiteral("What the library index holds - entries, files measured, when it was built, the "
		"folders, whether a scan is running and how far it is - and where it is kept."), {}, {},
		[](const QJsonObject&) { return ControlResult::success(statusJson()); });
	add(registry, "index_query", QStringLiteral("browser.query over the library index - the whole library, without walking it: "
		"'text' (a substring or an expression - OR, -word, \"phrase\", name: path: tag: kind: ext:), 'tags' (all of them), "
		"'kind', 'limit' and 'offset'. As fresh as the last browser.reindex."),
		{{QStringLiteral("text"), stringProperty()}, {QStringLiteral("tags"), arrayProperty()}, {QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("limit"), integerProperty(1, 500)}, {QStringLiteral("offset"), integerProperty(0, 2147483647)}}, {}, indexQuery);
	add(registry, "similar", QStringLiteral("The indexed sounds most like the audio file 'path', closest first ('limit', 10 when left "
		"out): each with its distance (0 = the same numbers) and the features it was compared on. The file itself need not be "
		"indexed; the others must have been measured (browser.reindex)."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("limit"), integerProperty(1, 200)}}, {QStringLiteral("path")}, similar);
}

} // namespace lmms
