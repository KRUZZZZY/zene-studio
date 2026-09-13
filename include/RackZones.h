/*
 * RackZones.h - a rack's key zones and velocity zones: MIDI key ranges and
 *               velocity ranges mapped to one of the rack's chains.
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

#ifndef LMMS_RACK_ZONES_H
#define LMMS_RACK_ZONES_H

#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! The MIDI key range a zone is stated in (the engine's own key space).
constexpr int RackZoneMinKey = 0;
constexpr int RackZoneMaxKey = 127;
//! The velocity range a zone is stated in. 0..200 is the engine's own note
//! volume range (note.velocity_set, and the MIDI import path), not 0..127.
constexpr int RackZoneMinVelocity = 0;
constexpr int RackZoneMaxVelocity = 200;

/**
 * @brief One zone: a key range AND a velocity range, and the chain they map to.
 *
 * A "key zone" and a "velocity zone" are the two halves of one object here on
 * purpose: a zone that states only keys is a zone whose velocity range is the
 * whole velocity space, and one that states only velocities is a zone whose key
 * range is the whole keyboard. That is how the two are used together (a
 * split plus a dynamics layer), and it keeps ONE lookup rule rather than two
 * that can disagree about which of them wins.
 *
 * The ranges are inclusive. `sample` is an optional free-form reference
 * (typically the file name a chain plays); it is recorded and reported but does
 * not take part in matching, because the rack routes to a chain and the chain
 * owns what it plays.
 */
struct RackZone
{
	int lowKey = RackZoneMinKey;
	int highKey = RackZoneMaxKey;
	int lowVelocity = RackZoneMinVelocity;
	int highVelocity = RackZoneMaxVelocity;
	//! The chain the zone routes to (0 = the channel's own chain).
	int chain = 0;
	//! Optional sample reference; may be empty.
	QString sample;
};

/*! True when every range is inside the engine's own bounds, `low <= high` in
 *  both, and @a chain names an existing chain of a rack with @a chainCount
 *  chains. An invalid zone is REFUSED by the command that would create it,
 *  typed, rather than stored and matched never (or worse, always).
 */
LMMS_EXPORT bool isValidRackZone(const RackZone& zone, int chainCount);

/**
 * @brief A rack's zone list.
 *
 * Stored, validated, persisted as `<zone>` children of the channel's existing
 * `<rack>` element (no second container) and resolvable from the control
 * surface. The lookup is a read: no allocation, no lock, no growth.
 */
class LMMS_EXPORT RackZones
{
public:
	RackZones() = default;

	RackZones(const RackZones&) = delete;
	auto operator=(const RackZones&) -> RackZones& = delete;

	auto zoneCount() const -> int;
	auto zone(int index) const -> const RackZone*;

	//! Appends a zone and returns its index (the zone-<n> id).
	auto addZone(const RackZone& zone) -> int;
	//! Inserts a zone at @a index, for the inverse of a removal.
	auto insertZone(int index, const RackZone& zone) -> bool;
	auto removeZone(int index) -> bool;

	/**
	 * The index of the first zone - in the order the zones were added - whose
	 * key range AND velocity range both contain the note, or -1 when nothing
	 * matches.
	 *
	 * FIRST MATCH WINS, and the order is the list order, so the answer is
	 * deterministic and a caller can create a specific-over-general ladder by
	 * adding the specific zone first. The rule is stated here rather than being
	 * left to whichever loop happens to run.
	 */
	auto resolve(int key, int velocity) const -> int;

	//! Drops every zone.
	void clear();

	// --- persistence (children of the channel's <rack> element) ---

	//! Writes one <zone> element per zone, or nothing when there are none.
	void saveSettings(QDomDocument& doc, QDomElement& rackElement) const;
	//! Reads the <zone> children of @a rackElement; missing ones leave none.
	void loadSettings(const QDomElement& rackElement);

private:
	std::vector<RackZone> m_zones;
};

} // namespace lmms

#endif // LMMS_RACK_ZONES_H
