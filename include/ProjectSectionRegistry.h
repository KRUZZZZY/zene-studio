/*
 * ProjectSectionRegistry.h - every project section and audio-thread singleton built once, before the engine runs
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
#ifndef LMMS_PROJECT_SECTION_REGISTRY_H
#define LMMS_PROJECT_SECTION_REGISTRY_H

#include "lmms_export.h"

namespace lmms::projectsections
{

/*! Build and register every ProjectSection the fork has, and every singleton the audio thread reads, once, on the
 *  thread that builds the Song (Song::Song, in every mode - the interface, a headless render, a test). Before this,
 *  each one registered itself the first time something touched it: in a run without the control registry (a
 *  `--render`) that could be never - so a project's section was silently skipped on load - or the audio thread
 *  itself, allocating there (review W3-batchA #1). A new section adds one line here. */
LMMS_EXPORT void registerAll();

} // namespace lmms::projectsections

#endif // LMMS_PROJECT_SECTION_REGISTRY_H
