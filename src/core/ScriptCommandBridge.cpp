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
#include <cmath>

#include <QByteArray>
#include <QHash>
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

QString fromLuaString(lua_State* L, int index)
{
	size_t length = 0;
	const char* bytes = lua_tolstring(L, index, &length);
	return QString::fromUtf8(bytes, static_cast<qsizetype>(length));
}

void pushString(lua_State* L, const QString& value)
{
	const QByteArray utf8 = value.toUtf8();
	lua_pushlstring(L, utf8.constData(), static_cast<size_t>(utf8.size()));
}

const QStringList& refusedPrefixes()
{
	// shortcuts.* and workflow.* (0.5.0 L13; workflows were "macros" until design theme 19) write the
	// user's own configuration, as settings.set does; a workflow is also a run of commands the sandbox
	// has not looked at.
	static const QStringList prefixes{QStringLiteral("script."), QStringLiteral("livecode."),
		QStringLiteral("crash."), QStringLiteral("telemetry."), QStringLiteral("feedback."),
		QStringLiteral("shortcuts."), QStringLiteral("workflow.")};
	return prefixes;
}

const QStringList& refusedIds()
{
	static const QStringList ids{QStringLiteral("control.quit"), QStringLiteral("settings.set"),
		QStringLiteral("audio.device_set"), QStringLiteral("stem.model_download"),
		QStringLiteral("window.header_set"), QStringLiteral("window.layout_save"), QStringLiteral("window.layout_remove")};
	return ids;
}

//! Arguments that are paths under a name the key rule below does not recognise, per command
//! (taken from the registry's own schemas and descriptions; review L01-1 #1).
const QHash<QString, QStringList>& commandPathArgs()
{
	static const QHash<QString, QStringList> table{
		{QStringLiteral("project.diff"), {QStringLiteral("a"), QStringLiteral("b")}},
		{QStringLiteral("project.audible_diff"), {QStringLiteral("a"), QStringLiteral("b")}},
		{QStringLiteral("project.merge"), {QStringLiteral("base"), QStringLiteral("ours"), QStringLiteral("theirs"), QStringLiteral("report")}},
		{QStringLiteral("project.relink"), {QStringLiteral("to")}},
		{QStringLiteral("rack.zone_add"), {QStringLiteral("sample")}},
		{QStringLiteral("render.convert"), {QStringLiteral("files")}},  // the queue's sources: a script reads none outside
		{QStringLiteral("session.set_slot"), {QStringLiteral("source")}},
		{QStringLiteral("stem.job_start"), {QStringLiteral("source")}},
	};
	return table;
}

bool isPathKey(const QString& key)
{
	static const QRegularExpression pathKey(
		QStringLiteral("(^|_)(path|file|dir|folder|directory|out|output|destination|project|root|template)$"));
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
		if (lua_type(L, -2) == LUA_TSTRING) { object.insert(fromLuaString(L, -2), luaToJson(L, -1, depth + 1)); }
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
	case LUA_TSTRING: return fromLuaString(L, index);
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
		pushString(L, it.key());
		pushJson(L, it.value());
		lua_settable(L, -3);
	}
}

//! A whole number in lua_Integer's range as an integer, anything else as a number. The range is tested
//! BEFORE the cast: converting a double outside it (1e300) is undefined behaviour.
void pushNumber(lua_State* L, double d)
{
	constexpr double TwoTo63 = 9223372036854775808.0;
	if (d >= -TwoTo63 && d < TwoTo63 && std::trunc(d) == d) { lua_pushinteger(L, static_cast<lua_Integer>(d)); }
	else { lua_pushnumber(L, d); }
}

void pushJson(lua_State* L, const QJsonValue& value)
{
	switch (value.type())
	{
	case QJsonValue::Bool: lua_pushboolean(L, value.toBool()); return;
	case QJsonValue::Double: pushNumber(L, value.toDouble()); return;
	case QJsonValue::String: pushString(L, value.toString()); return;
	case QJsonValue::Array: pushArray(L, value.toArray()); return;
	case QJsonValue::Object: pushObject(L, value.toObject()); return;
	default: lua_pushnil(L); return;
	}
}

