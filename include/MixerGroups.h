/*
 * MixerGroups.h - nested VCA groups: a group's gain through the groups it sits in
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

#ifndef LMMS_MIXER_GROUPS_H
#define LMMS_MIXER_GROUPS_H

#include "lmms_export.h"

namespace lmms
{

class Mixer;
class VcaGroup;

//! 0.5.0 L09 (MX-070): how deep groups may nest (a parent chain longer than this is cut there).
constexpr int MaxGroupDepth = 8;

/*! A group's gain as it reaches its members: its own (VcaGroup::gain - mute and the chosen links
 *  included) times every group above it through VcaGroup::parentId. A parent id naming no group ends
 *  the chain; a cycle (only a project file can hold one) counts each group once, and the walk stops at MaxGroupDepth. Control thread (Mixer::refreshGroups). */
LMMS_EXPORT float vcaGroupGain(const Mixer& mixer, const VcaGroup& group);

//! Whether making @a parentId the parent of @a group would close a loop (or nest past MaxGroupDepth).
LMMS_EXPORT bool wouldLoop(const Mixer& mixer, int groupId, int parentId);

} // namespace lmms

#endif // LMMS_MIXER_GROUPS_H
