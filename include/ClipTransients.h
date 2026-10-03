/*
 * ClipTransients.h - the transients of a sample clip's audio
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

#ifndef LMMS_CLIP_TRANSIENTS_H
#define LMMS_CLIP_TRANSIENTS_H

#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleClip;

/*! 0.5.0 L05/L07 (ED-137, ED-150, ED-163): the transients of a sample clip's window, as source
 *  frames - the onset rule the tempo detector counts with (detection::detectOnsetFrames), on the
 *  window's audio mixed to mono. Empty for a clip with no audio. UI thread (it analyses the audio). */
LMMS_EXPORT std::vector<f_cnt_t> clipTransientFrames(SampleClip& clip);

//! The same transients as ticks from the clip's start - where the clip plays them now (its warp).
LMMS_EXPORT std::vector<int> clipTransientTicks(SampleClip& clip);

} // namespace lmms

#endif // LMMS_CLIP_TRANSIENTS_H
