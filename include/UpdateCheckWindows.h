/*
 * UpdateCheckWindows.h - the update question and notice, and starting the daily check
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

#ifndef LMMS_GUI_UPDATE_CHECK_WINDOWS_H
#define LMMS_GUI_UPDATE_CHECK_WINDOWS_H

#include "lmms_export.h"

class QMenu;
class QWidget;

namespace lmms::gui
{

/*! Q050-501. No Q_OBJECT here, as with TelemetryConsentDialog.h: -DZENE_UPDATE_CHECK=OFF removes the .cpp.
 *  The windows are found by object name: "updateConsentDialog" (buttons "updateConsentYes", "updateConsentNo", check
 *  box "updateConsentPrereleases") and "updateNoticeDialog" (label "updateNoticeHeading", notes "updateNoticeNotes",
 *  button "updateNoticeDownload"). Neither is modal: nothing about them can hold up the program. */

//! Help > Check for Updates, running window.update_check.
LMMS_EXPORT void addUpdateCheckMenuEntry(QMenu* help);
/*! What starting does: the question, once, while nobody has answered it; otherwise a check if one is due. Then an hourly
 *  look at whether a day has passed, for a window left open for days. Makes no request while the setting is off. */
LMMS_EXPORT void runUpdateCheckStartup(QWidget* window);

} // namespace lmms::gui

#endif // LMMS_GUI_UPDATE_CHECK_WINDOWS_H
