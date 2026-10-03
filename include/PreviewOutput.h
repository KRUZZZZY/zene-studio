/*
 * PreviewOutput.h - the mixer channel the browser's previews play into
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
#ifndef LMMS_PREVIEW_OUTPUT_H
#define LMMS_PREVIEW_OUTPUT_H

#include "lmms_export.h"

namespace lmms
{

class PlayHandle;

/*! 0.5.0 L09 (MX-042): where the browser's previews (a sample, a preset) play - a mixer channel, so they can go
 *  to a cue bus instead of the main mix. A preference (config mixer/previewchannel); 0, the master, by default,
 *  which is where previews always played. */
namespace previewoutput
{

//! The preview channel's index; the master when the saved one no longer exists.
LMMS_EXPORT int channel();
LMMS_EXPORT void setChannel(int index);
//! Point @a handle's bus at the preview channel. Call it before the handle is added to the engine.
LMMS_EXPORT void route(PlayHandle* handle);

} // namespace previewoutput

} // namespace lmms

#endif // LMMS_PREVIEW_OUTPUT_H
