/*
 * ControlProjectAssets.h - what a project FILE references on disk, the content
 *                          hash of each of those references, and the rewrite
 *                          that points one of them at the file it found
 *                          (feature list row 38: project collection / archive,
 *                          hashing, relink).
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

#ifndef LMMS_CONTROL_PROJECT_ASSETS_H
#define LMMS_CONTROL_PROJECT_ASSETS_H

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ControlRegistry.h" // ControlResult
#include "lmms_export.h"

namespace lmms
{
namespace control
{

/*! One file a project file names, and everything this fork can say about it
 *  without loading the project.
 *
 *  A reference is a (element, attribute) pair the project FORMAT carries a path
 *  in - not a path the engine happens to have in memory - so the whole answer
 *  is available for a project that cannot be loaded at all: a file whose media
 *  is gone, whose plugins are missing, or whose session would refuse to open.
 *  That is the difference between this and `project.open`'s own error list,
 *  which already reports `Sample not found: <src>` for the two element kinds
 *  whose loadSettings collects one (SampleClip.cpp:537,
 *  AudioFileProcessor.cpp:227) and can only report what a LOAD reached.
 */
struct ProjectAssetReference
{
	//! Position in the project document, in document order. Stable across
	//! scans of one file, which is what makes it usable as a handle.
	int index = -1;
	//! The element that carries the reference ("sampleclip", "sf2player", ...).
	QString tag;
	//! The attribute the path is in ("src").
	QString attribute;
	//! The value EXACTLY as the project stores it, prefixes included. This is
	//! what `project.relink`'s `from` matches, so a caller never has to guess a
	//! normalised form.
	QString raw;
	//! The resolved absolute path; empty when the value names no file that can
	//! be placed on this filesystem.
	QString path;
	//! How the path was arrived at: "absolute" | "local:" | "project-dir" |
	//! "empty". Reported because `local:` and a legacy relative path resolve
	//! against different bases and a caller comparing two projects needs to
	//! know which rule was used.
	QString resolvedVia;
	bool resolved = false;
	//! The element also carries its media inline (SampleClip's `data`,
	//! AudioFileProcessor's `sampledata`). Such a reference names no file, so
	//! it is never missing - but it is reported, because "the sample is inside
	//! the project" is the answer to "why is nothing missing here".
	bool embedded = false;
	bool exists = false;
	bool hashed = false;
	qint64 bytes = -1;
	qint64 mtimeMs = -1;
	//! sha256 of the file at `path`, filled by controlHashProjectAssets().
	QString sha256;
	//! Why the reference is not usable, when it is not: "not on disk", "cannot
	//! be resolved", or the hash cap. Empty for a reference that is fine.
	QString error;

	//! What `project.missing_assets` reports: a reference that names a file,
	//! is not satisfied by inline media, and has nothing on disk at it.
	bool missing() const { return !embedded && !(resolved && exists); }
};

//! The whole answer for one project file.
struct ProjectAssetScan
{
	//! The project file that was read, as given.
	QString file;
	//! "mmp" (plain XML) or "mmpz" (the same XML, qCompress'd).
	QString format;
	QVector<ProjectAssetReference> references;
	//! sha256 over the reference set in document order, one line per reference
	//! ("<tag>\t<raw>\t<sha256|embedded|unhashed|missing>\n"). Two projects whose
	//! media is byte-identical have the same digest whatever the projects are named.
	//! "unhashed" is a reference that is on disk but not hashed (this verb hashes
	//! nothing, and project.hash_assets leaves an over-cap file unhashed); it is
	//! deliberately NOT the same state as "missing".
	QString digest;
	//! Elements that carry their media inline and therefore name no file.
	int embeddedCount = 0;

	int missingCount() const;
	int presentCount() const;
	int unresolvedCount() const;
};

//! One reference from the project to point at the file it found.
//! (The media itself is never copied, and no other attribute or element is
//! touched: this is the smallest edit that makes the reference resolve again.)
struct ProjectAssetRelink
{
	//! How many references changed (or would change, for a dry run).
	int replaced = 0;
	//! The raw values that changed, each with the value that replaced it.
	QStringList before;
	QStringList after;
	//! The file written. Empty for a dry run.
	QString path;
	//! sha256 of the document AFTER the rewrite (of the document that WOULD be
	//! written, for a dry run).
	QString sha256;
	//! The bytes written (or that would be written). The command layer keeps
	//! the PREVIOUS bytes for the recorded inverse; this is the other half.
	QByteArray bytes;
};

/*! Bounds. Both are refusals, not silent skips: a caller is told what was not
 *  done rather than handed a partial answer that looks complete.
 *
 *  A 10000-reference project is far past any real song (the largest project
 *  under data/projects carries well under a thousand), and the per-file hash
 *  cap is one GiB - a stem, not a session's worth of audio. A file over the cap
 *  is reported unhashed (`hashed == false`, `error` naming the cap), never
 *  quietly hashed for a while and then not. */
