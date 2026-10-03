/*
 * AutomationSwitches.cpp - the song-wide automation read and write switches
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
#include "AutomationSwitches.h"

#include <QDomElement>

#include "AudioEngine.h"
#include "Engine.h"

namespace lmms
{

AutomationSwitches& AutomationSwitches::instance()
{
	static AutomationSwitches* switches = [] {
		auto* made = new AutomationSwitches();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *switches;
}

void AutomationSwitches::set(bool reads, bool writes, bool punchedWrites)
{
	m_bits.store((reads ? Reads : 0u) | (writes ? Writes : 0u) | (punchedWrites ? Punched : 0u), std::memory_order_relaxed);
}

bool AutomationSwitches::writesAt(tick_t tick) const
{
	const unsigned bits = m_bits.load(std::memory_order_relaxed);  // one snapshot of both switches
	if ((bits & Writes) == 0) { return false; }
	return (bits & Punched) == 0 || Engine::audioEngine()->punchCapturesTick(tick);
}

void AutomationSwitches::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	if (!reads()) { element.setAttribute(QStringLiteral("read"), 0); }
	if (!writes()) { element.setAttribute(QStringLiteral("write"), 0); }
	if (punchedWrites()) { element.setAttribute(QStringLiteral("punched"), 1); }
	parent.appendChild(element);
}

void AutomationSwitches::load(const QDomElement& element)
{
	set(element.attribute(QStringLiteral("read"), QStringLiteral("1")).toInt() != 0,
		element.attribute(QStringLiteral("write"), QStringLiteral("1")).toInt() != 0,
		element.attribute(QStringLiteral("punched"), QStringLiteral("0")).toInt() != 0);
}

} // namespace lmms
