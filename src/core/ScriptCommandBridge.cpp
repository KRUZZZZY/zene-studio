/*
 * ScriptCommandBridge.cpp - zene.command(): every registered control command, from Lua
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

#include "ScriptCommandBridge.h"

#include <algorithm>

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include "ScriptDawBindings.h"
#include "ScriptEngine.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

#include "LuaBridge/LuaBridge.h"

namespace lmms::ScriptCommandBridge
{

namespace
{

//! Nesting beyond this is refused rather than recursed into (a self-referencing table).
constexpr int MaxDepth = 16;

const QStringList& refusedPrefixes()
{
	static const QStringList prefixes{QStringLiteral("script."), QStringLiteral("livecode."),
		QStringLiteral("crash."), QStringLiteral("telemetry."), QStringLiteral("feedback.")};
	return prefixes;
}

const QStringList& refusedIds()
{
	static const QStringList ids{QStringLiteral("control.quit"), QStringLiteral("settings.set"),
		QStringLiteral("audio.device_set")};
	return ids;
}

bool isPathKey(const QString& key)
{
	static const QRegularExpression pathKey(
		QStringLiteral("(^|_)(path|file|dir|folder|directory|out|output|destination)$"));
	return pathKey.match(key).hasMatch();
}

QJsonValue luaToJson(lua_State* L, int index, int depth);

//! A table whose keys are exactly 1..n is an array; any other table is an object.
bool isSequence(lua_State* L, int index)
{
	const auto length = static_cast<lua_Integer>(lua_rawlen(L, index));
	int keys = 0;
	lua_pushnil(L);
	while (lua_next(L, index) != 0)
	{
		lua_pop(L, 1);
		++keys;
	}
	return length > 0 && keys == length;
}

QJsonValue tableToJson(lua_State* L, int index, int depth)
{
	index = lua_absindex(L, index);
	if (depth > MaxDepth) { return QJsonValue(); }
	if (isSequence(L, index))
	{
		QJsonArray array;
		const auto length = static_cast<lua_Integer>(lua_rawlen(L, index));
		for (lua_Integer i = 1; i <= length; ++i)
		{
			lua_rawgeti(L, index, i);
			array.append(luaToJson(L, -1, depth + 1));
			lua_pop(L, 1);
		}
		return array;
	}
	QJsonObject object;
	lua_pushnil(L);
	while (lua_next(L, index) != 0)
	{
		if (lua_type(L, -2) == LUA_TSTRING) { object.insert(QString::fromUtf8(lua_tostring(L, -2)), luaToJson(L, -1, depth + 1)); }
		lua_pop(L, 1);
	}
	return object;
}

QJsonValue luaToJson(lua_State* L, int index, int depth)
{
	switch (lua_type(L, index))
	{
	case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
	case LUA_TNUMBER:
		return lua_isinteger(L, index) ? QJsonValue(static_cast<qint64>(lua_tointeger(L, index)))
			: QJsonValue(lua_tonumber(L, index));
	case LUA_TSTRING: return QString::fromUtf8(lua_tostring(L, index));
	case LUA_TTABLE: return tableToJson(L, index, depth);
	default: return QJsonValue();
	}
}

void pushJson(lua_State* L, const QJsonValue& value);

void pushArray(lua_State* L, const QJsonArray& array)
{
	lua_createtable(L, static_cast<int>(array.size()), 0);
	for (int i = 0; i < array.size(); ++i)
	{
		pushJson(L, array.at(i));
		lua_rawseti(L, -2, i + 1);
	}
}

void pushObject(lua_State* L, const QJsonObject& object)
{
	lua_createtable(L, 0, static_cast<int>(object.size()));
	for (auto it = object.begin(); it != object.end(); ++it)
	{
		pushJson(L, it.value());
		lua_setfield(L, -2, it.key().toUtf8().constData());
	}
}

void pushJson(lua_State* L, const QJsonValue& value)
{
	switch (value.type())
	{
	case QJsonValue::Bool: lua_pushboolean(L, value.toBool()); return;
	case QJsonValue::Double:
	{
		const double d = value.toDouble();
		const auto asInt = static_cast<lua_Integer>(d);
		if (static_cast<double>(asInt) == d) { lua_pushinteger(L, asInt); }
		else { lua_pushnumber(L, d); }
		return;
	}
	case QJsonValue::String: lua_pushstring(L, value.toString().toUtf8().constData()); return;
	case QJsonValue::Array: pushArray(L, value.toArray()); return;
	case QJsonValue::Object: pushObject(L, value.toObject()); return;
	default: lua_pushnil(L); return;
	}
}

int pushFailure(lua_State* L, const QString& message, const QString& kind)
{
	lua_pushnil(L);
	lua_pushstring(L, message.toUtf8().constData());
	lua_pushstring(L, kind.toUtf8().constData());
	return 3;
}

bool sandboxValue(const QString& key, QJsonValue& value, QString* error);

bool sandboxArray(const QString& key, QJsonArray& array, QString* error)
{
	for (int i = 0; i < array.size(); ++i)
	{
		QJsonValue item = array.at(i);
		if (!sandboxValue(key, item, error)) { return false; }
		array.replace(i, item);
	}
	return true;
}

bool sandboxValue(const QString& key, QJsonValue& value, QString* error)
{
	if (value.isObject())
	{
		QJsonObject object = value.toObject();
		if (!sandboxPaths(object, error)) { return false; }
		value = object;
		return true;
	}
	if (value.isArray())
	{
		QJsonArray array = value.toArray();
		if (!sandboxArray(key, array, error)) { return false; }
		value = array;
		return true;
	}
	if (!value.isString() || !isPathKey(key)) { return true; }
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(value.toString(), true, error);
	if (resolved.isEmpty()) { return false; }
	value = resolved;
	return true;
}

//! zene.command(id, args) -> reply | nil, message, kind
int luaCommand(lua_State* L)
{
	const QString id = QString::fromUtf8(luaL_checkstring(L, 1));
	QJsonObject args = lua_istable(L, 2) ? luaToJson(L, 2, 0).toObject() : QJsonObject();
	if (const QString why = refusalFor(id); !why.isEmpty()) { return pushFailure(L, why, QStringLiteral("refused")); }
	QString error;
	if (!sandboxPaths(args, &error)) { return pushFailure(L, error, QStringLiteral("refused")); }
	Request request{id, args};
	ScriptCommand command;
	command.type = ScriptCommand::Type::DawEdit;
	command.i0 = static_cast<std::int32_t>(ScriptDawBindings::Op::RegistryCall);
	command.object0 = &request;
	ScriptEngine::instance()->enqueue(command);
	// The read-back rule (ScriptDawEdit.cpp): the request is on this stack, so the script waits
	// until the apply side has run it.
	ScriptEngine::instance()->flushCommandsForRead();
	if (!request.result.ok)
	{
		return pushFailure(L, request.result.errorMessage, controlErrorKindName(request.result.errorKind));
	}
	pushObject(L, request.result.result);
	return 1;
}

//! zene.commands() -> { id, ... } (sorted)
int luaCommands(lua_State* L)
{
	QStringList ids = ControlRegistry::instance()->commandIds();
	ids.sort();
	lua_createtable(L, static_cast<int>(ids.size()), 0);
	for (int i = 0; i < ids.size(); ++i)
	{
		lua_pushstring(L, ids.at(i).toUtf8().constData());
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

} // namespace

QString refusalFor(const QString& id)
{
	const bool prefixed = std::any_of(refusedPrefixes().begin(), refusedPrefixes().end(),
		[&id](const QString& prefix) { return id.startsWith(prefix); });
	if (!prefixed && !refusedIds().contains(id)) { return QString(); }
	return QStringLiteral("'%1' is not available to scripts: it would leave the script sandbox "
		"(docs/LUA-COMPATIBILITY-POLICY.md section 5)").arg(id);
}

bool sandboxPaths(QJsonObject& args, QString* error)
{
	for (auto it = args.begin(); it != args.end(); ++it)
	{
		QJsonValue value = it.value();
		if (!sandboxValue(it.key(), value, error)) { return false; }
		it.value() = value;
	}
	return true;
}

void applyRequest(Request* request)
{
	if (request == nullptr) { return; }
	request->result = ControlRegistry::instance()->invoke(request->id, request->args);
}

void registerAll(lua_State* L)
{
	luabridge::getGlobalNamespace(L)
		.beginNamespace("zene")
			.addFunction("command", &luaCommand)
			.addFunction("commands", &luaCommands)
		.endNamespace();
}

} // namespace lmms::ScriptCommandBridge
