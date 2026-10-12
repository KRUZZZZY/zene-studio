/*
 * ScriptImport.cpp - sandboxed Lua `require` for script files (Q050-051)
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

#include "ScriptImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#ifndef _WIN32
#include <sys/stat.h>
#endif

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
}

namespace lmms
{

namespace
{

//! The registry table holding this run's loaded modules, name -> value.
constexpr const char* CacheKey = "zene.imports";

bool isIdentifierPart(const QString& part)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,63}$"));
	return pattern.match(part).hasMatch();
}

//! A clearer reason than "bad name" for the shapes an author might try.
QString classifyBadName(const QString& text)
{
	if (text.contains(QLatin1String("..")))
	{
		return QStringLiteral("'..' is not allowed in a module name");
	}
	const bool drive = text.size() >= 2 && text.at(1) == QLatin1Char(':')
			&& text.at(0).isLetter();
	if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('\\')) || drive)
	{
		return QStringLiteral("absolute paths are not allowed as a module name");
	}
	if (text.contains(QLatin1Char('/')) || text.contains(QLatin1Char('\\')))
	{
		return QStringLiteral("path separators are not allowed in a module name"
					" (use dots: lib.util)");
	}
	return QStringLiteral("a module name is identifiers joined by dots");
}

bool underRoot(const QString& root, const QString& path)
{
	return path.startsWith(root + QLatin1Char('/'));
}

#ifndef _WIN32
//! The open descriptor and the path must still be the same regular file.
bool sameFile(const QFile& file, const QString& canonicalPath)
{
	struct stat byDescriptor;
	struct stat byPath;
	if (::fstat(file.handle(), &byDescriptor) != 0
		|| ::stat(QFile::encodeName(canonicalPath).constData(), &byPath) != 0)
	{
		return false;
	}
	return S_ISREG(byDescriptor.st_mode) && byDescriptor.st_dev == byPath.st_dev
		&& byDescriptor.st_ino == byPath.st_ino;
}
#endif

//! Pcall'd helper: stack [name] -> [cached value or nil]. POD only.
int cacheLookup(lua_State* L)
{
	luaL_getsubtable(L, LUA_REGISTRYINDEX, CacheKey);
	lua_pushvalue(L, 1);
	lua_rawget(L, -2);
	return 1;
}

//! What runModule hands to moduleBody (lightuserdata, POD).
struct ModuleCall
{
	const char* code;
	std::size_t size;
	const char* chunk;
	const char* name;
	std::size_t nameSize;
	const char* path;
};

//! Pcall'd helper: load (text only), run, cache. POD only, so a Lua error
//! unwinding through it skips no C++ destructor.
int moduleBody(lua_State* L)
{
	const auto* call = static_cast<const ModuleCall*>(lua_touserdata(L, 1));
	if (luaL_loadbufferx(L, call->code, call->size, call->chunk, "t") != LUA_OK)
	{
		return lua_error(L);
	}
	lua_pushlstring(L, call->name, call->nameSize);
	lua_pushstring(L, call->path);
	lua_call(L, 2, 1);
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushboolean(L, 1);
	}
	luaL_getsubtable(L, LUA_REGISTRYINDEX, CacheKey);
	lua_pushlstring(L, call->name, call->nameSize);
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	return 1;
}

} // namespace


ScriptImport::ScriptImport(const QString& root)
{
	if (!root.isEmpty())
	{
		m_root = QFileInfo(root).canonicalFilePath();
	}
}


bool ScriptImport::parseModuleName(const QByteArray& name, QStringList* parts, QString* error)
{
	if (name.isEmpty() || name.contains('\0'))
	{
		*error = QStringLiteral("a module name must be non-empty and contain no NUL byte");
		return false;
	}
	if (name.size() > 128)
	{
		*error = QStringLiteral("module name is too long (limit 128 bytes)");
		return false;
	}
	const QString text = QString::fromUtf8(name);
	const QStringList split = text.split(QLatin1Char('.'));
	for (const QString& part : split)
	{
		if (!isIdentifierPart(part))
		{
			*error = classifyBadName(text);
			return false;
		}
	}
	*parts = split;
	return true;
}


bool ScriptImport::resolve(const QString& root, const QStringList& parts,
				QString* canonicalPath, QString* error)
{
	if (root.isEmpty())
	{
		*error = QStringLiteral("imports are unavailable: this script has no import root");
		return false;
	}
	const QString candidate = root + QLatin1Char('/') + parts.join(QLatin1Char('/'))
				+ QStringLiteral(".lua");
	const QString canonical = QFileInfo(candidate).canonicalFilePath();
	if (canonical.isEmpty() || !QFileInfo(canonical).isFile())
	{
		*error = QStringLiteral("module '%1' not found").arg(parts.join(QLatin1Char('.')));
		return false;
	}
	if (!underRoot(root, canonical))
	{
		*error = QStringLiteral("module '%1' resolves outside the import root")
				.arg(parts.join(QLatin1Char('.')));
		return false;
	}
	*canonicalPath = canonical;
	return true;
}


bool ScriptImport::readModule(const QString& root, const QString& canonicalPath,
				QByteArray* source, QString* error)
{
	QFile file(canonicalPath);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("cannot open module: %1").arg(file.errorString());
		return false;
	}
	// Re-check after open: the path must still resolve to the same place, and
	// (where the platform can say) to the very file we hold open.
	bool unchanged = QFileInfo(canonicalPath).canonicalFilePath() == canonicalPath
			&& underRoot(root, canonicalPath);
#ifndef _WIN32
	unchanged = unchanged && sameFile(file, canonicalPath);
#endif
	if (!unchanged)
	{
		*error = QStringLiteral("module changed while it was being opened; refused");
		return false;
	}
	if (file.size() > MaxModuleBytes)
	{
		*error = QStringLiteral("module is too large (limit %1 bytes)").arg(MaxModuleBytes);
		return false;
	}
	*source = file.read(MaxModuleBytes + 1);
	if (source->size() > MaxModuleBytes)
	{
		*error = QStringLiteral("module is too large (limit %1 bytes)").arg(MaxModuleBytes);
		return false;
	}
	return true;
}


void ScriptImport::install(lua_State* L)
{
	lua_pushlightuserdata(L, this);
	lua_pushcclosure(L, &ScriptImport::requireFunction, 1);
	lua_setglobal(L, "require");
}


int ScriptImport::requireFunction(lua_State* L)
{
	auto* self = static_cast<ScriptImport*>(lua_touserdata(L, lua_upvalueindex(1)));
	std::size_t length = 0;
	const char* text = luaL_checklstring(L, 1, &length);
	lua_settop(L, 1);
	char message[600];
	message[0] = '\0';
	int count = 0;
	{
		QByteArray error;
		count = self->require(L, QByteArray(text, static_cast<int>(length)), &error);
		if (count == -1)
		{
			qstrncpy(message, error.constData(), sizeof(message));
		}
	}
	if (count == -1)
	{
		return luaL_error(L, "%s", message);
	}
	if (count == -2)
	{
		return lua_error(L); // the module's own error value is on top
	}
	return count;
}


bool ScriptImport::prepare(const QByteArray& name, QString* canonical, QByteArray* source,
				QString* error)
{
	QStringList parts;
	if (!parseModuleName(name, &parts, error)
		|| !resolve(m_root, parts, canonical, error))
	{
		return false;
	}
	if (m_stack.contains(QString::fromUtf8(name)))
	{
		*error = QStringLiteral("import cycle: %1 -> %2")
				.arg(m_stack.join(QStringLiteral(" -> ")), QString::fromUtf8(name));
		return false;
	}
	if (m_stack.size() >= MaxDepth)
	{
		*error = QStringLiteral("imports nested deeper than %1 levels").arg(MaxDepth);
		return false;
	}
	if (m_loaded >= MaxModules)
	{
		*error = QStringLiteral("more than %1 modules imported in one run").arg(MaxModules);
		return false;
	}
	return readModule(m_root, *canonical, source, error);
}


int ScriptImport::runModule(lua_State* L, const QByteArray& name, const QByteArray& source,
				const QString& canonical)
{
	const QByteArray chunk = '@' + QFileInfo(canonical).fileName().toUtf8();
	const QByteArray path = canonical.mid(m_root.size() + 1).toUtf8();
	const ModuleCall call{source.constData(), static_cast<std::size_t>(source.size()),
				chunk.constData(), name.constData(),
				static_cast<std::size_t>(name.size()), path.constData()};
	m_stack.append(QString::fromUtf8(name));
	++m_loaded;
	lua_pushcfunction(L, &moduleBody);
	lua_pushlightuserdata(L, const_cast<ModuleCall*>(&call));
	const int status = lua_pcall(L, 1, 1, 0);
	m_stack.removeLast();
	return status == LUA_OK ? 1 : -2;
}


//! Returns the result count, -1 with \p error set, or -2 with a Lua error on top.
int ScriptImport::require(lua_State* L, const QByteArray& name, QByteArray* error)
{
	if (!lua_checkstack(L, 8))
	{
		*error = "stack overflow in require";
		return -1;
	}
	lua_pushcfunction(L, &cacheLookup);
	lua_pushvalue(L, 1);
	if (lua_pcall(L, 1, 1, 0) != LUA_OK)
	{
		return -2;
	}
	if (!lua_isnil(L, -1))
	{
		return 1;
	}
	lua_pop(L, 1);

	QString canonical;
	QByteArray source;
	QString reason;
	if (!prepare(name, &canonical, &source, &reason))
	{
		QByteArray shown = name.left(64);
		shown.replace('\0', '?');
		*error = (QByteArray("require '") + shown + "': " + reason.toUtf8()).left(500);
		return -1;
	}
	return runModule(L, name, source, canonical);
}

} // namespace lmms
