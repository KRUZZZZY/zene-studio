/*
 * VideoCommands.h - the video.* command group's registration
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

#ifndef LMMS_VIDEO_COMMANDS_H
#define LMMS_VIDEO_COMMANDS_H

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;

/*! 0.5.0 ED-054: video.attach / detach / get_state / set_offset / extract_audio (src/core/ControlCommandsVideo.cpp)
 *  and video.window_show (src/gui/VideoWindow.cpp, registered from here). Registered in every build: without
 *  FFmpeg each one answers a typed `requires` refusal saying video is unavailable. */
LMMS_EXPORT void registerVideoCommands(ControlRegistry& registry);
LMMS_EXPORT void registerVideoWindowCommand(ControlRegistry& registry);

} // namespace lmms

#endif // LMMS_VIDEO_COMMANDS_H
