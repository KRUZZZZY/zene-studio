/*
 * ScriptImportWindows.h - Windows handle path checks (Q050-051)
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

#ifndef LMMS_SCRIPT_IMPORT_WINDOWS_H
#define LMMS_SCRIPT_IMPORT_WINDOWS_H

#ifdef _WIN32

#include <QDir>
#include <QFile>
#include <QString>

#include <io.h>
#include <windows.h>

#include <string>
#include <vector>

namespace lmms
{
namespace scriptimportwindows
{

//! Return the final DOS path reported by a Windows handle, without the extended prefix.
inline QString finalPathForHandle(HANDLE handle)
{
	std::vector<wchar_t> buffer(512);
	constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
	for (;;)
	{
		const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(),
				static_cast<DWORD>(buffer.size()), flags);
		if (length == 0)
		{
			return {};
		}
		if (length < buffer.size())
		{
			QString path = QString::fromWCharArray(buffer.data(), static_cast<int>(length));
			const QString extendedPrefix = QStringLiteral("\\\\?\\");
			if (path.startsWith(extendedPrefix, Qt::CaseInsensitive))
			{
				path.remove(0, extendedPrefix.size());
			}
			return QDir::fromNativeSeparators(path);
		}
		// The API returns the required size when the buffer is too small.
		if (length > 32768)
		{
			return {};
		}
		buffer.resize(static_cast<std::size_t>(length) + 1);
	}
}

//! Resolve both handles, so a junction/reparse point cannot hide an outside target.
inline bool openedModuleIsUnderRoot(const QFile& file, const QString& root, QString* error)
{
	const auto nativeHandle = _get_osfhandle(file.handle());
	if (nativeHandle == -1)
	{
		*error = QStringLiteral("cannot verify the opened module path; refused");
		return false;
	}
	const QString modulePath = finalPathForHandle(reinterpret_cast<HANDLE>(nativeHandle));
	if (modulePath.isEmpty())
	{
		*error = QStringLiteral("cannot verify the opened module path; refused");
		return false;
	}

	const std::wstring rootName = root.toStdWString();
	const HANDLE rootHandle = CreateFileW(rootName.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (rootHandle == INVALID_HANDLE_VALUE)
	{
		*error = QStringLiteral("cannot open the import root to verify its final path; refused");
		return false;
	}
	const QString rootPath = finalPathForHandle(rootHandle);
	CloseHandle(rootHandle);
	if (rootPath.isEmpty())
	{
		*error = QStringLiteral("cannot verify the import root path; refused");
		return false;
	}

	QString rootPrefix = rootPath;
	if (!rootPrefix.endsWith(QLatin1Char('/')))
	{
		rootPrefix += QLatin1Char('/');
	}
	if (!modulePath.startsWith(rootPrefix, Qt::CaseInsensitive))
	{
		*error = QStringLiteral("opened module resolves outside the import root; refused");
		return false;
	}
	return true;
}

} // namespace scriptimportwindows
} // namespace lmms

#endif // _WIN32

#endif // LMMS_SCRIPT_IMPORT_WINDOWS_H
