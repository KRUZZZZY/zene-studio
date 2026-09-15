/*
 * AutomationRamp.cpp - the per-sample automation ramp (feature-list row 9)
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

#include "AutomationRamp.h"

namespace lmms
{

void AutomationRamp::reset(f_cnt_t frames) noexcept
{
	m_knotCount = 0;
	m_frames = frames;
	m_refusals = 0;
}

bool AutomationRamp::addKnot(f_cnt_t frame, float value) noexcept
{
	if (m_knotCount >= MaxKnots)
	{
		++m_refusals;
		return false;
	}
	if (m_knotCount > 0 && frame < m_knots[m_knotCount - 1].frame)
	{
		++m_refusals;
		return false;
	}
	m_knots[m_knotCount].frame = frame;
	m_knots[m_knotCount].value = value;
	++m_knotCount;
	return true;
}

void AutomationRamp::setBlockValue(float value) noexcept
{
	reset(m_frames);
	addKnot(0, value);
}

float AutomationRamp::valueAt(f_cnt_t frame) const noexcept
{
	if (m_knotCount == 0)
	{
		return 0.0f;
	}
	// Walk from the last knot down: the common case is a handful of knots and
	// the frame wants the tail of them, and a linear scan over at most
	// MaxKnots bounded knots allocates nothing and cannot run away.
	for (int i = m_knotCount - 1; i >= 0; --i)
	{
		if (m_knots[i].frame > frame)
		{
			continue;
		}
		if (i + 1 >= m_knotCount)
		{
			return m_knots[i].value;
		}
		const Knot& a = m_knots[i];
		const Knot& b = m_knots[i + 1];
		if (b.frame <= a.frame)
		{
			// Two knots on one frame: the later one is the value (a step).
			return b.value;
		}
		const float t = static_cast<float>(frame - a.frame) / static_cast<float>(b.frame - a.frame);
		return a.value + t * (b.value - a.value);
	}
	return m_knots[0].value;
}

const AutomationRamp::Knot& AutomationRamp::knot(int index) const noexcept
{
	static const Knot empty{0, 0.0f};
	if (index < 0 || index >= m_knotCount)
	{
		return empty;
	}
	return m_knots[index];
}

} // namespace lmms
