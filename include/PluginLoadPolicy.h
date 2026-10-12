/*
 * PluginLoadPolicy.h - dialog policy for plug-in load failures
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

#ifndef LMMS_PLUGIN_LOAD_POLICY_H
#define LMMS_PLUGIN_LOAD_POLICY_H

namespace lmms::pluginload
{

//! Per-plug-in dialogs are for interactive non-project operations only; project load reports once at completion.
inline auto shouldShowFailureDialog(bool hasGui, bool unattended, bool loadingProject) noexcept -> bool
{
	return hasGui && !unattended && !loadingProject;
}

} // namespace lmms::pluginload

#endif // LMMS_PLUGIN_LOAD_POLICY_H
