/*
 * BrowserCatalog.h - the file browser's ENGINE half: the roots the browser
 *                    reads, what a browsed item is, the metadata an audio file
 *                    can be probed for, and the persisted tag store.
 *
 * The browser GUI (src/gui/FileBrowser.cpp) already has audition, favourites,
 * a Space preview and StringPairDrag drag-out. What it has never had is a way
 * to find anything by what a file IS rather than where it sits, and there is no
 * way at all to reach the browser from the control surface. This file is the
 * engine half of that: it reads the same directories the browser tabs read
 * (src/gui/MainWindow.cpp's `new FileBrowser(...)` list) and answers questions
 * about them, so `browser.*` commands can expose the result over the socket.
 *
 * It deliberately includes no Qt widget header: the catalog is a filesystem
 * fact, not a view of one. That is also what makes it testable headless.
 *
 * WHERE THE TAGS LIVE (the decision the release contract asks for). In a JSON
 * file in the user's config directory - `workingDir()/browser-tags.json` - and
 * NOT in the project file:
 *   - a tag describes the USER's library, not one project's content: the same
 *     kick sample is the same kick sample in every project, and a tag that
 *     vanished when a project was closed would be a different feature;
 *   - the browser's existing user state already lives there: the favourites tab
 *     is `ConfigManager::favoriteItems()`, read out of the same config file
 *     (src/gui/MainWindow.cpp:126);
 *   - `<mmp>`/`<mmpz>` is a versioned, upstream-compatible document format
 *     (tests/src/core/ProjectVersionTest.cpp, DataFileFormatTest.cpp). Adding a
 *     tag table to it would change the project format for state that is not
 *     project content, and would make a tagged sample library diverge from any
 *     other LMMS reading the file.
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

#ifndef LMMS_BROWSER_CATALOG_H
#define LMMS_BROWSER_CATALOG_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

//! One directory the file browser reads: a short id (the socket names it, so it
//! must not be a path), the directory, and whether it exists on this machine.
struct LMMS_EXPORT BrowserRoot
{
	QString id;
	QString path;
	bool exists = false;
};

//! How the browser groups a path. Derived from the file name's extension - the
//! browser's own directory listing rule - and never from the file's contents:
//! grouping 5000 files must not open 5000 files.
enum class BrowserKind
{
	Audio,
	Preset,
	Project,
	Midi,
	Other,
};

//! Wire name of \a kind ("audio", "preset", "project", "midi", "other").
LMMS_EXPORT QString browserKindName(BrowserKind kind);
//! Parses a wire name; false when it names no kind (the caller refuses typed).
LMMS_EXPORT bool browserKindFromName(const QString& name, BrowserKind* kind);
//! The kind of \a path, from its extension.
LMMS_EXPORT BrowserKind browserKindForPath(const QString& path);

//! Every root the browser reads, in the order the sidebar's tabs have them.
LMMS_EXPORT QList<BrowserRoot> browserRoots();
//! The root with this id, or a root with an empty path when there is none.
LMMS_EXPORT BrowserRoot browserRootById(const QString& id);

/*! What an audio file can be probed for: what the FILE ITSELF carries.
 *
 * Nothing here is invented and nothing is stored in a database of ours: the
 * numbers come from libsndfile's own header parse (the same library the engine
 * decodes with) and the strings are the file's embedded tags, read back with
 * `sf_get_string`. A file with no tags reports empty strings, and a format with
 * no tag support in the library reports empty strings too - both are facts
 * about the file, not errors.
 */
struct LMMS_EXPORT BrowserMetadata
{
	bool valid = false;
	//! Why the probe failed; empty when valid. A directory, a preset XML and a
	//! file libsndfile cannot read all land here with the library's own words.
	QString error;
	int sampleRate = 0;
	int channels = 0;
	qint64 frames = 0;
	double durationSeconds = 0.0;
	//! The container: "wav", "flac", "ogg", ... (libsndfile's major format).
	QString format;
	//! The encoding inside it: "pcm_16", "float", "vorbis", ...
	QString encoding;
	QString title;
	QString artist;
	QString album;
	QString comment;
	QString genre;
	QString date;
	QString software;
	QString copyright;

	//! Every non-empty field plus the numeric ones as text, lowercased: what a
	//! query string is matched against ("48000" finds a 48 kHz file).
	QStringList searchableText() const;
};

//! Probes \a path with libsndfile. Never throws and never opens a modal: an
//! unreadable file is a valid=false result with \a error set (when non-null).
LMMS_EXPORT BrowserMetadata probeAudioMetadata(const QString& path, QString* error = nullptr);

//! One browsed item: a file the browser would list, with what it knows about it.
struct LMMS_EXPORT BrowserItem
{
	QString path;
	QString name;
	BrowserKind kind = BrowserKind::Other;
	qint64 sizeBytes = 0;
	qint64 modifiedMs = 0;
	//! The persisted tags of this path (empty when it has none).
	QStringList tags;
	//! True when `metadata` was actually filled in (a query only probes when it
	//! was asked to, and only for the candidates that need it).
	bool hasMetadata = false;
	BrowserMetadata metadata;

