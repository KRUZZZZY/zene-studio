/*
 * BrowserCatalog.cpp - the browser's items: the roots it reads, how a path is
 *                      grouped, and the query that finds one by name, tag or
 *                      metadata.
 *
 * The roots are the SAME directories the browser tabs read - the list is
 * src/gui/MainWindow.cpp's `new FileBrowser(...)` chain - so a query answers
 * about the library the user actually browses, and not about a parallel one
 * this feature invented. The grouping extensions mirror FileItem::defaultFilters()
 * in src/gui/FileBrowser.cpp:1324.
 *
 * The walk is bounded twice over, and both bounds are reported rather than
 * hidden: at most BrowserMaxScanEntries entries are examined, and at most
 * BrowserMaxProbes of them are OPENED to read metadata. Opening a file costs a
 * header parse; reading a name costs a stat, so a query that matched on names
 * never opens anything.
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

#include "BrowserCatalog.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPair>

#include "ConfigManager.h"

namespace lmms
{

namespace
{

//! How deep below a root a query looks. A sample library is a few levels deep;
//! an unbounded descent into a home directory is a denial of service on the
//! command socket, not a feature.
constexpr int kMaxDepth = 8;

struct KindRow
{
	const char* extension;
	BrowserKind kind;
};

//! The extensions the browser groups, from FileItem::defaultFilters(). Audio is
//! the superset libsndfile can open (the browser's own audio filter plus the
//! container names the newer library knows), preset/project/midi are the
//! browser's lists verbatim.
const KindRow kKinds[] = {
	{"wav", BrowserKind::Audio},
	{"wave", BrowserKind::Audio},
	{"ogg", BrowserKind::Audio},
	{"oga", BrowserKind::Audio},
	{"flac", BrowserKind::Audio},
	{"mp3", BrowserKind::Audio},
	{"opus", BrowserKind::Audio},
	{"aif", BrowserKind::Audio},
	{"aiff", BrowserKind::Audio},
	{"au", BrowserKind::Audio},
	{"caf", BrowserKind::Audio},
	{"raw", BrowserKind::Audio},
	{"voc", BrowserKind::Audio},
	{"spx", BrowserKind::Audio},
	{"ds", BrowserKind::Audio},
	{"xpf", BrowserKind::Preset},
	{"xiz", BrowserKind::Preset},
	{"lv2", BrowserKind::Preset},
	{"sf2", BrowserKind::Preset},
	{"sf3", BrowserKind::Preset},
	{"pat", BrowserKind::Preset},
	{"mmp", BrowserKind::Project},
	{"mmpz", BrowserKind::Project},
	{"mpt", BrowserKind::Project},
	{"mid", BrowserKind::Midi},
	{"midi", BrowserKind::Midi},
	{"rmi", BrowserKind::Midi},
};

//! A root and the id a client names it by. The ids are stable: an agent stores
//! them, so renaming one is an interface change.
struct RootRow
{
	const char* id;
	QString (*path)();
};

const RootRow kRoots[] = {
	{"samples", []() { return ConfigManager::inst()->userSamplesDir(); }},
	{"factory-samples", []() { return ConfigManager::inst()->factorySamplesDir(); }},
	{"presets", []() { return ConfigManager::inst()->userPresetsDir(); }},
	{"factory-presets", []() { return ConfigManager::inst()->factoryPresetsDir(); }},
	{"projects", []() { return ConfigManager::inst()->userProjectsDir(); }},
	{"factory-projects", []() { return ConfigManager::inst()->factoryProjectsDir(); }},
};

constexpr int kRootCount = static_cast<int>(sizeof(kRoots) / sizeof(kRoots[0]));

//! The wire names of the groups. One table, used in both directions, so a name
//! can never be accepted by the reader and unknown to the writer.
struct KindNameRow
{
	const char* name;
	BrowserKind kind;
};

const KindNameRow kKindNames[] = {
	{"audio", BrowserKind::Audio},
	{"preset", BrowserKind::Preset},
	{"project", BrowserKind::Project},
	{"midi", BrowserKind::Midi},
	{"other", BrowserKind::Other},
};

//! The state one query carries across its directories.
struct QueryContext
{
	const BrowserQuery* query = nullptr;
	QList<BrowserItem> matched;
	int scanned = 0;
	int probed = 0;
	bool truncated = false;
};

bool tagsMatch(const QStringList& present, const QStringList& wanted)
{
	for (const QString& wantedTag : wanted)
	{
		bool found = false;
		for (const QString& tag : present)
		{
			if (QString::compare(tag, wantedTag, Qt::CaseInsensitive) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found) { return false; }
	}
	return true;
}

bool textMatches(const QStringList& haystack, const QString& needle)
{
	for (const QString& text : haystack)
	{
		if (text.contains(needle)) { return true; }
	}
	return false;
}

BrowserItem makeItem(const QString& path, BrowserKind kind)
{
	const QFileInfo info(path);
	BrowserItem item;
	item.path = info.absoluteFilePath();
	item.name = info.fileName();
	item.kind = kind;
	item.sizeBytes = info.size();
	item.modifiedMs = info.lastModified().toMSecsSinceEpoch();
	return item;
}

/*! Considers one file: the cheap filters first, and a metadata probe only for a
 *  candidate whose name, path and tags did not already answer the query.
 */
