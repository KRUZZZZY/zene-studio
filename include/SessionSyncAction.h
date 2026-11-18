/*
 * SessionSyncAction.h - the transport's session-sync toggle, with its peers and tempo
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

#ifndef LMMS_GUI_SESSION_SYNC_ACTION_H
#define LMMS_GUI_SESSION_SYNC_ACTION_H

#include <QString>

#include "lmms_export.h"

class QAction;
class QObject;
class QJsonObject;

namespace lmms::gui
{

/*! The transport toolbar's checkable session-sync action. Toggling runs link.set_enabled; the
 *  action's text and tooltip read link.get_state - the peer count and the session tempo - once a
 *  second while it is enabled, so a peer joining or a tempo arriving shows without a socket client.
 *  With the Ableton Link backend (the default build, docs/LINK-SYNC.md) it is labelled "Ableton
 *  Link", the name written in full as Ableton's Link guidelines ask, and the tooltip says it plays
 *  in time with Ableton Live, Bitwig and other Link apps and states Enabled/Disabled; with the
 *  zene-link-style fallback it keeps its "Sync" label and says it syncs Zene instances only. */
LMMS_EXPORT QAction* makeSessionSyncAction(QObject* parent);

//! The text the action shows for a link.get_state reply (split out so a test can read it).
LMMS_EXPORT QString sessionSyncText(const QJsonObject& state);

//! The tooltip the action shows for a link.get_state reply.
LMMS_EXPORT QString sessionSyncToolTip(const QJsonObject& state);

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_SYNC_ACTION_H