constexpr int MaxProjectAssetReferences = 10000;
constexpr qint64 MaxProjectAssetHashBytes = 1024LL * 1024LL * 1024LL;

/*! The largest project file these verbs read, and the largest expanded document
 *  they will inflate from a .mmpz. Same bound, same reason, and the same
 *  measured-first rule the engine's own document recovery uses
 *  (MaxRecoveredDocumentBytes, src/core/DataFile.cpp:202). The file on disk and
 *  the size a compressed body declares are measured first as refusals, and the
 *  bytes actually read or inflated are counted against this same cap. A declared
 *  size is a hint, not a bound: qUncompress grows its output past the header. The largest project under
 *  data/projects is about 200 kB; this is orders of magnitude above any real one. */
constexpr qint64 MaxProjectAssetDocumentBytes = 256LL * 1024LL * 1024LL;

/*! Read the project file at \a projectPath and report every reference it
 *  carries, with what is (or is not) on disk at each one. Writes nothing.
 *
 *  \a projectPath must be an existing .mmp or .mmpz file. The scan does not
 *  need the project to be the open one, does not need it to be loadable, and
 *  does not need a GUI.
 */
/*! 0.5.0 L12 (WF-049): project.collect_media's work without its undo step - every external file the
 *  project FILE @a project names copied into @a folderName beside it, the file rewritten to name the
 *  copies. For a copy of a project (a hand-off package), not the session's own file. */
LMMS_EXPORT bool collectProjectMediaInto(const QString& project, const QString& folderName, QJsonArray* copied,
	QJsonArray* failed, int* missing, ControlResult* error);

LMMS_EXPORT bool controlScanProjectAssets(const QString& projectPath, ProjectAssetScan* out,
	ControlResult* error);

/*! Inflates a qCompress() stream (4-byte big-endian expanded size, then zlib) into \a out, refusing
 *  as soon as the bytes PRODUCED would pass \a cap. False when the stream does not inflate, would pass the
 *  cap, or this build has no zlib (ZENE_HAVE_ZLIB): the caller then gets nothing, never an unbounded inflate.
 *  The declared size in the header is not trusted for the bound. */
bool inflateQtCompressedWithinCap(const QByteArray& stored, qint64 cap, QByteArray* out);

/*! controlScanProjectAssets() plus a sha256 per reference that resolves to a
 *  file on disk. Writes nothing to the project; only reads the media. */
LMMS_EXPORT bool controlHashProjectAssets(const QString& projectPath, ProjectAssetScan* out,
	ControlResult* error);

/*! Point every reference whose raw value is \a from - or whose resolved path is
 *  \a from - at the file \a to. Refuses when \a to is not a readable file, and
 *  when \a expectSha256 is not empty and the file at \a to does not hash to it
 *  (the hash `project.hash_assets` reported for the copy that was found).
 *
 *  \a dryRun reports what would change and writes nothing.
 *
 *  The document is re-serialised with the product's own serialiser
 *  (QDomDocument::save(stream, 2)) and the `<!DOCTYPE ...>` line is dropped at
 *  the output boundary, exactly as DataFile::write drops it
 *  (src/core/DataFile.cpp:330-350). Unlike a project.save this does NOT rename
 *  the root element to `zene-project` and does NOT touch the creator
 *  attributes: a relink is not a save, and a legacy root that is renamed here
 *  would be a change the caller did not ask for.
 */
LMMS_EXPORT bool controlRelinkProjectAsset(const QString& projectPath, const QString& from,
	const QString& to, const QString& expectSha256, bool dryRun, ProjectAssetRelink* out,
	ControlResult* error, const QString& storedValue = QString());

//! (\a storedValue, when given, is written instead of controlProjectAssetStoredPath(\a to) - how
//! project.collect_media stores a project-relative `local:` path, so the folder can move.)

/*! The relink ADDRESS arguments - `from` and `to`, both non-empty - checked
 *  before anything reads the project file. The command layer needs the same
 *  check before it captures the recorded inverse's bytes (SPEC A16), and it
 *  must see the SAME message: the identical junk arguments answered
 *  invalid_args with `dry_run` (nothing is read) and not_found without it (the
 *  missing project was read first) until this was one function.
 *
 *  A blank address is a statement about the ARGUMENTS, never about the project,
 *  so it is invalid_args wherever it is checked. */
LMMS_EXPORT bool controlRelinkAddressOk(const QString& from, const QString& to,
	ControlResult* error);

//! The value this product writes for \a path - PathUtil::toShortestRelative
//! with the same default the engine's own sample loading uses
//! (SampleBuffer::fromFile: the `factorysample:`/`usersample:` form when one of
//! those bases holds the file, the absolute path otherwise). One definition, so
//! a relink stores what the product would have stored had the sample been
//! picked from the browser.
LMMS_EXPORT QString controlProjectAssetStoredPath(const QString& path);

} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_PROJECT_ASSETS_H
