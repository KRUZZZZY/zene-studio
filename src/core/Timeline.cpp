/*
 * Timeline.cpp
 *
 * Copyright (c) 2023 Dominic Clark
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

#include "Timeline.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include <QDomElement>

namespace lmms {

void Timeline::setLoopBegin(TimePos begin)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(begin, TimePos{m_loopEnd});
}

void Timeline::setLoopEnd(TimePos end)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(TimePos{m_loopBegin}, end);
}

void Timeline::setLoopPoints(TimePos begin, TimePos end)
{
	std::tie(m_loopBegin, m_loopEnd) = std::minmax(begin, end);
}

void Timeline::setLoopEnabled(bool enabled)
{
	if (enabled != m_loopEnabled) {
		m_loopEnabled = enabled;
		emit loopEnabledChanged(m_loopEnabled);
	}
}

void Timeline::setStopBehaviour(StopBehaviour behaviour)
{
	if (behaviour != m_stopBehaviour) {
		m_stopBehaviour = behaviour;
		emit stopBehaviourChanged(m_stopBehaviour);
	}
}

void Timeline::setPunchRange(tick_t begin, tick_t end)
{
	if (begin > end) { std::swap(begin, end); }
	if (begin == m_punchBegin && end == m_punchEnd) { return; }
	m_punchBegin = begin;
	m_punchEnd = end;
	emit punchChanged();
}

void Timeline::setPunchEnabled(bool enabled)
{
	if (enabled == m_punchEnabled) { return; }
	m_punchEnabled = enabled;
	emit punchChanged();
}

void Timeline::clearPunch()
{
	if (!shouldPersistPunch()) { return; }
	m_punchBegin = 0;
	m_punchEnd = 0;
	m_punchEnabled = false;
	emit punchChanged();
}

bool Timeline::punchCapturesAt(tick_t ticks) const
{
	return punchArmed() && ticks >= m_punchBegin && ticks < m_punchEnd;
}

void Timeline::saveSettings(QDomDocument& doc, QDomElement& element)
{
	element.setAttribute("lp0pos", static_cast<int>(loopBegin()));
	element.setAttribute("lp1pos", static_cast<int>(loopEnd()));
	element.setAttribute("lpstate", static_cast<int>(loopEnabled()));
	element.setAttribute("stopbehaviour", static_cast<int>(stopBehaviour()));
	// The punch region (0.3.0). Written ONLY when it is armed or holds a
	// range, so a project that never punches re-saves exactly the bytes it has
	// always had - the same rule the tempo map and the modulation layer follow.
	// The range is two scalars and the flag is a third, so their absence is
	// unambiguous and loadSettings can clear them (below).
	if (shouldPersistPunch())
	{
		element.setAttribute("punch0pos", static_cast<int>(m_punchBegin));
		element.setAttribute("punch1pos", static_cast<int>(m_punchEnd));
		element.setAttribute("punchstate", static_cast<int>(m_punchEnabled));
	}
}

void Timeline::loadSettings(const QDomElement& element)
{
	setLoopPoints(
		static_cast<TimePos>(element.attribute("lp0pos").toInt()),
		static_cast<TimePos>(element.attribute("lp1pos").toInt())
	);
	setLoopEnabled(static_cast<bool>(element.attribute("lpstate").toInt()));
	setStopBehaviour(static_cast<StopBehaviour>(element.attribute("stopbehaviour", "1").toInt()));
	// RESET ON ABSENCE. The punch attributes are written only for a non-default
	// region, so a restore of a checkpoint that predates the first punch call
	// carries no punch attribute at all - and without this clear the region
	// would survive the undo and `transport.punch_set` would not be reversible.
	// The same trap the skill records for the warp feature: a value serialised
	// only when non-default cannot be taken back unless the loader resets it.
	clearPunch();
	if (element.hasAttribute("punch0pos") || element.hasAttribute("punch1pos")
		|| element.hasAttribute("punchstate"))
	{
		setPunchRange(element.attribute("punch0pos", "0").toInt(),
			element.attribute("punch1pos", "0").toInt());
		setPunchEnabled(element.attribute("punchstate", "0").toInt() != 0);
	}
}

} // namespace lmms
