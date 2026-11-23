/*
 * DataFileSaveLock.h - the "a save of this file is in flight" lock (cluster
 * new-p10-89)
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

#ifndef LMMS_DATA_FILE_SAVE_LOCK_H
#define LMMS_DATA_FILE_SAVE_LOCK_H

#include <memory>
#include <QString>

#include "lmms_export.h"

class QLockFile;

namespace lmms
{

//! RAII ownership of <name>.save-lock, the sidecar that says a save of <name> is
//! running. A save writes <name>.new BEFORE it replaces <name>, so while it runs
//! a complete temp sits on disk that is not an orphan: a second instance
//! recovering it would take the temp the writer is about to publish, and a
//! restore or a second save would rotate the revision set and replace the file
//! from under the writer. Everything that changes <name> or its temps therefore
//! holds this lock for the whole window: a save (DataFile::writeFile, or the
//! caller that rotates revisions first and hands its lock in), a revision
//! restore, and recovery.
//!
//! The holder's pid is recorded by QLockFile, so a writer that CRASHED leaves a
//! lock whose pid is dead and the next tryAcquire() reclaims it: recovery stays
//! available for real orphans. The age rule is DISABLED (staleLockTime 0): a
//! lock held by a live process is never taken away because it is old, since a
//! save legitimately runs longer than QLockFile's 30 second default. The cost is
//! that a lock left by a writer that crashed on another host of a shared folder
//! is not reclaimed by time alone; the refusal names the sidecar so it can be
//! removed by hand, the safe direction for a save.
//!
//! Nobody waits: acquisition is one non-blocking attempt, because recovery runs
//! inside an open and a save that blocks a session is a save that hangs.
//! QLockFile is not re-entrant for one process, so code that already holds the
//! lock passes it down (DataFile::writeFile, Song::saveProjectFile) instead of
//! acquiring it again.
class LMMS_EXPORT DataFileSaveLock
{
public:
	~DataFileSaveLock();
	DataFileSaveLock(const DataFileSaveLock&) = delete;
	DataFileSaveLock& operator=(const DataFileSaveLock&) = delete;

	//! Take the lock of \a fileName, the NORMALISED name the save writes
	//! (DataFile::normalisedName). Returns null when it cannot be taken, and
	//! then \a refusal (when given) holds the reason, worded for the user:
	//! another live instance's save, a folder that refuses the lock file, or
	//! anything else - every reason is a refusal, never a write without the lock.
	static std::unique_ptr<DataFileSaveLock> tryAcquire(const QString& fileName,
		QString* refusal = nullptr);

	//! tryAcquire() for a caller that holds a project PATH rather than the name a
	//! save will write (project.save, a revision restore): the lock is taken for
	//! the name DataFile::writeFile() will use for it (a missing extension gets
	//! one), so every contender for one project contends for one sidecar. The
	//! refusal also names the sidecar, so a lock left by a crash on another host
	//! of a shared folder can be removed by hand.
	static std::unique_ptr<DataFileSaveLock> tryAcquireForProject(const QString& projectPath,
		QString* refusal = nullptr);

	//! The sidecar path for \a fileName.
	static QString lockPathFor(const QString& fileName);

	//! The file this lock guards.
	const QString& fileName() const { return m_fileName; }

private:
	explicit DataFileSaveLock(const QString& fileName);

	QString m_fileName;
	std::unique_ptr<QLockFile> m_lock;
};

} // namespace lmms

#endif // LMMS_DATA_FILE_SAVE_LOCK_H
