/*
 * HeadlessMode.h - "can a human answer a dialog in this instance?"
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

#ifndef LMMS_HEADLESS_MODE_H
#define LMMS_HEADLESS_MODE_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! True when this instance has no display a human could answer a dialog on: the
//! Qt platform plugin is one of the headless ones (offscreen/minimal/vnc) or
//! QT_QPA_PLATFORM asks for one. Both sources are consulted because callers ask
//! before and after Qt's platform plugin has reported its name.
//!
//! Interactive prompts - the setup dialog, the audio-device setup failure box,
//! "the project was modified, save it?" - are meaningless here: nothing can
//! click them, so each one is an unbounded hang that pins the whole instance and
//! starves the agent control socket (task #626 measured 92 s of `busy` and two
//! different shutdown hangs, both of them exactly this).
LMMS_EXPORT bool isHeadlessRun();

//! The platform name that made isHeadlessRun() true, or the empty string.
//! Diagnostic only: it goes into the not-ready reason agents read.
LMMS_EXPORT QString headlessPlatformName();

} // namespace lmms

#endif // LMMS_HEADLESS_MODE_H
