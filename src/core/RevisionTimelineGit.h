/*
 * RevisionTimelineGit.h - the PRIVATE seam between the revision timeline and
 *                        the one artefact it has to ask a subprocess about: the
 *                        project's own git history (feature-list row 76).
 *
 * Split out of RevisionTimeline.cpp for the same reason the command groups split
 * their read/edit halves: the file-length ratchet measures a file as a unit, and
 * the git half is the half that owns a child process, its timeout and its
 * absence handling. It is a src/core header, not an include/ one: no caller
 * outside this directory's own translation units uses it.
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

#ifndef LMMS_REVISION_TIMELINE_GIT_H
#define LMMS_REVISION_TIMELINE_GIT_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "RevisionTimeline.h"

namespace lmms
{
namespace control
{

/*! The git half of the timeline, private to src/core. No git executable, no
 *  repository, a failed command and a timeout are all the same answer to a
 *  caller - "no git entries", with the reason the report carries - so nothing
 *  here ever fails a timeline. */
namespace timelineGit
{

//! The commits that touched \a projectPath, newest first, each a "git:<short-sha>"
//! entry whose note is the commit's subject. \a report is filled either way.
QVector<RevisionEntry> entries(const QString& projectPath, QJsonObject* report);

/*! The bytes of one commit's copy of the project, through
 *  `git cat-file blob <sha>:./<name>`. The `<rev>:./<path>` form is relative to
 *  the command's own working directory, so no path has to be translated to the
 *  repository's root first. */
bool bytesOfCommit(const QString& projectPath, const QString& commit, QByteArray* bytes,
	QString* error);

} // namespace timelineGit

} // namespace control

} // namespace lmms

#endif // LMMS_REVISION_TIMELINE_GIT_H
