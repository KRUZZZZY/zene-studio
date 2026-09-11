/*
 * ScriptApiVersion.cpp - build-time version of the LMMS/Zene Lua scripting API
 *
 * Copyright (c) 2026 LMMS contributors
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
} // namespace lmms
