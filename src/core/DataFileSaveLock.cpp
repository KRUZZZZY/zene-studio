/*
 * DataFileSaveLock.cpp - the "a save of this file is in flight" lock (cluster
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

#include "DataFileSaveLock.h"

#include <QLockFile>

#include "DataFile.h"
#include "SongEditor.h"

namespace lmms
{

namespace
{

//! Why \a lock could not be taken, for a refusal the user can act on.
QString refusalText(const QLockFile& lock)
{
	switch (lock.error())
	{
	case QLockFile::LockFailedError:
		return gui::SongEditor::tr("another instance of Zene Studio is saving it right now");
	case QLockFile::PermissionError:
		return gui::SongEditor::tr("the folder it is in refuses the lock file");
	default:
		return gui::SongEditor::tr("the lock file it needs could not be created");
	}
}

} // namespace

DataFileSaveLock::DataFileSaveLock(const QString& fileName)
	: m_fileName(fileName)
	, m_lock(std::make_unique<QLockFile>(lockPathFor(fileName)))
{
}

DataFileSaveLock::~DataFileSaveLock() = default;

QString DataFileSaveLock::lockPathFor(const QString& fileName)
{
	return fileName + QStringLiteral(".save-lock");
}

std::unique_ptr<DataFileSaveLock> DataFileSaveLock::tryAcquire(const QString& fileName,
	QString* refusal)
{
	std::unique_ptr<DataFileSaveLock> lock(new DataFileSaveLock(fileName));
	// Staleness is "the holder's process is gone" and nothing else: 0 disables
	// the age rule (the int overload, the only one Qt 5.15 has).
	lock->m_lock->setStaleLockTime(0);
	if (!lock->m_lock->tryLock(0))
	{
		if (refusal) { *refusal = refusalText(*lock->m_lock); }
		return nullptr;
	}
	return lock;
}

std::unique_ptr<DataFileSaveLock> DataFileSaveLock::tryAcquireForProject(const QString& projectPath,
	QString* refusal)
{
	const QString name = DataFile::normalisedName(DataFile::Type::SongProject, projectPath);
	std::unique_ptr<DataFileSaveLock> lock = tryAcquire(name, refusal);
	if (!lock && refusal)
	{
		*refusal = gui::SongEditor::tr("%1 (if no other instance is saving this project, delete %2)")
			.arg(*refusal, lockPathFor(name));
	}
	return lock;
}

} // namespace lmms
