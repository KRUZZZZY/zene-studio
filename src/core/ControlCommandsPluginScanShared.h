/*
 * ControlCommandsPluginScanShared.h - what the two halves of the plugin.*
 *                                     scan-cache surface share.
 *
 * The scan-cache group is split into a read half (ControlCommandsPluginScan.cpp:
 * plugin.scan_cache_get_state / scan_cache_list / scan_cache_lookup) and an
 * edit half (ControlCommandsPluginScanEdit.cpp: plugin.scan_cache_quarantine_add /
 * quarantine_remove / plugin.rescan), the seam ControlCommandsAutomation.cpp /
 * ...AutomationEdit.cpp established: a file over the per-file length ratchet
 * (tests/file-length-gate.sh, 500 lines) is not one a group's boilerplate fits
 * in, and splitting along "reads" / "writes" puts every state CHANGING handler
 * in one file a reviewer can read at once.
 *
 * The declarations below are the shared vocabulary of the pair: the engine
 * handles and the JSON shapes. They are DEFINED in the read half (which owns
 * the reporting) so there is one definition of each, and the edit half calls
 * them rather than restating a shape it would then have to keep in sync.
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

#ifndef LMMS_CONTROL_COMMANDS_PLUGIN_SCAN_SHARED_H
#define LMMS_CONTROL_COMMANDS_PLUGIN_SCAN_SHARED_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace lmms
{

class PluginFactory;
class PluginScanCache;
class PluginScanRecord;

namespace control
{

/*! The scan cache this instance's scans read and write, or nullptr when there
 *  is no factory at all.
 *
 * NOTE the engine's own cost model, stated here because every id in the group
 * pays it: asking for the factory CONSTRUCTS it if this is the first ask
 * (PluginFactory::instance()), and the constructor runs the first scan
 * (src/core/PluginFactory.cpp). `plugin.list` has had the same property since
 * it landed, so this is the existing behaviour rather than a new one - but a
 * reader of this group deserves to know that the first read here may be slow
 * and may write the derived cache file.
 */
PluginScanCache* scanCacheOrNull();

//! The plugin factory, for the fields only it publishes (the last scan's own report).
PluginFactory* pluginFactoryOrNull();

//! One remembered record: its fingerprint, its status and - for a descriptor -
//! the metadata the scan resolved. The same key set the cache's own JSON uses
//! (PluginScanCache.cpp's recordToJson), in the wire's lower_snake form.
QJsonObject scanRecordJson(const PluginScanRecord& record);

//! The last scan's own report (PluginFactory::ScanStats plus the one-line text).
QJsonObject scanStatsJson(const PluginFactory& factory);

//! One quarantine entry: the path, the reason, and whether the file is still there.
QJsonObject quarantineEntryJson(const QString& path, const QString& reason);

//! Every quarantine entry, in the cache's own file order.
QJsonArray quarantineReport(const PluginScanCache& cache);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_PLUGIN_SCAN_SHARED_H
