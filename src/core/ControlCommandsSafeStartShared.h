/*
 * ControlCommandsSafeStartShared.h - what the two halves of the safestart.*
 *                                    surface share.
 *
 * The safe-start group is split into a READ half (ControlCommandsSafeStart.cpp:
 * safestart.get_state) and an EDIT half (ControlCommandsSafeStartEdit.cpp:
 * safestart.acknowledge / clear / set_skip), the seam
 * ControlCommandsAutomation.cpp / ...AutomationEdit.cpp established and the
 * plugin.* scan-cache pair follows: a group's boilerplate does not fit in one
 * file under the per-file length ratchet (tests/file-length-gate.sh, 500 lines),
 * and splitting along "reads" / "writes" puts every state-CHANGING handler in
 * one file a reviewer can read at once.
 *
 * The declarations below are the shared vocabulary of the pair: the four ids and
 * the three small JSON/file helpers. They are DEFINED in the read half (which
 * owns the reporting), and the edit half calls them rather than restating a
 * shape it would then have to keep in sync - exactly how the scan-cache pair
 * shares its engine handles.
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
 */

#ifndef LMMS_CONTROL_COMMANDS_SAFE_START_SHARED_H
#define LMMS_CONTROL_COMMANDS_SAFE_START_SHARED_H

#include <string>

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"

namespace lmms
{

namespace control
{

//! The group's four ids. One definition each, in the read half's header scope.
extern const QString SafeStartGetStateId;
extern const QString SafeStartAcknowledgeId;
extern const QString SafeStartClearId;
extern const QString SafeStartSetSkipId;

//! \a text as a wire string.
QString safeStartWire(const std::string& text);

//! One of the module's own files, as it stands: its path, whether it is there,
//! how big it is and when it was last written. The shape the crash group's
//! report listing uses (src/core/ControlCommandsCrash.cpp: fileJson).
QJsonObject safeStartFileJson(const std::string& path);

//! The "this instance has no safe-start module" refusal: the module is a plain
//! file API, so an instance whose working directory could not be opened has no
//! state at all. The read still answers; the writers refuse.
ControlResult safeStartNoModule(const QString& command);

//! The before-state of the writers: what the module's files and switches held.
QJsonObject safeStartBeforeState(const std::string& marker, const std::string& acknowledged);

// --- the edit half's handlers, DEFINED in ControlCommandsSafeStartEdit.cpp ----
ControlResult safeStartHandleAcknowledge();
ControlResult safeStartHandleClear();
ControlResult safeStartHandleSetSkip(const QJsonObject& args);

/*! The edit half's registration point, called by registerSafeStartCommands() so
 *  the registry has exactly ONE safestart.* entry (the scan-cache pair's
 *  arrangement: registerPluginScanCommands calls registerPluginScanEditCommands).
 */
void registerSafeStartEditCommands(ControlRegistry& registry);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_SAFE_START_SHARED_H
