/*
 * ChannelPickDialog.h - choose several mixer channels, for an edit that applies to all of them
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

#ifndef LMMS_GUI_CHANNEL_PICK_DIALOG_H
#define LMMS_GUI_CHANNEL_PICK_DIALOG_H

#include <QStringList>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! MX-050 (0.5.0 L09): a checklist of the mixer's channels (all but @a exclude and the master);
 *  answers the ch-<n> ids ticked, or nothing when cancelled. Modal, UI thread. */
LMMS_EXPORT QStringList pickChannels(QWidget* parent, const QString& title, const QString& exclude);

} // namespace lmms::gui

#endif // LMMS_GUI_CHANNEL_PICK_DIALOG_H
