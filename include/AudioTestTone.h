/*
 * AudioTestTone.h - whether the audio.test_tone sine is still sounding
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

#ifndef LMMS_AUDIO_TEST_TONE_H
#define LMMS_AUDIO_TEST_TONE_H

#include "lmms_export.h"

namespace lmms
{

//! True while the tone that audio.test_tone started is still sounding: it was started, it was not stopped
//! by audio.test_tone_stop and it has not run out. Control thread only; not for the audio thread.
LMMS_EXPORT bool audioTestToneActive();

//! How many tone slots currently hold a handle, sounding or retired and not yet reclaimed. For tests and
//! diagnostics. Control thread only; not for the audio thread.
LMMS_EXPORT int audioTestToneHandlesHeld();

} // namespace lmms

#endif // LMMS_AUDIO_TEST_TONE_H
