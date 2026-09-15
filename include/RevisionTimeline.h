/*
 * RevisionTimeline.h - the in-app revision timeline: ONE list over the revision
 *                      artefacts this engine ALREADY writes, and a restore that
 *                      reaches any of them (feature-list row 76, OWNER-31 item 30).
 *
 * WHY THIS IS A LIST AND NOT A STORE. Every artefact here was already on disk
 * before this file existed, written by a different subsystem for its own reason:
 *
 *   rotation  `<project>.rev0..rev2` - the A16 file-level revision set that
 *             `project.save` rotates before it overwrites a project file (the
 *             named 'keep-3' policy; include/ProjectRevisions.h).
 *   backup    `<project>.bak` - the copy DataFile::writeFile makes of the file
 *             it is about to replace, on EVERY save from the interface.
 *   autosave  `recover.mmp` + its `.info` identity sidecar - the periodic
 *             autosave, and `recover.mmp.bak` when the autosave overwrites one.
 *   git       the project's own history where it lives in a git repository
 *             (tools/mmpz-git is the filter/diff/merge tooling for that).
 *
 * WHAT IT IS NOT: a semantic diff. `compare` reports each document's element
 * counts and the difference between them, which is a structural summary a caller
 * can act on; the SEMANTIC diff of two project documents is tools/mmpz-git's
 * (`mmpz-git diff`), a Python tool outside this process, and this header does not
 * pretend to re-implement it. The same honesty applies to the timestamp: it is
 * the artefact's own UTC mtime, or the autosave sidecar's recorded `savedUTC`.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_REVISION_TIMELINE_H
#define LMMS_REVISION_TIMELINE_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "lmms_export.h"

namespace lmms
{

namespace control
{

//! The bounds every call in this file respects. Declared as data, not as
//! literals in the loops, because each one is part of the contract a caller reads
//! (and because a bound a test cannot name is a bound nobody can check).
struct RevisionTimelineBounds
{
	//! The most entries one timeline reports (the artefacts it can hold, plus
	//! the git history it is willing to list).
	static constexpr int MaxEntries = 24;
	//! The most commits one timeline reports from a project's own history.
	static constexpr int MaxGitEntries = 10;
	//! The most DIFFERING element tags `compare` reports. A project with 40
	//! differing tags is still compared; the tail is summarised by the counts.
	static constexpr int MaxComparedTags = 24;
	//! How long one `git` invocation may take before it is given up on. A
	//! control handler runs on the UI thread, so an unbounded child would stall
	//! the whole surface - the same reason ExternalProcessStemSeparator bounds
	//! its own probe.
	static constexpr int GitTimeoutMs = 2500;
	/*! The largest revision this file will READ or COPY, in bytes: the keep-3
	 *  policy's own per-revision cap (ProjectRevisionPolicy::MaxRevisionBytes),
	 *  reused rather than re-derived, because a revision set that can hold a
	 *  file the timeline refuses to read (or the other way round) is two
	 *  disagreeing contracts. Over this, a read is refused TYPED, never
	 *  truncated. */
	static constexpr qint64 MaxRevisionBytes = 8 * 1024 * 1024;
	//! The largest decompressed document `compare` will count, so a project
	//! compressed to a small container cannot expand without bound.
	static constexpr qint64 MaxDocumentBytes = 64 * 1024 * 1024;
};

//! One revision of a project, however it got there.
struct RevisionEntry
{
	//! The id a caller passes back to restore/compare it: "rev0".."rev2",
	//! "backup", "autosave", "autosave_prev" or "git:<short-sha>".
	QString id;
	//! Where it comes from: "rotation" | "backup" | "autosave" | "git".
	QString source;
	//! The artefact's own UTC time (the autosave sidecar's `savedUTC` where it
	//! has one, the file's mtime otherwise); invalid when neither exists.
	QDateTime timestamp;
	qint64 bytes = 0;
	//! The file the bytes live in. Empty for a git entry: its bytes come from
	//! the repository, not from a file (reading it would create one).
	QString path;
	//! sha256 of the raw bytes; empty for a git entry (hashing every listed
	//! commit would be one child process per entry - the commit sha IS its
	//! identity, and it is the id).
	QString sha256;
	//! The short commit a git entry names; empty otherwise.
	QString commit;
	//! A one-line human note, where the artefact carries one (a git commit's
	//! subject; the sidecar's recorded project for an autosave).
	QString note;
};

