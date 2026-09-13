/*
 * RackZones.cpp - a rack's key zones and velocity zones: MIDI key ranges and
 *                 velocity ranges mapped to one of the rack's chains.
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

#include "RackZones.h"

#include <cstddef>

#include <QDomDocument>

namespace lmms
{

namespace
{

//! Both bounds inside the engine's range, and low <= high. One definition for
//! the key range and the velocity range, so the two cannot disagree about what
//! "a range" means.
bool isValidRange(int low, int high, int minimum, int maximum)
{
	if (low < minimum) { return false; }
	if (high > maximum) { return false; }
	return low <= high;
}

} // namespace

bool isValidRackZone(const RackZone& zone, int chainCount)
{
	if (!isValidRange(zone.lowKey, zone.highKey, RackZoneMinKey, RackZoneMaxKey)) { return false; }
	if (!isValidRange(zone.lowVelocity, zone.highVelocity, RackZoneMinVelocity,
			RackZoneMaxVelocity))
	{
		return false;
	}
	// A zone that names a chain the rack does not have is refused rather than
	// stored: a stored one could only ever fail to route, or - worse - route a
	// chain nobody asked for.
	if (zone.chain < 0) { return false; }
	return zone.chain < chainCount;
}

auto RackZones::zoneCount() const -> int
{
	return static_cast<int>(m_zones.size());
}

auto RackZones::zone(int index) const -> const RackZone*
{
	if (index < 0 || index >= zoneCount()) { return nullptr; }
	return &m_zones[static_cast<std::size_t>(index)];
}

auto RackZones::addZone(const RackZone& zone) -> int
{
	m_zones.push_back(zone);
	return zoneCount() - 1;
}

auto RackZones::insertZone(int index, const RackZone& zone) -> bool
{
	if (index < 0) { return false; }
	if (index > zoneCount()) { return false; }
	m_zones.insert(m_zones.begin() + index, zone);
	return true;
}

auto RackZones::removeZone(int index) -> bool
{
	if (zone(index) == nullptr) { return false; }
	m_zones.erase(m_zones.begin() + index);
	return true;
}

auto RackZones::resolve(int key, int velocity) const -> int
{
	// First match wins, in the order the zones were added - see the header.
	for (std::size_t i = 0; i < m_zones.size(); ++i)
	{
		const RackZone& candidate = m_zones[i];
		if (key < candidate.lowKey) { continue; }
		if (key > candidate.highKey) { continue; }
		if (velocity < candidate.lowVelocity) { continue; }
		if (velocity > candidate.highVelocity) { continue; }
		return static_cast<int>(i);
	}
	return -1;
}

void RackZones::clear()
{
	m_zones.clear();
}

void RackZones::saveSettings(QDomDocument& doc, QDomElement& rackElement) const
{
	for (const RackZone& zone : m_zones)
	{
		QDomElement zoneElement = doc.createElement(QStringLiteral("zone"));
		zoneElement.setAttribute(QStringLiteral("low_key"), zone.lowKey);
		zoneElement.setAttribute(QStringLiteral("high_key"), zone.highKey);
		zoneElement.setAttribute(QStringLiteral("low_velocity"), zone.lowVelocity);
		zoneElement.setAttribute(QStringLiteral("high_velocity"), zone.highVelocity);
		zoneElement.setAttribute(QStringLiteral("chain"), zone.chain);
		// Written only when the zone has one, so a project that never names a
		// sample does not grow an empty attribute it never carried.
		if (!zone.sample.isEmpty())
		{
			zoneElement.setAttribute(QStringLiteral("sample"), zone.sample);
		}
		rackElement.appendChild(zoneElement);
	}
}

void RackZones::loadSettings(const QDomElement& rackElement)
{
	clear();

	for (QDomElement zoneElement = rackElement.firstChildElement(QStringLiteral("zone"));
		!zoneElement.isNull();
		zoneElement = zoneElement.nextSiblingElement(QStringLiteral("zone")))
	{
		RackZone zone;
		zone.lowKey = zoneElement.attribute(QStringLiteral("low_key"),
			QString::number(RackZoneMinKey)).toInt();
		zone.highKey = zoneElement.attribute(QStringLiteral("high_key"),
			QString::number(RackZoneMaxKey)).toInt();
		zone.lowVelocity = zoneElement.attribute(QStringLiteral("low_velocity"),
			QString::number(RackZoneMinVelocity)).toInt();
		zone.highVelocity = zoneElement.attribute(QStringLiteral("high_velocity"),
			QString::number(RackZoneMaxVelocity)).toInt();
		zone.chain = zoneElement.attribute(QStringLiteral("chain"), QStringLiteral("0")).toInt();
		zone.sample = zoneElement.attribute(QStringLiteral("sample"));
		m_zones.push_back(zone);
	}
}

} // namespace lmms
