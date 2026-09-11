/*
 * ScriptLuaQtTypes.h - LuaBridge Stack specialisations for Qt value types
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

#ifndef LMMS_SCRIPT_LUA_QT_TYPES_H
#define LMMS_SCRIPT_LUA_QT_TYPES_H

/*! \file
 * LuaBridge needs a luabridge::Stack<> in every translation unit that pushes or
 * reads a Qt value type from Lua. Split out of ScriptBindings.cpp, which is a
 * grandfathered 1217-line source under the per-file length ratchet
 * (tests/file-length-gate.sh). Include this *after* LuaBridge/LuaBridge.h on
 * purpose: the specialisations must not be visible before the primary template.
 */

#include <QString>
#include <QStringList>

#include <cstddef>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

#include "LuaBridge/LuaBridge.h"

namespace luabridge
{

template<>
struct Stack<QString>
{
	static void push(lua_State* L, const QString& value)
	{
		const QByteArray utf8 = value.toUtf8();
		lua_pushlstring(L, utf8.constData(), static_cast<std::size_t>(utf8.size()));
	}

	static QString get(lua_State* L, int index)
	{
		std::size_t length = 0;
		const char* text = luaL_checklstring(L, index, &length);
		return QString::fromUtf8(text, static_cast<int>(length));
	}

	static bool isInstance(lua_State* L, int index) { return lua_type(L, index) == LUA_TSTRING; }
};

template<>
struct Stack<QStringList>
{
	static void push(lua_State* L, const QStringList& value)
	{
		lua_createtable(L, static_cast<int>(value.size()), 0);
		int index = 1;
		for (const QString& entry : value)
		{
			Stack<QString>::push(L, entry);
			lua_rawseti(L, -2, index++);
		}
	}

	static QStringList get(lua_State* L, int index)
	{
		QStringList result;
		if (!lua_istable(L, index)) { return result; }
		const int length = static_cast<int>(lua_rawlen(L, index));
		for (int i = 1; i <= length; ++i)
		{
			lua_rawgeti(L, index, i);
			result << Stack<QString>::get(L, -1);
			lua_pop(L, 1);
		}
		return result;
	}

	static bool isInstance(lua_State* L, int index) { return lua_istable(L, index); }
};

} // namespace luabridge

#endif // LMMS_SCRIPT_LUA_QT_TYPES_H
