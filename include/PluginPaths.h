/*
 * PluginPaths.h - the folders the VST3, CLAP and LV2 hosts scan for plug-ins
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
#ifndef LMMS_PLUGIN_PATHS_H
#define LMMS_PLUGIN_PATHS_H

#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::pluginpaths
{

/*! 0.5.0 L10 (MX-163): where each hosted format looks for plug-ins. The user's own folders are kept
 *  in the config file (paths/vst3dirs, paths/clapdirs, paths/lv2dirs; ';'-separated) and searched
 *  first; then the format's usual places (VST3: the VST folder setting, ~/.vst3, /usr/lib/vst3,
 *  /usr/local/lib/vst3; CLAP: CLAP_PATH, ~/.clap, /usr/lib/clap, the VST folder setting; LV2: the
 *  LV2_PATH the process started with, or ~/.lv2, /usr/lib/lv2, /usr/local/lib/lv2). The VST3 and
 *  CLAP hosts read searchDirs() on every scan (plugin.rescan applies a change); LV2's world is loaded
 *  once, at start, so a change there applies from the next start. Control thread only. */

enum class Format
{
	Vst3,
	Clap,
	Lv2
};

LMMS_EXPORT const QStringList& formatNames();  //!< "vst3", "clap", "lv2" - the Format order
LMMS_EXPORT bool formatFromName(const QString& name, Format* format);

//! The user's own folders for @a format.
LMMS_EXPORT QStringList configured(Format format);
LMMS_EXPORT void setConfigured(Format format, const QStringList& dirs);
//! The format's usual places.
LMMS_EXPORT QStringList defaults(Format format);
//! Every folder a scan reads, the user's first, each once (by its canonical path) and only if it exists.
LMMS_EXPORT QStringList searchDirs(Format format);

} // namespace lmms::pluginpaths

#endif // LMMS_PLUGIN_PATHS_H
