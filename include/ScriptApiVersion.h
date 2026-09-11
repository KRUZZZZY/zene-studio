/*
 * ScriptApiVersion.h - build-time version of the LMMS/Zene Lua scripting API
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

#ifndef LMMS_SCRIPT_API_VERSION_H
#define LMMS_SCRIPT_API_VERSION_H

/*! \file
 * Build-time single source of truth for the Lua scripting API version.
 *
 * The numbers come from the top-level CMakeLists.txt (ZENE_LUA_API_VERSION_*,
 * passed as compile definitions by src/CMakeLists.txt). The #ifndef fallbacks
 * keep a translation unit compiled outside the lmmsobjs target - or against a
 * stale build directory - working instead of silently reporting a different
 * version. Bump MINOR for an additive change, MAJOR for a breaking one; what a
 * script can rely on is written down in docs/LUA-COMPATIBILITY-POLICY.md.
 */

#ifndef ZENE_LUA_API_VERSION_MAJOR
#define ZENE_LUA_API_VERSION_MAJOR 0
#endif
#ifndef ZENE_LUA_API_VERSION_MINOR
#define ZENE_LUA_API_VERSION_MINOR 1
#endif
#ifndef ZENE_LUA_API_VERSION_PATCH
#define ZENE_LUA_API_VERSION_PATCH 0
#endif

#define ZENE_LUA_API_STRINGIFY_(x) #x
#define ZENE_LUA_API_STRINGIFY(x) ZENE_LUA_API_STRINGIFY_(x)

//! "0.1" - the major.minor form a script declares in its `--! lmms-api` header.
#define ZENE_LUA_API_MAJOR_MINOR_STRING \
	ZENE_LUA_API_STRINGIFY(ZENE_LUA_API_VERSION_MAJOR) "." \
	ZENE_LUA_API_STRINGIFY(ZENE_LUA_API_VERSION_MINOR)

//! "0.1.0" - the full build version of the API.
#define ZENE_LUA_API_VERSION_STRING \
	ZENE_LUA_API_MAJOR_MINOR_STRING "." \
	ZENE_LUA_API_STRINGIFY(ZENE_LUA_API_VERSION_PATCH)

#include <QString>

namespace lmms
{

/*! Lua scripting API version, always read from the build.
 *
 *  Nothing in the engine may hardcode a version string: a call site that does
 *  cannot be found by grepping for the number, which is how a "0.2" engine once
 *  kept reporting "0.1". Tests compare the value the engine reports against the
 *  value the *test* translation unit was compiled with (both from the same
 *  CMake cache variable), so drift between the two compilation units fails.
 */
namespace ScriptApi
{

//! Major.minor, e.g. "0.1". What a script declares and the loader checks.
QString version();
//! Full major.minor.patch, e.g. "0.1.0".
QString fullVersion();
int major();
int minor();
//! Stability promise for this API generation, e.g. "v0-unstable".
QString stability();

} // namespace ScriptApi

} // namespace lmms

#endif // LMMS_SCRIPT_API_VERSION_H
