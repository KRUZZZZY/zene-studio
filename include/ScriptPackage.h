/*
 * ScriptPackage.h - script package layout for the LMMS/Zene Lua API
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

#ifndef LMMS_SCRIPT_PACKAGE_H
#define LMMS_SCRIPT_PACKAGE_H

#include <QString>
#include <QVector>

namespace lmms
{

/*! \brief One script package on disk.
 *
 *  A package is a directory named after the package, holding an entry script
 *  called `package.lua`:
 *
 *      <scripts root>/<name>/package.lua      the entry point (required)
 *      <scripts root>/<name>/...              any sibling files the script loads
 *
 *  The entry script is an ordinary v0 script: it needs the
 *  `--! lmms-api <major>.<minor>` header like any other, and the engine treats
 *  it no differently once it is running. The package adds only *discovery* -
 *  a host can list what is installed without knowing any file names - and an
 *  optional manifest line:
 *
 *      --! lmms-package <name> <version>
 *
 *  The convention is written up in docs/LUA-PACKAGE-FORMAT.md.
 */
struct ScriptPackage
{
	QString name;        //!< directory name, also the script-visible package name
	QString version;     //!< from the manifest header; empty when absent
	QString packageDir;  //!< the package directory
	QString entryPath;   //!< <packageDir>/package.lua

	//! A package with no name or no entry path is not usable.
	bool isValid() const { return !name.isEmpty() && !entryPath.isEmpty(); }
};

namespace ScriptPackages
{

//! Name of the entry script inside a package directory.
inline constexpr const char* EntryFileName = "package.lua";

/*! Is \a name usable as a package name?
 *
 *  Lowercase letters, digits, `_` and `-`, starting with a letter, at most 64
 *  characters. Deliberately strict: a package name is a path component and a
 *  future script-visible identifier at the same time, so no dots, no spaces, no
 *  case folding surprises, and no traversal.
 */
bool isValidName(const QString& name);

/*! Parse `--! lmms-package <name> <version>` out of \a source (a package entry
 *  script, or any file). Empty when absent or malformed. \a version may be
 *  empty in the header itself; that is reported as an empty version, not as an
 *  error.
 */
ScriptPackage parseManifest(const QString& source);

/*! Scan \a scriptsRoot for packages, sorted by name.
 *
 *  Entry-less directories and invalid names are skipped rather than reported as
 *  broken packages: the scripts root is a shared directory (the product ships
 *  loose example scripts beside packages) and a half-copied directory must not
 *  break the listing. Callers that need to explain a skip can use
 *  isValidName() and EntryFileName themselves.
 */
QVector<ScriptPackage> scan(const QString& scriptsRoot);

} // namespace ScriptPackages

} // namespace lmms

#endif // LMMS_SCRIPT_PACKAGE_H
