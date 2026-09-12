/*
 * UnattendedRun.h - "can a human answer a dialog in this process?"
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

#ifndef LMMS_UNATTENDED_RUN_H
#define LMMS_UNATTENDED_RUN_H

#include "lmms_export.h"

namespace lmms
{

//! True when this process was started with --control-socket: the instance is
//! driven by an agent, so no modal dialog may be opened (SPEC-zene-studio.md
//! A13).  Set by main() while parsing the command line - before the GUI, the
//! engine or the command registry exist.
LMMS_EXPORT bool isAgentInstance();
LMMS_EXPORT void setAgentInstance(bool agent);

//! True when nobody can answer a dialog in this process: it was started with
//! --control-socket, or Qt runs on a platform with no display
//! (offscreen/minimal/vnc, i.e. a CI run).  A QMessageBox::exec() /
//! QDialog::exec() here is a wait on a click that never comes: Qt keeps
//! dispatching events inside the dialog's nested loop, so the instance stays
//! alive, answers `control.ping` with `engine_ready:false`, and never becomes
//! usable (task #625, measured on the base commit 6b01b98eb).
LMMS_EXPORT bool isUnattendedRun();

} // namespace lmms

#endif // LMMS_UNATTENDED_RUN_H
