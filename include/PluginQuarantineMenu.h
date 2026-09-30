/*
 * PluginQuarantineMenu.h - Edit > Plugin Quarantine: hide a plugin file from the scan, or release it
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

#ifndef LMMS_GUI_PLUGIN_QUARANTINE_MENU_H
#define LMMS_GUI_PLUGIN_QUARANTINE_MENU_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "The plugin scan cache and its quarantine list are drivable, and there is no
 *  interface for either": a "Plugin Quarantine" submenu of @a menu, rebuilt from
 *  plugin.scan_cache_get_state each time it opens - a line with the cache's file and quarantine
 *  counts, one Release item per quarantined file (plugin.scan_cache_quarantine_remove; its tooltip
 *  is the path and the reason, and a file that is gone says so), Quarantine a Plugin File...
 *  (plugin.scan_cache_quarantine_add, the file from a picker) and Rescan Plugins (plugin.rescan),
 *  which is what applies an edit. */
LMMS_EXPORT QMenu* addPluginQuarantineMenu(QMenu* menu);

//! The counts line for a plugin.scan_cache_get_state reply.
LMMS_EXPORT QString pluginQuarantineStatusText(const QJsonObject& state);

//! Tests replace the file picker (default: a file dialog). An empty result cancels; passing an
//! empty function restores the default.
LMMS_EXPORT void setPluginQuarantinePicker(std::function<QString()> picker);

} // namespace lmms::gui

#endif // LMMS_GUI_PLUGIN_QUARANTINE_MENU_H
