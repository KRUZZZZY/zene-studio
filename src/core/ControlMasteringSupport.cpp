/*
 * ControlMasteringSupport.cpp - the shared helpers of the `mastering.*` command
 *                              group (SPEC A11-A16). See the header for what
 *                              each one is for.
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

#include "ControlMasteringSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>

#include "BounceInPlace.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

//! The instance's memory of its own last run (see the header). Process-local
//! on purpose: it is the surface's record of a command's answer, not project
//! state, and loading another project must not resurrect it.
QJsonObject& lastRunStore()
{
	static QJsonObject store;
	return store;
}

//! True for a name this group's outputs carry: a .wav file, whatever the case
//! of the suffix.
bool isWavName(const QString& name)
{
	return name.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
}

} // namespace

QStringList masteringWavFiles(const QString& directory)
{
	const QDir dir(directory);
	if (!dir.exists())
	{
		return QStringList();
	}

	QStringList files;
	const QStringList entries = dir.entryList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const QString& name : entries)
	{
		if (isWavName(name))
		{
			files.append(dir.absoluteFilePath(name));
		}
	}
	return files;
}

bool captureMasteringWavDirectory(const QString& directory, QMap<QString, QByteArray>* capture,
	ControlResult* error)
{
	capture->clear();
	const QStringList files = masteringWavFiles(directory);

	// The bound is checked against the SIZES before a byte is read, so an
	// oversized directory is refused without loading any of it.
	qint64 total = 0;
	for (const QString& path : files)
	{
		total += QFileInfo(path).size();
	}
	if (total > MasteringCaptureLimitBytes)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already holds %2 bytes of wav files and this command's recorded "
				"inverse is bounded at %3: mastering.run is refused rather than performed "
				"without an inverse. Master into an empty directory, or move those files away "
				"first")
				.arg(QDir::toNativeSeparators(directory)).arg(total)
				.arg(MasteringCaptureLimitBytes));
		return false;
	}

	for (const QString& path : files)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot read %1 to record this command's inverse: %2")
					.arg(path, file.errorString()));
			return false;
		}
		capture->insert(path, file.readAll());
	}
	return true;
}

QJsonArray masteringFileFacts(const QStringList& paths)
{
	QJsonArray facts;
	for (const QString& path : paths)
	{
		const QFileInfo info(path);
		QJsonObject entry;
		entry.insert(QStringLiteral("path"), path);
		entry.insert(QStringLiteral("exists"), info.exists());
		entry.insert(QStringLiteral("bytes"), info.exists() ? info.size() : 0);
		// Measured from the file, not asserted: an empty hash means the file is
		// gone or unreadable, which is exactly what a reader must be able to see.
		entry.insert(QStringLiteral("sha256"),
			info.exists() ? BounceInPlace::sha256OfFile(path) : QString());
		facts.append(entry);
	}
	return facts;
}

QJsonObject masteringCaptureJson(const QString& directory, const QMap<QString, QByteArray>& before,
	const QStringList& created)
{
	QJsonArray held;
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("path"), it.key());
		entry.insert(QStringLiteral("bytes"), it.value().size());
		entry.insert(QStringLiteral("sha256"), controlSha256OfBytes(it.value()));
		held.append(entry);
	}

	QJsonArray createdNames;
	for (const QString& path : created) { createdNames.append(path); }

	QJsonObject out;
	out.insert(QStringLiteral("directory"), directory);
	out.insert(QStringLiteral("held_before"), held);
	out.insert(QStringLiteral("held_before_count"), held.size());
	out.insert(QStringLiteral("created"), createdNames);
	out.insert(QStringLiteral("created_count"), createdNames.size());
	out.insert(QStringLiteral("capture_limit_bytes"), MasteringCaptureLimitBytes);
	return out;
}

void recordMasteringUndo(const QStringList& created, const QMap<QString, QByteArray>& before)
{
	if (created.isEmpty() && before.isEmpty())
	{
		// Nothing on disk changed, so there is nothing to take back. Recording a
		// step here would cost the caller one Ctrl+Z for a no-op.
		return;
	}
	const QStringList createdCopy = created;
	const QMap<QString, QByteArray> beforeCopy = before;
	addUndoStep([createdCopy, beforeCopy]() {
		// 1. What the run created is REMOVED. This half is the whole inverse on
		//    a first run into an empty directory: a revision that never existed
		//    cannot be restored, only deleted.
		for (const QString& path : createdCopy) { QFile::remove(path); }
		// 2. What the run REPLACED is written back byte for byte. A file the run
		//    left untouched is rewritten with identical bytes, which is a no-op
		//    rather than a decision this step has to make.
		for (auto it = beforeCopy.constBegin(); it != beforeCopy.constEnd(); ++it)
		{
			QFile file(it.key());
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { continue; }
			file.write(it.value());
		}
	});
}

QJsonObject masteringLastRun()
{
	return lastRunStore();
}

void setMasteringLastRun(const QJsonObject& report)
{
	lastRunStore() = report;
}

bool readMasteringReportFile(const QString& path, QJsonObject* report,
	ControlResult* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mastering run wrote no report at %1").arg(path));
		return false;
	}
	const QByteArray bytes = file.readAll();
	QJsonParseError parse{};
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mastering run's report at %1 is not a JSON object: %2")
				.arg(path, parse.errorString()));
		return false;
	}
	*report = document.object();
	return true;
}

} // namespace control
} // namespace lmms
