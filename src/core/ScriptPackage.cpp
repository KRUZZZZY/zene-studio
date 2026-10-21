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
//! The same 4 KB window ScriptEngine::parseVersionHeader() reads, so a manifest
//! that is too far down the file is missed by both, consistently.
const QRegularExpression& manifestExpression()
{
	static const QRegularExpression expression(
		QStringLiteral("^--!\\s*lmms-package\\s+(\\S+)(?:\\s+(\\S+))?\\s*$"),
		QRegularExpression::MultilineOption);
	return expression;
}

constexpr int ManifestScanBytes = 4096;

//! Bytes read from package entry scripts. A test seam (see
//! entryScriptBytesReadForTesting()), not part of the package format.
std::atomic<qint64> s_entryScriptBytesRead{0};

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
	ScriptPackage package;
	const QRegularExpressionMatch match = manifestExpression().match(source.left(ManifestScanBytes));
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
		if (file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
			const QByteArray source = file.readAll();
			s_entryScriptBytesRead.fetch_add(source.size(), std::memory_order_relaxed);
			const ScriptPackage declared = parseManifest(QString::fromUtf8(source));
			file.close();
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