//! The compare baseline: the project file as it is on disk right now. Only
//! `compare` accepts it - a timeline lists revisions, and the live file is not
//! one until a save replaces it.
LMMS_EXPORT QString revisionLiveId();

//! The entries of \a projectPath's timeline, newest first. Never throws, never
//! logs; an artefact that cannot be read is reported through \a gitReport or
//! simply absent. \a recoveryFile is the autosave file to look at (the caller
//! passes ConfigManager::inst()->recoveryFile(), and a test its own), and the
//! `git` half runs only when \a includeGit and a `git` executable is present.
LMMS_EXPORT QVector<RevisionEntry> listProjectRevisions(const QString& projectPath,
	const QString& recoveryFile, bool includeGit, QJsonObject* gitReport);

//! \a entry on the wire.
LMMS_EXPORT QJsonObject revisionEntryJson(const RevisionEntry& entry);

//! The whole `revisions.list` body: the file, the count, the per-source counts,
//! the entries and the git half's own report.
LMMS_EXPORT QJsonObject revisionTimelineState(const QString& projectPath,
	const QString& recoveryFile, bool includeGit);

//! The entry \a id names, or an invalid entry (empty id) when nothing does.
LMMS_EXPORT RevisionEntry findRevision(const QString& projectPath,
	const QString& recoveryFile, const QString& id);

//! The raw bytes of \a id - "live" included. False with \a error set when the id
//! is unknown, its artefact is gone, or it is over MaxRevisionBytes.
LMMS_EXPORT bool readRevisionBytes(const QString& projectPath, const QString& recoveryFile,
	const QString& id, QByteArray* bytes, RevisionEntry* entry, QString* error);

/*! Restores \a id OVER \a projectPath's live file.
 *
 * A rotation id ("rev0".."rev2") is delegated to control::restoreProjectRevision,
 * the keep-3 policy's own function. Every other id is staged first, then the live
 * file is rotated into that set and the staged bytes are moved into place, so the
 * restore is ITSELF recoverable by restoring revision 0 - which is the inverse
 * control.undo dispatches. Nothing is written when the live file is over the
 * policy's per-revision cap (its rotation is refused) or when the source cannot
 * be read: a half-restored project is worse than a refusal.
 */
LMMS_EXPORT bool restoreTimelineRevision(const QString& projectPath, const QString& recoveryFile,
	const QString& id, QString* error);

/*! The structural comparison of two project documents: how many elements of each
 *  tag each side holds, how many elements in total, and which tags differ.
 *
 *  `identical` is a BYTE comparison of the two inputs (so two files that
 *  serialise the same music differently are not identical - LMMS' own writer is
 *  byte-stable, which is what makes the comparison worth reporting). A `.mmpz`
 *  container is decompressed first; a document that cannot be read at all is
 *  reported through `readable: false` rather than as "nothing differs". */
LMMS_EXPORT QJsonObject compareRevisionDocuments(const QByteArray& left, const QByteArray& right);

//! The XML text of a project document: \a raw itself when it is already text
//! (`.mmp`), its decompressed form when it is a qCompress container (`.mmpz`, the
//! shape DataFile::writeFile writes). Empty when it cannot be read or is over
//! MaxDocumentBytes.
LMMS_EXPORT QByteArray projectDocumentText(const QByteArray& raw);

} // namespace control

} // namespace lmms

#endif // LMMS_REVISION_TIMELINE_H
