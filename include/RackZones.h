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

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;

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
 * The ranges are inclusive. `sample` is an optional reference to an audio file
 * (a path, or one of the engine's `factorysample:` / `usersample:` prefixes); it
 * does not take part in matching. 0.5.0 E38: the reference is LOADED (decoded on
 * the control thread into `loaded`) when the zone is added or read from a
 * project, and a note the zone claims then plays that sample on the zone's chain
 * instead of the instrument's own sound for that note - at the pitch the
 * instrument track gives the note (the sample's own pitch at the track's base
 * note, as AudioFileProcessor plays one) and at the note's velocity. A reference
 * that does not load (missing file, unreadable data) is KEPT - it saves back
 * unchanged and reports `sample_loaded: false` - and the zone then routes the
 * instrument's own note to its chain, as a zone without a sample does.
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
	//! E38: the decoded sample, or nullptr (no reference, or it did not load).
	//! Shared, immutable once loaded: a copy of the zone (an undo step) plays the
	//! same data without decoding again.
	std::shared_ptr<const SampleBuffer> loaded;
};

/*! E38: decodes @a zone's sample reference into `zone.loaded` when it has one and
 *  it is not loaded yet. Control thread (it reads a file). True when the zone
 *  ends with a playable sample. Never shows a dialog: a project read by an agent
 *  has nobody to answer one, and a missing file is reported, not refused. */
LMMS_EXPORT bool loadRackZoneSample(RackZone& zone);

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
 *
 * 0.5.0 E20: the audio workers read the list while notes play (RackZoneFeed.h),
 * so every mutation takes the audio engine's model-change guard, and a sample is
 * decoded BEFORE the guard is taken - the render never waits on a file. The
 * owning Rack is told after each change (setChangedHook) so it can re-wire.
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
	//! @a alongside runs inside the same edit, after the list changes and before the changed hook: a caller
	//! whose own state must publish with the list (a layer slot) passes it here rather than as a second edit.
	auto addZone(const RackZone& zone, const std::function<void()>& alongside = {}) -> int;
	//! Inserts a zone at @a index, for the inverse of a removal.
	auto insertZone(int index, const RackZone& zone) -> bool;
	auto removeZone(int index) -> bool;
	//! 0.5.0 MX-112: replaces the whole list in one edit (a layer removed renumbers the zones it leaves).
	//! @a alongside runs in that edit, as addZone's does.
	void assign(std::vector<RackZone> zones, const std::function<void()>& alongside = {});

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

	/*! 0.5.0 MX-112: every matching instrument layer chain, one bit per chain index, accumulated over
	 *  all zones whose key range and velocity range contain the note. Layer racks have at most nine
	 *  chains, so this stays complete regardless of how many zones are stored. The routing racks keep
	 *  resolve(). A read: no allocation, no lock. */
	auto matchMask(int key, int velocity) const -> std::uint64_t;

	//! Drops every zone.
	void clear();

	/*! Luna cluster 5256: keeps the zone list consistent with a rack that lost
	 *  chain @a removedIndex - a zone that named a LATER chain slides down with
	 *  it, and a zone that named the removed chain is dropped, because an index
	 *  that names nothing is a note that silently takes the common path.
	 *  Called by Rack::removeChain(); the guard nests. */
	void reindexChains(int removedIndex);

	//! Called after every change, inside the model-change guard (the owning Rack re-wires).
	void setChangedHook(std::function<void()> hook) { m_changed = std::move(hook); }

	// --- persistence (children of the channel's <rack> element) ---

	//! Writes one <zone> element per zone, or nothing when there are none.
	void saveSettings(QDomDocument& doc, QDomElement& rackElement) const;
	//! Reads the <zone> children of @a rackElement; missing ones leave none.
	void loadSettings(const QDomElement& rackElement);

private:
	//! Runs @a change on the list under the model-change guard, then the hook.
	void edit(const std::function<void()>& change);

	std::vector<RackZone> m_zones;
	std::function<void()> m_changed;
};

} // namespace lmms

#endif // LMMS_RACK_ZONES_H
