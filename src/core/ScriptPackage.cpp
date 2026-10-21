/*
 * ScriptPackage.cpp - script package layout for the LMMS/Zene Lua API
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

#include "ScriptPackage.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>

namespace lmms
{
namespace ScriptPackages
{

namespace
{

//! `--! lmms-package <name> [<version>]` anywhere in the first 4 KB.
const QRegularExpression& manifestExpression()
{
	static const QRegularExpression expression(
		QStringLiteral("^--!\\s*lmms-package\\s+(\\S+)(?:\\s+(\\S+))?\\s*$"),
		QRegularExpression::MultilineOption);
	return expression;
}

//! The manifest window: the first 4 KB of the entry script. Counted in BYTES,
//! because 4 KB is a size of the file, not a number of characters: a manifest
//! that starts past byte 4096 is outside the window however few characters it
//! takes to get there.
constexpr int ManifestScanBytes = 4096;

/*! The window of \a bytes the manifest may live in.
 *
 *  \a sourceContinues says whether the source has more bytes behind \a bytes.
 *  When it does, the window is cut back to the last COMPLETE line: a line the
 *  window cuts through is not a declaration, and matching half of one would
 *  report a truncated name or version.
 */
QByteArray manifestWindow(QByteArray bytes, bool sourceContinues)
{
	if (bytes.size() > ManifestScanBytes)
	{
		bytes.truncate(ManifestScanBytes);
		sourceContinues = true;
	}
	if (!sourceContinues)
	{
		return bytes;
	}
	const int lastNewline = bytes.lastIndexOf('\n');
	return bytes.left(lastNewline + 1);
}

//! Bytes read from package entry scripts. A test seam (see
//! entryScriptBytesReadForTesting()), not part of the package format.
std::atomic<qint64> s_entryScriptBytesRead{0};

/*! \brief Parse a manifest out of the raw bytes of an entry script.
 *
 *  \a sourceContinues says whether more of the file follows \a bytes; the
 *  window is then cut back to the last complete line.
 */
ScriptPackage parseManifestBytes(const QByteArray& bytes, bool sourceContinues)
{
	ScriptPackage package;
	const QRegularExpressionMatch match =
		manifestExpression().match(QString::fromUtf8(manifestWindow(bytes, sourceContinues)));
	if (!match.hasMatch())
	{
		return package;
	}
	const QString declaredName = match.captured(1);
	if (!isValidName(declaredName))
	{
		return package;
	}
	package.name = declaredName;
	package.version = match.captured(2);
	return package;
}

} // namespace

bool isValidName(const QString& name)
{
	if (name.isEmpty() || name.size() > 64)
	{
		return false;
	}
	// Lowercase letter first, then lowercase letters, digits, '_' or '-'.
	static const QRegularExpression expression(QStringLiteral("^[a-z][a-z0-9_-]*$"));
	return expression.match(name).hasMatch();
}

ScriptPackage parseManifest(const QString& source)
{
	// The limit is a limit on the BYTES of the source, so it is enforced on
	// the encoded bytes, before the text is decoded.
	const QByteArray bytes = source.toUtf8();
	return parseManifestBytes(bytes, bytes.size() > ManifestScanBytes);
}

QVector<ScriptPackage> scan(const QString& scriptsRoot)
{
	QVector<ScriptPackage> packages;
	if (scriptsRoot.isEmpty())
	{
		return packages;
	}

	const QDir root(scriptsRoot);
	const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QString& entry : entries)
	{
		if (!isValidName(entry))
		{
			continue;
		}
		const QString directory = root.absoluteFilePath(entry);
		const QString entryPath = QDir(directory).absoluteFilePath(
				QString::fromLatin1(EntryFileName));
		if (!QFileInfo(entryPath).isFile())
		{
			continue;
		}

		ScriptPackage package;
		package.name = entry;
		package.packageDir = directory;
		package.entryPath = entryPath;

		// The manifest is optional and may live in the entry script; a declared
		// name that disagrees with the directory is ignored rather than
		// trusted, because the directory is what a host lists and runs.
		QFile file(entryPath);
		if (file.open(QIODevice::ReadOnly))
		{
			// Only the manifest window is read: the manifest lives in the first
			// 4 KB, and an entry script may be arbitrarily large, so reading it
			// whole would make listing cost memory proportional to the file.
			const QByteArray head = file.read(ManifestScanBytes);
			s_entryScriptBytesRead.fetch_add(head.size(), std::memory_order_relaxed);
			const bool continues = file.size() > head.size();
			file.close();
			const ScriptPackage declared = parseManifestBytes(head, continues);
			if (declared.name == package.name)
			{
				package.version = declared.version;
			}
		}

		packages.append(package);
	}

	std::sort(packages.begin(), packages.end(),
			[](const ScriptPackage& a, const ScriptPackage& b) { return a.name < b.name; });
	return packages;
}

qint64 entryScriptBytesReadForTesting()
{
	return s_entryScriptBytesRead.load(std::memory_order_relaxed);
}

} // namespace ScriptPackages
} // namespace lmms
