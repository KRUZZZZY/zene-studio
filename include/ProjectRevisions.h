/*
 * ProjectRevisions.h - the bounded previous-revision set of a project file
 *                     (SPEC A16 deliverable 4: FILE-LEVEL REVERSIBILITY).
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

#ifndef LMMS_PROJECT_REVISIONS_H
#define LMMS_PROJECT_REVISIONS_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{
namespace control
{

/*! The named retention policy of a project file's previous revisions.
 *
 * Policy name: **keep-3**. Every write of an existing project file rotates the
 * revisions one slot and stores the file it is about to replace as
 * `<file>.rev0`:</br>
 *   `<file>.rev0`  the revision replaced by the last write
 *   `<file>.rev1`  the one before that
 *   `<file>.rev2`  the one before that
 *
 * DISK BOUND, two-sided. A single revision is never larger than
 * \c MaxRevisionBytes; a file larger than that is NOT copied (a truncated
 * project file is a corrupt revision, which is worse than none) and the
 * refusal is reported in the result rather than hidden. The whole set for one
 * project is therefore bounded by \c Keep * \c MaxRevisionBytes, and the
 * rotation trims the set to that bound.
 */
struct ProjectRevisionPolicy
{
	static constexpr int Keep = 3;
	static constexpr qint64 MaxRevisionBytes = 8 * 1024 * 1024;
};

//! The file a revision is stored in: "<file>.rev<n>".
LMMS_EXPORT QString projectRevisionPath(const QString& projectPath, int revision);

/*! Rotates \a projectPath's previous revisions and stores the current file as
 * revision 0. Returns the number of revisions now retained. When the file does
 * not exist nothing is rotated (a first save has no previous revision) and the
 * result says so through \a refusedReason. A file over MaxRevisionBytes is not
 * copied: \a refusedReason names that, and \a refused is set true.
 */
LMMS_EXPORT int rotateProjectRevision(const QString& projectPath, bool* refused,
	QString* refusedReason);

//! The retained revisions of \a projectPath: count, per-revision paths, sizes
//! and sha256, plus the policy name and the disk bound. This is what an agent
//! reads to see the recoverable set rather than assuming one.
LMMS_EXPORT QJsonObject projectRevisionState(const QString& projectPath);

/*! Restores revision \a revision of \a projectPath OVER the live file.
 *
 * The live file is rotated first (so the restore is itself recoverable: the
 * file it replaced is the new revision 0), then the revision is copied into
 * place. False with \a error set when the revision does not exist.
 */
LMMS_EXPORT bool restoreProjectRevision(const QString& projectPath, int revision,
	QString* error);

} // namespace control
} // namespace lmms

#endif // LMMS_PROJECT_REVISIONS_H
