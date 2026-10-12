/*
 * ScriptImport.h - sandboxed Lua `require` for script files (Q050-051)
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

#ifndef LMMS_SCRIPT_IMPORT_H
#define LMMS_SCRIPT_IMPORT_H

#include <QString>
#include <QStringList>

struct lua_State;

namespace lmms
{

/*! \brief The import root and bookkeeping of one script run.
 *
 *  A script file may `require("util")` or `require("lib.util")`, which loads
 *  `<root>/util.lua` / `<root>/lib/util.lua`. The root is the directory of the
 *  entry script (the package directory for a package). Nothing outside the
 *  canonical root can be loaded: names are restricted to identifier components,
 *  the file's canonical path (symlinks resolved) must lie under the canonical
 *  root, and the opened file is re-checked against the path after open.
 *
 *  Lives on the worker's stack for one run; Lua reaches it through the
 *  `require` closure's upvalue. Each run has its own module cache.
 */
class ScriptImport
{
public:
	static constexpr int MaxDepth = 16;                 //!< nested imports
	static constexpr int MaxModules = 64;               //!< distinct modules per run
	static constexpr qint64 MaxModuleBytes = 256 * 1024; //!< one module's source

	//! \param root the import root; made canonical here. Empty/missing = refuse all.
	explicit ScriptImport(const QString& root);

	//! Install `require` in the globals of \p L. Call after the sandbox opened.
	void install(lua_State* L);

	//! Validate a module name and map it to path components. Pure.
	//! Returns false with a one-line reason for anything but identifier parts
	//! joined by single dots (separators, `..`, absolute and Windows paths, NUL).
	static bool parseModuleName(const QByteArray& name, QStringList* parts, QString* error);

	//! Resolve \p parts under the canonical \p root to a canonical file path.
	//! Refuses a result outside the root (symlink escape) or a non-regular file.
	static bool resolve(const QString& root, const QStringList& parts,
				QString* canonicalPath, QString* error);

	//! Open \p canonicalPath, re-check it, and read it (bounded).
	static bool readModule(const QString& root, const QString& canonicalPath,
				QByteArray* source, QString* error);

	const QString& root() const { return m_root; }

private:
	static int requireFunction(lua_State* L);
	int require(lua_State* L, const QByteArray& name, QByteArray* error);
	bool prepare(const QByteArray& name, QString* canonical, QByteArray* source, QString* error);
	int runModule(lua_State* L, const QByteArray& name, const QByteArray& source,
			const QString& canonical);

	QString m_root;
	QStringList m_stack; //!< module names being loaded, outermost first
	int m_loaded{0};
};

} // namespace lmms

#endif
