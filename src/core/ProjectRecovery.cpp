/*
 * ProjectRecovery.cpp - see ProjectRecovery.h
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

#include "ProjectRecovery.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace lmms
{

namespace ProjectRecovery
{

namespace
{

//! Identity sidecar format version. A sidecar this build does not understand is
//! treated as absent (the recovery file is then offered as upstream would).
constexpr int IdentityVersion = 1;

const QString VersionKey = QStringLiteral("version");
const QString ProjectKey = QStringLiteral("project");
const QString SavedKey = QStringLiteral("savedUTC");

//! A path as it is compared for "same project": absolute, cleaned, with symlinks
//! NOT resolved (the file may not exist, and a recovery must never be refused
//! because of a link).
QString comparablePath(const QString& path)
{
	if (path.isEmpty()) { return QString(); }
	return QFileInfo(path).absoluteFilePath();
}

//! One non-blank, non-comment `key=value` line of an identity sidecar.
struct IdentityField
{
	QString key;
	QString value;
	bool present = false;
};

//! Split a sidecar line. Only the FIRST '=' separates key from value, so a
//! Windows path or a filename containing '=' survives.
IdentityField splitIdentityField(const QString& rawLine)
{
	IdentityField field;

	const QString line = rawLine.trimmed();
	if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) { return field; }

	const int eq = line.indexOf(QLatin1Char('='));
	if (eq <= 0) { return field; }

	field.key = line.left(eq);
	field.value = line.mid(eq + 1);
	field.present = true;
	return field;
}

//! Fold one field into the parse in progress. False = malformed sidecar.
bool applyIdentityField(const IdentityField& field, int& version, bool& sawVersion,
					QString& project, bool& sawSaved, QDateTime& savedAtUtc)
{
	if (field.key == VersionKey)
	{
		bool ok = false;
		version = field.value.toInt(&ok);
		if (!ok) { return false; }
		sawVersion = true;
	}
	else if (field.key == ProjectKey) { project = field.value; }
	else if (field.key == SavedKey)
	{
		const QDateTime parsed = QDateTime::fromString(field.value, Qt::ISODate);
		if (!parsed.isValid()) { return false; }
		savedAtUtc = parsed.toUTC();
		sawSaved = true;
	}
	return true;
}

//! True when the recovery names a project other than the one being opened.
bool namesAnotherProject(const RecoveryInfo& recovery, const QString& projectBeingOpened)
{
	const QString wanted = comparablePath(projectBeingOpened);
	return !wanted.isEmpty() && wanted != comparablePath(recovery.sourceProject);
}

//! True when the project file already holds at least as much as the recovery.
bool recoveryIsStale(const RecoveryInfo& recovery)
{
	return recovery.sourceProjectExists && recovery.fileModified.isValid()
			&& recovery.sourceProjectModified.isValid()
			&& recovery.fileModified <= recovery.sourceProjectModified;
}

} // namespace

QString recoveryInfoPath(const QString& recoveryFile)
{
	return recoveryFile + QStringLiteral(".info");
}

const char* recoveryVerdictName(RecoveryVerdict verdict)
{
	switch (verdict)
	{
		case RecoveryVerdict::NotPresent:   return "NotPresent";
		case RecoveryVerdict::Empty:        return "Empty";
		case RecoveryVerdict::Stale:        return "Stale";
		case RecoveryVerdict::OtherProject: return "OtherProject";
		case RecoveryVerdict::Offer:        return "Offer";
	}
	return "?";
}

QString formatRecoveryIdentity(const QString& sourceProject, const QDateTime& savedAtUtc)
{
	QString text;
	text += QStringLiteral("# zene-studio autosave identity - a side file, never part of a project\n");
	text += VersionKey + QStringLiteral("=") + QString::number(IdentityVersion) + QStringLiteral("\n");
	// A path may contain '='; only the first one separates key from value.
	text += ProjectKey + QStringLiteral("=") + sourceProject + QStringLiteral("\n");
	text += SavedKey + QStringLiteral("=") + savedAtUtc.toUTC().toString(Qt::ISODate) + QStringLiteral("\n");
	return text;
}

bool parseRecoveryIdentity(const QString& text, QString& sourceProject, QDateTime& savedAtUtc)
{
	int version = 0;
	bool sawVersion = false;
	bool sawSaved = false;
	QString project;

	for (const QString& rawLine : text.split(QLatin1Char('\n')))
	{
		const IdentityField field = splitIdentityField(rawLine);
		if (!field.present) { continue; }
		if (!applyIdentityField(field, version, sawVersion, project, sawSaved, savedAtUtc))
		{
			return false;
		}
	}

	if (!sawVersion || version != IdentityVersion || !sawSaved) { return false; }

	sourceProject = project;
	return true;
}

RecoveryInfo readRecoveryInfo(const QString& recoveryFile)
{
	RecoveryInfo info;

	const QFileInfo recovery(recoveryFile);
	info.fileExists = recovery.exists() && recovery.isFile();
	if (!info.fileExists) { return info; }

	info.fileSize = recovery.size();
	info.fileModified = recovery.lastModified().toUTC();

	QFile sidecar(recoveryInfoPath(recoveryFile));
	if (sidecar.exists() && sidecar.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QString sourceProject;
		QDateTime savedAtUtc;
		if (parseRecoveryIdentity(QString::fromUtf8(sidecar.readAll()), sourceProject, savedAtUtc))
		{
			info.hasIdentity = true;
			info.sourceProject = sourceProject;

			if (!sourceProject.isEmpty())
			{
				const QFileInfo source(sourceProject);
				info.sourceProjectExists = source.exists() && source.isFile();
				if (info.sourceProjectExists)
				{
					info.sourceProjectModified = source.lastModified().toUTC();
				}
			}
		}
	}

	return info;
}

bool writeRecoveryIdentity(const QString& recoveryFile, const QString& sourceProject,
						const QDateTime& savedAtUtc)
{
	// QSaveFile: a crash mid-write leaves the previous sidecar (or none) rather
	// than a half-written one, and the recovery project file itself is untouched.
	QSaveFile sidecar(recoveryInfoPath(recoveryFile));
	if (!sidecar.open(QIODevice::WriteOnly | QIODevice::Text)) { return false; }

	const QByteArray payload =
		formatRecoveryIdentity(sourceProject, savedAtUtc).toUtf8();
	if (sidecar.write(payload) != payload.size())
	{
		sidecar.cancelWriting();
		return false;
	}

	return sidecar.commit();
}

bool removeRecovery(const QString& recoveryFile)
{
	QFile::remove(recoveryInfoPath(recoveryFile));
	QFile::remove(recoveryFile);
	return !QFileInfo::exists(recoveryFile);
}

RecoveryDecision decideRecovery(const RecoveryInfo& recovery, const QString& projectBeingOpened)
{
	RecoveryDecision decision;

	if (!recovery.fileExists)
	{
		decision.verdict = RecoveryVerdict::NotPresent;
		decision.detail = QStringLiteral("no recovery file");
		return decision;
	}

	if (recovery.fileSize <= 0)
	{
		decision.verdict = RecoveryVerdict::Empty;
		decision.detail = QStringLiteral("recovery file is empty (truncated write)");
		return decision;
	}

	if (recovery.hasIdentity && !recovery.sourceProject.isEmpty())
	{
		decision.projectLabel = QFileInfo(recovery.sourceProject).fileName();

		// 1. A recovery of a DIFFERENT project must never be offered as the
		//    recovery of the project this launch asked to open.
		if (namesAnotherProject(recovery, projectBeingOpened))
		{
			decision.verdict = RecoveryVerdict::OtherProject;
			decision.detail = QStringLiteral("recovery belongs to '%1', not to '%2'")
								.arg(decision.projectLabel,
									comparablePath(projectBeingOpened));
			return decision;
		}

		// 2. A recovery file no newer than the project file it came from is
		//    stale: the project on disk already holds at least as much, so
		//    recovering would go backwards. (Equal mtimes are treated as stale
		//    for the same reason.)
		if (recoveryIsStale(recovery))
		{
			decision.verdict = RecoveryVerdict::Stale;
			decision.detail = QStringLiteral("recovery of '%1' is not newer than the saved project "
										"(%2 <= %3)")
								.arg(decision.projectLabel,
									recovery.fileModified.toString(Qt::ISODate),
									recovery.sourceProjectModified.toString(Qt::ISODate));
			return decision;
		}

		decision.verdict = RecoveryVerdict::Offer;
		decision.detail = QStringLiteral("recovering '%1', autosaved %2")
							.arg(decision.projectLabel,
								recovery.fileModified.toString(Qt::ISODate));
		return decision;
	}

	// No identity (an upstream-written recovery file, a project with no filename
	// yet, or an unreadable sidecar): offer it exactly as upstream does. Not
	// knowing which project it is must not be a reason to throw a user's
	// unsaved session away.
	decision.verdict = RecoveryVerdict::Offer;
	decision.detail = QStringLiteral("recovery file with no project identity, autosaved %1")
						.arg(recovery.fileModified.toString(Qt::ISODate));
	return decision;
}

} // namespace ProjectRecovery

} // namespace lmms
