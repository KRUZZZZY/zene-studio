/*
 * ScriptApiVersion.cpp - build-time version of the LMMS/Zene Lua scripting API
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

#include "ScriptApiVersion.h"

// The class whose members are defined below, and the string/QRegularExpression
// pieces parseVersionHeader() reads the header with.
#include "ScriptEngine.h"

#include <QRegularExpression>
#include <QStringList>

namespace lmms
{
namespace ScriptApi
{

QString version()
{
	return QString::fromLatin1(ZENE_LUA_API_MAJOR_MINOR_STRING);
}

QString fullVersion()
{
	return QString::fromLatin1(ZENE_LUA_API_VERSION_STRING);
}

int major()
{
	return ZENE_LUA_API_VERSION_MAJOR;
}

int minor()
{
	return ZENE_LUA_API_VERSION_MINOR;
}

QString stability()
{
	// The promise (or lack of one) for this API generation. See
	// docs/LUA-COMPATIBILITY-POLICY.md: v0 carries no stability promise, and a
	// script that wants to depend on more must wait for v1.
	return QStringLiteral("v0-unstable");
}

} // namespace ScriptApi

/*! \brief The `--! zene-api <major>.<minor>` header's parser and
 *  compatibility check.
 *
 * These two are ScriptEngine members, and they live here because this is
 * the Lua API VERSION's translation unit: the number they compare against
 * is this file's own. Moved verbatim out of ScriptEngine.cpp, whose size
 * the 500-line ratchet grandfathers at 880 lines - the file may not grow
 * for code that belongs to the version it is compared against
 * (tests/file-length-gate.sh).
 */
QString ScriptEngine::parseVersionHeader(const QString& source)
{
	static const QRegularExpression header(
		QStringLiteral("^--!\\s*(?:zene|lmms)-api\\s+(\\d+\\.\\d+)\\s*$"),
		QRegularExpression::MultilineOption);
	const QRegularExpressionMatch match = header.match(source.left(4096));
	return match.hasMatch() ? match.captured(1) : QString();
}


bool ScriptEngine::isCompatibleVersion(const QString& version, QString* reason)
{
	const QStringList parts = version.split(QLatin1Char('.'));
	if (parts.size() != 2)
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("malformed version '%1'").arg(version);
		}
		return false;
	}
	bool okMajor = false;
	bool okMinor = false;
	const int major = parts[0].toInt(&okMajor);
	const int minor = parts[1].toInt(&okMinor);
	if (!okMajor || !okMinor)
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("malformed version '%1'").arg(version);
		}
		return false;
	}
	if (major != ScriptApi::major())
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("API major version %1 is not supported by this build"
					" (implements %2)").arg(major).arg(ScriptApi::version());
		}
		return false;
	}
	if (minor > ScriptApi::minor())
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("API version %1.%2 is newer than this build supports"
					" (%3)").arg(major).arg(minor).arg(ScriptApi::version());
		}
		return false;
	}
	return true;
}

} // namespace lmms
