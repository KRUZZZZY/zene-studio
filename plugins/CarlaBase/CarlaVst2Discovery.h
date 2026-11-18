/*
 * CarlaVst2Discovery.h - finding VST2 plug-ins for the Carla-backed VST2 host
 *                        (Q050-481.1; in carlabase since Q050-481.2, so the
 *                        instrument and effect hosts share one cache).
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

#ifndef LMMS_CARLA_VST2_DISCOVERY_H
#define LMMS_CARLA_VST2_DISCOVERY_H

#include <QList>
#include <QString>
#include <QStringList>

#include "carlabase_export.h"

namespace lmms::carlavst2
{

//! How Carla reaches a VST2 binary: in process (a native module of this
//! platform) or through one of its out-of-process bridges.
enum class Binary
{
	Native, //!< a .so/.dll/.vst of the host's own architecture
	Win64,  //!< a 64-bit Windows .dll on a non-Windows host: Carla's Wine bridge
	Win32,  //!< a 32-bit Windows .dll on a non-Windows host: Carla's 32-bit Wine bridge
	Unknown
};

//! One plug-in Carla's discovery tool reported inside a VST2 binary.
struct Vst2Record
{
	QString file;     //!< absolute path of the binary
	QString name;     //!< the plug-in's own name
	QString maker;    //!< its vendor string (may be empty)
	qint64 uniqueId = 0;
	bool isSynth = false;
	Binary binary = Binary::Unknown;
};

//! The key attribute names a VST2 device carries (Plugin::Descriptor::SubPluginFeatures::Key):
//! what plugin.list publishes and a saved project stores.
inline QString fileAttribute() { return QStringLiteral("file"); }
inline QString nameAttribute() { return QStringLiteral("name"); }
inline QString uniqueIdAttribute() { return QStringLiteral("uniqueid"); }
inline QString binaryAttribute() { return QStringLiteral("binary"); }
inline QString makerAttribute() { return QStringLiteral("maker"); }

CARLABASE_EXPORT QString binaryName(Binary binary);
CARLABASE_EXPORT Binary binaryFromName(const QString& name);

//! The binary kind of @a path from its own header (ELF / PE machine field), not its extension.
CARLABASE_EXPORT Binary binaryOf(const QString& path);

//! The directories searched for VST2 binaries. ZENE_VST2_PATH (list-separated), when set, is the
//! whole list - a test or a packaged setup names exactly where to look. Otherwise VST_PATH, the
//! product's VST directory setting, ~/.vst and the usual system locations.
CARLABASE_EXPORT QStringList searchDirs();

//! Every VST2 plug-in under searchDirs(), as Carla's own discovery tools report them (run out of
//! process, so a crashing plug-in cannot take the product down; Windows binaries through Wine).
//! Results are cached per file and refreshed when the file's size or time changes. Empty when
//! no Carla runtime (libcarla_native-plugin and its discovery tools) is installed.
CARLABASE_EXPORT QList<Vst2Record> discoverAll();

//! Parses carla-discovery's stdout ("carla-discovery::<key>::<value>" lines, one block per plug-in).
CARLABASE_EXPORT QList<Vst2Record> parseDiscoveryOutput(const QString& file, Binary binary, const QString& output);

//! The folder of the Carla runtime in use (where its bridges and discovery tools live), or empty.
CARLABASE_EXPORT QString runtimeFolder();

} // namespace lmms::carlavst2

#endif // LMMS_CARLA_VST2_DISCOVERY_H
