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

/*! Q050-049: when the previous session died while scanning or instantiating a third-party plug-in
 *  (the safe-start marker names it, safestart::crashSuspect()), ask ONCE whether to quarantine
 *  exactly that file. Yes goes through plugin.scan_cache_quarantine_add, the command Edit >
 *  Plugin Quarantine uses. Returns true when the file was quarantined; false when there was no
 *  suspect, the answer was no, or the command refused. Tests replace the question with
 *  setCrashQuarantineAsker(); the default is a message box. */
LMMS_EXPORT bool offerCrashQuarantine();

//! Tests replace the yes/no question (argument: the prompt text). An empty function restores the box.
LMMS_EXPORT void setCrashQuarantineAsker(std::function<bool(const QString&)> asker);

//! Tests replace the failure notice (default: a warning box) to prove that a
//! refused quarantine is reported to the user.
LMMS_EXPORT void setCrashQuarantineFailureReporter(std::function<void(const QString&)> reporter);

} // namespace lmms::gui

#endif // LMMS_GUI_PLUGIN_QUARANTINE_MENU_H
