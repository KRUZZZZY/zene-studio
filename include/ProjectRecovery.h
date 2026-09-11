/*
 * ProjectRecovery.h - decide whether an autosave recovery file may be offered
 *                     as the recovery of the project the user is opening
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_PROJECT_RECOVERY_H
#define LMMS_PROJECT_RECOVERY_H

#include <QDateTime>
#include <QString>

namespace lmms
{

/**
 * The autosave recovery API. A nested namespace, not a class: there is no state
 * to own, only one decision function and the small file helpers around it.
 */
namespace ProjectRecovery
{

/**
 * The autosave recovery file (`recover.mmp` in ConfigManager's working dir)
 * plus the identity sidecar this fork writes beside it.
 *
 * Upstream writes the recovery project file and offers it on the next launch
 * with no way to tell WHICH project it belongs to or whether it is newer than
 * the project on disk. A stale `recover.mmp` from an unrelated project is then
 * offered forever (it also suppresses "open last project" until discarded), and
 * launching with an explicit project on the command line can be hijacked by a
 * recovery of something else.
 *
 * The sidecar, `<recoveryFile>.info`, records the project the autosave came
 * from and when. It is a SIDE file: it never touches the project format a user
 * opens and its absence is not an error (an upstream-written recovery file has
 * none, and is still offered - see decideRecovery()).
 */
struct RecoveryInfo
{
	//! The recovery project file itself.
	bool fileExists = false;
	qint64 fileSize = 0;
	//! UTC; invalid when the file does not exist.
	QDateTime fileModified;

	//! A readable identity sidecar named the project the autosave came from.
	bool hasIdentity = false;
	//! Path recorded by the sidecar; empty when that project had no filename yet.
	QString sourceProject;
	//! Whether the file named by the sidecar still exists, and its UTC mtime.
	bool sourceProjectExists = false;
	QDateTime sourceProjectModified;
};

//! Why a recovery file is, or is not, offered.
enum class RecoveryVerdict
{
	//! No recovery file at all.
	NotPresent,
	//! Present but zero bytes - a truncated write holds nothing to recover.
	Empty,
	//! The project it came from is at least as new: recovering would go backwards.
	Stale,
	//! It belongs to a project other than the one this launch is opening.
	OtherProject,
	//! Offer it.
	Offer,
};

struct RecoveryDecision
{
	RecoveryVerdict verdict = RecoveryVerdict::NotPresent;
	//! Basename of the project the recovery belongs to; empty when unknown.
	QString projectLabel;
	//! One-line reason, for the startup log and bug reports.
	QString detail;

	bool offered() const { return verdict == RecoveryVerdict::Offer; }
};

//! Sidecar path for a recovery file: `<recoveryFile>.info`.
QString recoveryInfoPath(const QString& recoveryFile);

//! Name of a verdict, for logs and test failures.
const char* recoveryVerdictName(RecoveryVerdict verdict);

//! Serialize/parse the identity sidecar (plain text, one `key=value` per line).
QString formatRecoveryIdentity(const QString& sourceProject, const QDateTime& savedAtUtc);
bool parseRecoveryIdentity(const QString& text, QString& sourceProject, QDateTime& savedAtUtc);

//! Read the recovery file and its sidecar from disk. Never throws, never logs.
RecoveryInfo readRecoveryInfo(const QString& recoveryFile);

//! Write the identity sidecar (atomically). Returns false if it could not be written.
bool writeRecoveryIdentity(const QString& recoveryFile, const QString& sourceProject,
						const QDateTime& savedAtUtc);

//! Remove the recovery file and its sidecar. Returns true when the recovery file is gone.
bool removeRecovery(const QString& recoveryFile);

/**
 * The whole recovery decision, as a pure function of its inputs: no file system
 * access and no GUI, so it is exercised headless in
 * tests/src/core/ProjectRecoveryTest.cpp.
 *
 * \param projectBeingOpened the project this launch asked to open, or "" when it
 *        named none (then the recovery file IS the session to offer).
 *
 * A recovery file is offered only when it is this session's:
 * a recovery naming a DIFFERENT project is never offered as the recovery of the
 * project being opened, and one no newer than the project file it came from is
 * stale (the saved project already holds at least as much).
 */
RecoveryDecision decideRecovery(const RecoveryInfo& recovery, const QString& projectBeingOpened);

} // namespace ProjectRecovery

} // namespace lmms

#endif // LMMS_PROJECT_RECOVERY_H
