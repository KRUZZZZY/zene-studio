/*
 * AutomationSwitches.h - the song-wide automation read and write switches
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
#ifndef LMMS_AUTOMATION_SWITCHES_H
#define LMMS_AUTOMATION_SWITCHES_H

#include <atomic>

#include "LmmsTypes.h"
#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L06 (ED-200, ED-201): switches over all automation at once, on top of each control's own
 *  mode (an AND, never an override) - read off makes every control keep its manual value; write off
 *  stops every control recording; punched writes record only inside the transport's punch region.
 *  Saved as <automation-switches> only when one is not at its default; the audio thread reads atomics. */
class LMMS_EXPORT AutomationSwitches : public ProjectSection
{
public:
	static AutomationSwitches& instance();

	bool reads() const { return m_reads.load(std::memory_order_relaxed); }
	bool writes() const { return m_writes.load(std::memory_order_relaxed); }
	bool punchedWrites() const { return m_punched.load(std::memory_order_relaxed); }
	void set(bool reads, bool writes, bool punchedWrites);

	//! Audio thread: whether a control may record at song tick @a tick.
	bool writesAt(tick_t tick) const;

	QString elementName() const override { return QStringLiteral("automation-switches"); }
	bool shouldPersist() const override { return !reads() || !writes() || punchedWrites(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { set(true, true, false); }

private:
	AutomationSwitches() = default;
	std::atomic<bool> m_reads{true};
	std::atomic<bool> m_writes{true};
	std::atomic<bool> m_punched{false};
};

} // namespace lmms

#endif // LMMS_AUTOMATION_SWITCHES_H