int pushFailure(lua_State* L, const QString& message, const QString& kind)
{
	lua_pushnil(L);
	pushString(L, message);
	pushString(L, kind);
	return 3;
}

bool sandboxValue(const QString& id, const QString& key, QJsonValue& value, QString* error);

bool sandboxArray(const QString& id, const QString& key, QJsonArray& array, QString* error)
{
	for (int i = 0; i < array.size(); ++i)
	{
		QJsonValue item = array.at(i);
		if (!sandboxValue(id, key, item, error)) { return false; }
		array.replace(i, item);
	}
	return true;
}

bool sandboxValue(const QString& id, const QString& key, QJsonValue& value, QString* error)
{
	if (value.isObject())
	{
		QJsonObject object = value.toObject();
		if (!sandboxPaths(id, object, error)) { return false; }
		value = object;
		return true;
	}
	if (value.isArray())
	{
		QJsonArray array = value.toArray();
		if (!sandboxArray(id, key, array, error)) { return false; }
		value = array;
		return true;
	}
	if (!value.isString() || !isPathArgument(id, key)) { return true; }
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(value.toString(), true, error);
	if (resolved.isEmpty()) { return false; }
	value = resolved;
	return true;
}

//! An empty Lua table is both {} and [], and tableToJson() reads it as {}: an argument @a id's schema declares
//! an array gets [] instead, so `follow_actions = {}` clears a chain (review L17 #9).
void emptyTablesAsArrays(const QString& id, QJsonObject& args)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr) { return; }
	const QJsonObject properties = command->argsSchema.value(QStringLiteral("properties")).toObject();
	for (auto it = args.begin(); it != args.end(); ++it)
	{
		const bool emptyTable = it.value().isObject() && it.value().toObject().isEmpty();
		const bool wantsArray = properties.value(it.key()).toObject().value(QStringLiteral("type")) == QLatin1String("array");
		if (emptyTable && wantsArray) { it.value() = QJsonArray(); }
	}
}

//! zene.command(id, args) -> reply | nil, message, kind
int luaCommand(lua_State* L)
{
	size_t idLength = 0;
	const char* idBytes = luaL_checklstring(L, 1, &idLength);
	const QString id = QString::fromUtf8(idBytes, static_cast<qsizetype>(idLength));
	if (lua_gettop(L) >= 2 && !lua_isnil(L, 2) && !lua_istable(L, 2))
	{
		return pushFailure(L, QStringLiteral("'%1' expects a table of arguments (or none), got %2")
			.arg(id, luaL_typename(L, 2)), QStringLiteral("invalid_args"));
	}
	QJsonObject args = lua_istable(L, 2) ? luaToJson(L, 2, 0).toObject() : QJsonObject();
	if (const QString why = refusalFor(id); !why.isEmpty()) { return pushFailure(L, why, QStringLiteral("refused")); }
	emptyTablesAsArrays(id, args);
	QString error;
	if (!sandboxPaths(id, args, &error)) { return pushFailure(L, error, QStringLiteral("refused")); }
	Request request{id, args};
	ScriptCommand command;
	command.type = ScriptCommand::Type::DawEdit;
	command.i0 = static_cast<std::int32_t>(ScriptDawBindings::Op::RegistryCall);
	command.object0 = &request;
	if (!ScriptEngine::instance()->enqueue(command))
	{
		// The bounded queue was full, so the request was DROPPED and will
		// never be applied: the reply below would be an empty failure that
		// says nothing. Say what happened instead.
		return pushFailure(L,
			QStringLiteral("the script command queue is full (%1 commands); the command was dropped")
				.arg(ScriptCommandQueueCapacity),
			QStringLiteral("refused"));
	}
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
		pushString(L, ids.at(i));
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

bool isPathArgument(const QString& id, const QString& key)
{
	return isPathKey(key) || commandPathArgs().value(id).contains(key);
}

bool sandboxPaths(const QString& id, QJsonObject& args, QString* error)
{
	for (auto it = args.begin(); it != args.end(); ++it)
	{
		QJsonValue value = it.value();
		if (!sandboxValue(id, it.key(), value, error)) { return false; }
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
