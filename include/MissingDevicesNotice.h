/*
 * MissingDevicesNotice.h - the one non-modal notice a project open raises for its placeholders
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

#ifndef LMMS_GUI_MISSING_DEVICES_NOTICE_H
#define LMMS_GUI_MISSING_DEVICES_NOTICE_H

#include <QList>

#include "MissingDevices.h"
#include "lmms_export.h"

class QMessageBox;
class QWidget;

namespace lmms::gui
{

/*! 0.5.0 Q050-018: shows ONE non-modal box listing every placeholder's name, format and vendor (the text of
 *  missingdevices::noticeText), and returns it - null, with nothing shown, when @a devices is empty. The box
 *  deletes itself when closed and never blocks the load or the event loop. */
LMMS_EXPORT QMessageBox* showMissingDevicesNotice(QWidget* parent, const QList<MissingDevice>& devices);

} // namespace lmms::gui

#endif // LMMS_GUI_MISSING_DEVICES_NOTICE_H
