/*
 * CrashReportMenu.h - Help > Crash Reports: arm the reporter, see its report, keep or delete it
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

#ifndef LMMS_GUI_CRASH_REPORT_MENU_H
#define LMMS_GUI_CRASH_REPORT_MENU_H

#include <functional>

#include <QString>

#include "lmms_export.h"

class QJsonObject;
class QMenu;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "The crash reporter is drivable, and there is no way to see or send a report
 *  from the interface": a "Crash Reports" submenu of @a menu. Report Crashes arms and disarms the
 *  reporter (crash.enable / crash.disable - disarming deletes nothing); a disabled line re-reads
 *  crash.list_reports each time the submenu opens; Show Report Folder opens its directory; Stop
 *  Offering the Report acknowledges a pending one (crash.acknowledge_report, the file stays) and
 *  Delete the Report... discards it (crash.discard_report) after a confirmation, because the
 *  report's content cannot be recovered. There is no Send: this build has no upload. */
LMMS_EXPORT QMenu* addCrashReportMenu(QMenu* menu);

//! The status line for a crash.list_reports reply.
LMMS_EXPORT QString crashReportStatusText(const QJsonObject& state);

//! Tests replace the confirmation (default: a question box) and the folder opener (default:
//! QDesktopServices). Passing an empty function restores the default.
LMMS_EXPORT void setCrashReportDeleteConfirm(std::function<bool(const QString& reportPath)> confirm);
LMMS_EXPORT void setCrashReportFolderOpener(std::function<void(const QString& directory)> opener);

} // namespace lmms::gui

#endif // LMMS_GUI_CRASH_REPORT_MENU_H
