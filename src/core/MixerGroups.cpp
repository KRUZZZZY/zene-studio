/*
 * MixerGroups.cpp - nested VCA groups: a group's gain through the groups it sits in
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

#include "MixerGroups.h"

#include <algorithm>
#include <array>

#include "Mixer.h"
#include "VcaGroup.h"

namespace lmms
{

float vcaGroupGain(const Mixer& mixer, const VcaGroup& group)
{
	// The groups already counted: a chain a project file closed into a loop counts each of them once.
	std::array<const VcaGroup*, MaxGroupDepth + 1> seen{&group};
	float gain = group.gain();
	int parent = group.parentId();
	for (int depth = 0; parent != VcaGroup::NoParent && depth < MaxGroupDepth; ++depth)
	{
		const VcaGroup* above = mixer.vcaGroup(parent);
		const auto counted = seen.begin() + depth + 1;
		if (above == nullptr || std::find(seen.begin(), counted, above) != counted) { break; }
		*counted = above;
		gain *= above->gain();
		parent = above->parentId();
	}
	return gain;
}

bool wouldLoop(const Mixer& mixer, int groupId, int parentId)
{
	int at = parentId;
	for (int depth = 0; at != VcaGroup::NoParent; ++depth)
	{
		if (at == groupId || depth >= MaxGroupDepth) { return true; }
		const VcaGroup* above = mixer.vcaGroup(at);
		if (above == nullptr) { return false; }
		at = above->parentId();
	}
	return false;
}

} // namespace lmms