void considerPath(const QString& path, BrowserKind kind, QueryContext* context)
{
	BrowserItem item = makeItem(path, kind);
	item.tags = BrowserTagStore::instance().tagsOf(path);
	if (!tagsMatch(item.tags, context->query->tags)) { return; }
	++context->scanned;

	const QString needle = context->query->text.trimmed().toLower();
	if (needle.isEmpty() || textMatches(item.searchableText(), needle))
	{
		context->matched.append(item);
		return;
	}
	if (!context->query->probe || context->probed >= BrowserMaxProbes) { return; }

	item.metadata = probeAudioMetadata(item.path);
	item.hasMetadata = true;
	++context->probed;
	if (item.metadata.valid && textMatches(item.metadata.searchableText(), needle))
	{
		context->matched.append(item);
	}
}

//! Every file under \a root, breadth-first, skipping hidden directories and
//! symlinked ones (a symlink loop is a walk that never ends).
QStringList filesUnder(const QString& root, int* remaining, bool* truncated)
{
	QStringList out;
	QList<QPair<QString, int>> queue{{root, 0}};
	while (!queue.isEmpty() && *remaining > 0)
	{
		const QPair<QString, int> head = queue.takeFirst();
		QDir dir(head.first);
		dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
		const QFileInfoList entries = dir.entryInfoList();
		for (const QFileInfo& entry : entries)
		{
			if (*remaining <= 0) { *truncated = true; break; }
			if (entry.isDir())
			{
				if (!entry.isSymLink() && head.second + 1 <= kMaxDepth
					&& !entry.fileName().startsWith(QLatin1Char('.')))
				{
					queue.append({entry.absoluteFilePath(), head.second + 1});
				}
				continue;
			}
			out.append(entry.absoluteFilePath());
			--(*remaining);
		}
	}
	if (!queue.isEmpty()) { *truncated = true; }
	return out;
}

//! The directories a query walks: the explicit path when it has one, else the
//! roots the query names (or every root that exists).
QStringList directoriesFor(const BrowserQuery& query, QStringList* walkedRoots)
{
	if (!query.path.isEmpty())
	{
		walkedRoots->append(query.path);
		return QStringList{query.path};
	}
	QStringList out;
	for (int i = 0; i < kRootCount; ++i)
	{
		if (!query.root.isEmpty() && query.root != QLatin1String(kRoots[i].id)) { continue; }
		QFileInfo info(kRoots[i].path());
		if (!info.isDir()) { continue; }
		walkedRoots->append(QString::fromLatin1(kRoots[i].id));
		out.append(info.absoluteFilePath());
	}
	return out;
}

} // namespace

QString browserKindName(BrowserKind kind)
{
	for (const KindNameRow& row : kKindNames)
	{
		if (row.kind == kind) { return QString::fromLatin1(row.name); }
	}
	return QStringLiteral("other");
}

bool browserKindFromName(const QString& name, BrowserKind* kind)
{
	for (const KindNameRow& row : kKindNames)
	{
		if (name == QLatin1String(row.name))
		{
			*kind = row.kind;
			return true;
		}
	}
	return false;
}

BrowserKind browserKindForPath(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	for (const KindRow& row : kKinds)
	{
		if (suffix == QLatin1String(row.extension)) { return row.kind; }
	}
	return BrowserKind::Other;
}

QList<BrowserRoot> browserRoots()
{
	QList<BrowserRoot> out;
	for (int i = 0; i < kRootCount; ++i)
	{
		BrowserRoot root;
		root.id = QString::fromLatin1(kRoots[i].id);
		root.path = QFileInfo(kRoots[i].path()).absoluteFilePath();
		root.exists = QFileInfo(kRoots[i].path()).isDir();
		out.append(root);
	}
	return out;
}

BrowserRoot browserRootById(const QString& id)
{
	for (const BrowserRoot& root : browserRoots())
	{
		if (root.id == id) { return root; }
	}
	return BrowserRoot();
}

QString browserCanonicalKey(const QString& path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString browserTagStorePath()
{
	const QString dir = ConfigManager::inst()->workingDir();
	if (dir.isEmpty()) { return QStringLiteral("browser-tags.json"); }
	return QDir(dir).filePath(QStringLiteral("browser-tags.json"));
}

QStringList BrowserItem::searchableText() const
{
	QStringList out{name.toLower(), path.toLower()};
	for (const QString& tag : tags) { out.append(tag.toLower()); }
	if (hasMetadata) { out.append(metadata.searchableText()); }
	return out;
}

BrowserQueryResult runBrowserQuery(const BrowserQuery& query)
{
	BrowserQueryResult result;
	QueryContext context;
	context.query = &query;

	BrowserKind kind = BrowserKind::Other;
	const bool kindFiltered = browserKindFromName(query.kind, &kind);

	int remaining = BrowserMaxScanEntries;
	for (const QString& directory : directoriesFor(query, &result.roots))
	{
		if (remaining <= 0) { result.truncated = true; break; }
		for (const QString& path : filesUnder(directory, &remaining, &context.truncated))
		{
			const BrowserKind pathKind = browserKindForPath(path);
			if (kindFiltered && pathKind != kind) { continue; }
			considerPath(path, pathKind, &context);
		}
	}

	std::sort(context.matched.begin(), context.matched.end(),
		[](const BrowserItem& a, const BrowserItem& b) { return a.path < b.path; });
	result.matched = context.matched.size();
	result.scanned = context.scanned;
	result.probed = context.probed;
	result.truncated = result.truncated || context.truncated;

	const int from = std::max(query.offset, 0);
	for (int i = from; i < result.matched && result.items.size() < std::max(query.limit, 0); ++i)
	{
		result.items.append(context.matched.at(i));
	}
	return result;
}

} // namespace lmms
