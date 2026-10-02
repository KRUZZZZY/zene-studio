/*
 * ControlCommandsCollectMedia.cpp - project.collect_media: the media a project uses, copied beside it
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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "ControlDeviceSupport.h" // controlReadFileBytes / controlWriteFileBytes
#include "ControlEdit.h"
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

using namespace control;

namespace
{

QString fileSha256(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

//! Where @a source's copy goes in @a folder: its own file name, or `name (2).ext` ... when a
//! DIFFERENT file holds the name. A file with the same content is reused, not copied again.
QString destinationFor(const QDir& folder, const QString& source, const QString& sha256)
{
	const QFileInfo info(source);
	for (int n = 1; n < 1000; ++n)
	{
		const QString name = n == 1 ? info.fileName()
			: QStringLiteral("%1 (%2).%3").arg(info.completeBaseName()).arg(n).arg(info.suffix());
		const QString candidate = folder.filePath(name);
		if (!QFileInfo::exists(candidate) || fileSha256(candidate) == sha256) { return candidate; }
	}
	return QString();
}

bool insideFolder(const QString& path, const QDir& folder)
{
	return QFileInfo(path).absoluteFilePath().startsWith(folder.absolutePath() + QLatin1Char('/'));
}

//! Copy one reference's file and point the project at the copy; false (with @a why) when either
//! step fails. The copy is stored as `local:<folder>/<name>`, relative to the project.
bool collectOne(const QString& project, const QDir& projectDir, const QDir& folder,
	const ProjectAssetReference& ref, QString* to, QString* why)
{
	*to = destinationFor(folder, ref.path, ref.sha256);
	if (to->isEmpty()) { *why = QStringLiteral("no free name in the media folder"); return false; }
	if (!QFileInfo::exists(*to) && !QFile::copy(ref.path, *to)) { *why = QStringLiteral("the copy failed"); return false; }
	ProjectAssetRelink relink;
	ControlResult error;
	const QString stored = QStringLiteral("local:") + projectDir.relativeFilePath(*to);
	if (!controlRelinkProjectAsset(project, ref.raw, *to, QString(), false, &relink, &error, stored))
	{
		*why = error.errorMessage;
		return false;
	}
	return true;
}

ControlResult collectMedia(const QJsonObject& args)
{
	const QString project = args.value(QStringLiteral("project")).toString();
	const QDir projectDir = QFileInfo(project).absoluteDir();
	QString folderName = args.value(QStringLiteral("dir")).toString(QStringLiteral("media"));
	// An absolute folder is accepted when it is inside the project's folder (the Lua sandbox hands
	// `dir` over resolved); anything else must stay beside the project.
	if (QDir::isAbsolutePath(folderName) && insideFolder(folderName, projectDir))
	{
		folderName = projectDir.relativeFilePath(folderName);
	}
	if (folderName.isEmpty() || QDir::isAbsolutePath(folderName) || folderName.contains(QStringLiteral("..")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'dir' is a folder inside the project's folder, got '%1'").arg(folderName));
	}
	ControlResult error;
	ProjectAssetScan scan;
	if (!controlHashProjectAssets(project, &scan, &error)) { return error; }
	QByteArray previous;
	if (!controlReadFileBytes(project, &previous, &error)) { return error; }
	if (!projectDir.mkpath(folderName))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create '%1'").arg(folderName));
	}
	const QDir folder(projectDir.filePath(folderName));
	QJsonArray copied;
	QJsonArray failed;
	QSet<QString> done;
	for (const ProjectAssetReference& ref : scan.references)
	{
		if (ref.embedded || !ref.exists || ref.path.isEmpty() || done.contains(ref.raw)) { continue; }
		done.insert(ref.raw);
		if (insideFolder(ref.path, projectDir)) { continue; }  // already beside the project
		QString to;
		QString why;
		if (collectOne(project, projectDir, folder, ref, &to, &why))
		{
			copied.append(QJsonObject{{QStringLiteral("from"), ref.raw}, {QStringLiteral("to"), to}});
		}
		else { failed.append(QJsonObject{{QStringLiteral("from"), ref.raw}, {QStringLiteral("why"), why}}); }
	}
	QByteArray written;
	controlReadFileBytes(project, &written, &error);
	if (written != previous)
	{
		addUndoStep(
			[project, previous]() { ControlResult ignored; controlWriteFileBytes(project, previous, true, &ignored); },
			[project, written]() { ControlResult ignored; controlWriteFileBytes(project, written, true, &ignored); });
	}
	QJsonObject result{{QStringLiteral("project"), project}, {QStringLiteral("dir"), folder.absolutePath()},
		{QStringLiteral("copied"), copied}, {QStringLiteral("failed"), failed},
		{QStringLiteral("missing"), scan.missingCount()}};
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(QJsonObject{{QStringLiteral("project"), project}, {QStringLiteral("bytes"), previous.size()}},
			QStringLiteral("recorded action step: the project file's previous bytes"), QJsonObject(), true,
			QStringLiteral("action checkpoint (the project file's previous bytes, written back; the copies stay)")));
	return ControlResult::success(result);
}

} // namespace

void registerCollectMediaCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.collect_media");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("collect_media");
	cmd.description = QStringLiteral("Make a project portable: copy every file the project file "
		"references from outside its own folder into `dir` (default \"media\") beside it, and point "
		"each reference at its copy as a project-relative `local:` path, so the folder can be moved "
		"or shared whole. A copy whose content matches a file already there is reused; a name held "
		"by a different file gets ` (2)`. Missing media is reported, not copied. Reversible: one "
		"control.undo puts the project file's previous bytes back (the copies stay on disk).");
	cmd.argsSchema = objectSchema({{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}}, {QStringLiteral("project")});
	cmd.resultSchema = objectSchema({{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("dir"), stringProperty()}, {QStringLiteral("copied"), arrayProperty()},
		{QStringLiteral("failed"), arrayProperty()}, {QStringLiteral("missing"), integerProperty()}});
	cmd.mutating = true;
	cmd.handler = collectMedia;
	registry.registerCommand(cmd);
}

} // namespace lmms