	//! name + path + tags (+ the probed metadata when there is any), lowercased.
	QStringList searchableText() const;
};

//! Hard bound on the entries one query walks. A browser root can be a home
//! directory; an unbounded walk would hold the socket for minutes. A query that
//! hits this bound answers `truncated: true` rather than pretending it saw all.
constexpr int BrowserMaxScanEntries = 5000;
//! Hard bound on how many candidates one query may OPEN to match on metadata.
//! Opening a file is orders of magnitude dearer than reading its name.
constexpr int BrowserMaxProbes = 200;

//! A query over the browser's items.
struct LMMS_EXPORT BrowserQuery
{
	//! Case-insensitive substring, matched against name, path, tags and (when
	//! \a probe is set) the probed metadata. Empty matches everything.
	QString text;
	//! Every listed tag must be present on the item (AND, not OR).
	QStringList tags;
	//! A BrowserKind wire name, or empty for any kind.
	QString kind;
	//! A root id ("samples", "factory-presets", ...), or empty for every root.
	QString root;
	//! An absolute directory that REPLACES the roots. This is how a test - or an
	//! agent working outside the configured library - points the query at a
	//! directory of its own.
	QString path;
	//! Read audio metadata for the candidates whose name/tags did not match, so
	//! the query can match what the file SAYS it is.
	bool probe = false;
	int limit = 25;
	int offset = 0;
};

struct LMMS_EXPORT BrowserQueryResult
{
	QList<BrowserItem> items;
	//! Files examined (after the kind filter, before the text filter).
	int scanned = 0;
	//! How many of them matched, before offset/limit.
	int matched = 0;
	//! How many were OPENED to read metadata.
	int probed = 0;
	//! The walk stopped at BrowserMaxScanEntries: the answer is a prefix.
	bool truncated = false;
	//! What was walked: root ids, or the one explicit path.
	QStringList roots;
};

//! Runs \a query over the roots (or over `query.path`). Sorted by path, so the
//! answer is deterministic and a client can page through it with offset.
LMMS_EXPORT BrowserQueryResult runBrowserQuery(const BrowserQuery& query);

/*! The key a tag is filed under: the file's canonical path when it resolves
 * (so a symlink and its target are one entry), else its absolute path. */
LMMS_EXPORT QString browserCanonicalKey(const QString& path);
//! The store's default file: `<user config dir>/browser-tags.json`.
LMMS_EXPORT QString browserTagStorePath();

/*! The persisted tag store: path -> tags, in a JSON file in the user's config
 * directory (see the header comment for why not the project file).
 *
 * Written atomically (QSaveFile: a crash mid-write leaves the previous file
 * intact, not a truncated one) and read back on demand. Every write is a
 * `browser.tag.add`/`browser.tag.remove` call, so a failure to persist is
 * reported to the caller as a refusal rather than silently dropped: an agent
 * that believes it tagged a file must be able to tell that it did not.
 *
 * Thread affinity: the control registry runs handlers on the UI thread, and
 * nothing on the audio thread touches this. It is not thread-safe and does not
 * pretend to be.
 */
class LMMS_EXPORT BrowserTagStore
{
public:
	static BrowserTagStore& instance();

	//! The file the store persists to.
	QString storePath() const;
	//! Points the store at \a path and loads it. A missing file is an empty
	//! store (false is returned only when the file exists but cannot be read).
	bool openAt(const QString& path);
	//! Re-reads storePath().
	bool load();
	//! Writes storePath() atomically. False + lastError() when it cannot.
	bool save() const;

	QStringList tagsOf(const QString& file) const;
	bool has(const QString& file, const QString& tag) const;
	//! Files known to the store (a tagged file whose tags were all removed is
	//! dropped, so this counts files that still carry at least one tag).
	int entryCount() const;
	//! Distinct tags across the library, sorted.
	QStringList allTags() const;
	//! How many files carry \a tag.
	int filesWithTag(const QString& tag) const;

	//! Adds \a tag to \a file. False when the file already carries it (an edit
	//! that changes nothing is not an edit) or when the store cannot be
	//! persisted. \a error receives the reason in both cases.
	bool add(const QString& file, const QString& tag, QString* error = nullptr);
	//! Removes \a tag from \a file. False when the file does not carry it.
	bool remove(const QString& file, const QString& tag, QString* error = nullptr);

	//! Why the last load()/save() failed; empty when it did not.
	QString lastError() const;
	//! Empties the in-memory store. It does NOT touch the file (the tests use it
	//! to prove a write really reached the file rather than the cache).
	void clear();

private:
	BrowserTagStore() = default;

	QHash<QString, QStringList> m_tags;
	QString m_path;
	mutable QString m_error;
};

} // namespace lmms

#endif // LMMS_BROWSER_CATALOG_H
