/*
 * TempoMap.h - the tempo map: tempo and time-signature events on the timeline
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

#ifndef LMMS_TEMPO_MAP_H
#define LMMS_TEMPO_MAP_H

#include <array>
#include <atomic>
#include <cmath>
#include <span>

#include "LmmsTypes.h"
#include "TimePos.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/* The tempo bounds are the engine's own (include/Song.h MinTempo/MaxTempo).
 * They are repeated here as constexpr ONLY so this header does not have to
 * include Song.h - Song.h includes THIS header. TempoMap.cpp static_asserts the
 * two pairs equal, so the repetition cannot drift. */
constexpr int TempoMapMinTempo = 10;
constexpr int TempoMapMaxTempo = 999;
constexpr int TempoMapDefaultTempo = 140;

constexpr int TempoMapMinNumerator = 1;
constexpr int TempoMapMaxNumerator = 32;
constexpr int TempoMapLargestDenominator = 32;

//! True for a denominator a time signature may use (a power of two, as the
//! engine's own meter widgets offer).
inline bool isSupportedTempoMapDenominator(int denominator)
{
	return denominator == 1 || denominator == 2 || denominator == 4
		|| denominator == 8 || denominator == 16 || denominator == 32;
}

//! A time signature, as the map hands one out.
struct TempoMapTimeSignature
{
	int numerator = 4;
	int denominator = 4;

	bool operator==(const TempoMapTimeSignature& other) const
	{
		return numerator == other.numerator && denominator == other.denominator;
	}
	bool operator!=(const TempoMapTimeSignature& other) const { return !(*this == other); }
};

/*! One event: at \a tick the tempo becomes \a tempo and/or the time signature
 *  becomes \a numerator / \a denominator.
 *
 *  Either half may be absent, and the two halves are independent: a tempo-only
 *  event does not disturb the time signature in force, and a time-signature-only
 *  event does not disturb the tempo. That is what lets a project carry a tempo
 *  curve and a metre change on the same grid without the two fighting.
 *
 *  `denominator` is 0 when `hasTimeSignature` is false, and is a power of two
 *  otherwise - `validEvent()` is the one place that is enforced.
 */
struct TempoMapEvent
{
	tick_t tick = 0;
	bool hasTempo = false;
	int tempo = TempoMapDefaultTempo;
	bool hasTimeSignature = false;
	int numerator = 4;
	int denominator = 4;

	bool operator==(const TempoMapEvent& other) const
	{
		return tick == other.tick && hasTempo == other.hasTempo && tempo == other.tempo
			&& hasTimeSignature == other.hasTimeSignature
			&& numerator == other.numerator && denominator == other.denominator;
	}
	bool operator!=(const TempoMapEvent& other) const { return !(*this == other); }
};

/*! The tempo map: an ordered set of tempo and time-signature events.
 *
 *  **Map shape (decision 1, docs/TEMPO-MAP.md section 2).** ONE ordered list of
 *  events, each carrying an optional tempo and an optional time signature; the
 *  list is kept strictly increasing in tick and merging is add-or-replace PER
 *  PROPERTY, so adding a tempo at a tick that already carries a metre keeps the
 *  metre. `tempoAtTick()` and `timeSignatureAtTick()` are independent step
 *  functions over that one list, each skipping events that do not carry the
 *  property it is looking for. The capacity is a compile-time constant, which is
 *  what makes every authoring call and every query allocation-free.
 *
 *  **Out-of-range behaviour (decision 2).** The map is a step function whose
 *  domain starts at its first event: on and after an event the event's value
 *  holds, and past the LAST event it holds indefinitely; BEFORE the first event
 *  the GLOBAL tempo (and time signature) is in force, i.e. exactly the value the
 *  engine used before a tempo map existed. Two properties follow, and both are
 *  load-bearing:
 *    - a map that names tick 0 is the total override (the map's own set_tempo);
 *    - a map with no event at or before a tick does not touch that tick, so
 *      adding an event at bar 16 can never retime bars 1-15.
 *  The alternative - returning the FIRST event's value before it happens - would
 *  silently retime material the user never mapped.
 *
 *  **The empty map is the pre-change arithmetic, verbatim.** An empty map is
 *  never consulted (`active()` false or `size()` 0 falls through to the global
 *  tempo on every path), and `framesPerTickAtTick()` then evaluates the exact
 *  expression `Engine::updateFramesPerTick()` uses. The byte-identity claim of
 *  this release rests on that, and `tests/src/core/TempoMapTest.cpp` measures it
 *  rather than asserting it.
 */
class TempoMap
{
public:
	//! A fixed capacity is what keeps every operation allocation-free.
	static constexpr int MaxEvents = 128;

	bool empty() const { return m_count == 0; }
	int size() const { return m_count; }
	const TempoMapEvent& operator[](int index) const
	{
		return m_events[static_cast<std::size_t>(index)];
	}
	std::span<const TempoMapEvent> all() const
	{
		return { m_events.data(), static_cast<std::size_t>(m_count) };
	}

	//! Whether the timeline obeys the map at all. An inactive map answers the
	//! global tempo (and signature) for EVERY tick, whatever it holds.
	bool active() const { return m_active; }
	void setActive(bool active) { m_active = active; }

	/*! True when the map has to be written into the project file. An INACTIVE
	 *  map with no event writes nothing - which is what keeps a project that
	 *  never used a tempo map byte-identical across this change. */
	bool shouldPersist() const { return m_active || m_count > 0; }

	void clear()
	{
		m_count = 0;
		m_active = false;
	}

	/*! Is this event one the engine will accept? A closed rule, in one place:
	 *  at least one half present, a non-negative tick, a tempo inside the
	 *  engine's own bounds, a numerator inside [1, 32] and a denominator that is
	 *  a power of two. */
	static bool validEvent(const TempoMapEvent& event);

	/*! Add \a event, or merge it into the event already at its tick (per
	 *  property). False - and NO change - for an invalid event or a full map. */
	bool addEvent(const TempoMapEvent& event);

	//! Remove the event at exactly \a tick; false when there is none.
	bool hasEventAt(tick_t tick) const { return indexOfTick(tick) >= 0; }

	bool removeEvent(tick_t tick);

	//! Replace the whole set (authoring call). False - and no change - when any
	//! event is invalid, two events share a tick, or the set is too large.
	bool set(std::span<const TempoMapEvent> events);

	int tempoAtTick(tick_t tick, int globalTempo) const;
	TempoMapTimeSignature timeSignatureAtTick(tick_t tick,
		const TempoMapTimeSignature& global) const;

	/* --- the timeline <-> ticks conversions, reading the map ------------- */

	//! Seconds from tick 0 to \a tick, integrating each tempo segment.
	//! Continuous at every event: moving the tempo at tick T cannot change the
	//! time already elapsed when T is reached.
	double secondsAtTick(tick_t tick, int globalTempo) const;

	//! The inverse: the largest tick whose elapsed time is <= \a seconds.
	tick_t tickAtSeconds(double seconds, int globalTempo) const;

	/*! The frames-per-tick rate in force AT \a tick - a step function, so this
	 *  is the rate a render uses from \a tick onwards. With an empty or inactive
	 *  map this is `Engine::updateFramesPerTick()`'s expression verbatim. */
	float framesPerTickAtTick(tick_t tick, sample_rate_t sampleRate, int globalTempo) const;

	//! Frames from tick 0 to \a tick at the map's own rates.
	double framesAtTick(tick_t tick, sample_rate_t sampleRate, int globalTempo) const;
	tick_t tickAtFrame(f_cnt_t frame, sample_rate_t sampleRate, int globalTempo) const;

	bool operator==(const TempoMap& other) const
	{
		return m_active == other.m_active && m_count == other.m_count
			&& std::equal(m_events.begin(), m_events.begin() + m_count, other.m_events.begin());
	}
	bool operator!=(const TempoMap& other) const { return !(*this == other); }

	//! Serialise the map as a `<tempo-map>` child of \a parent. The CALLER
	//! decides whether to call this; shouldPersist() is the rule.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Read a `<tempo-map>` element back. The map is left EMPTY - the state a
	//! project with no map loads into - when the element holds no valid event.
	bool loadSettings(const QDomElement& element);

private:
	//! Last index whose event carries a tempo and sits at or before \a tick.
	int lastTempoIndexAtOrBefore(tick_t tick) const;
	//! Last index whose event carries a time signature and sits at or before
	//! \a tick.
	int lastTimeSigIndexAtOrBefore(tick_t tick) const;
	//! The index of the event at exactly \a tick, or -1.
	int indexOfTick(tick_t tick) const;
	/*! The tempo segment that CONTAINS \a seconds: walks the tempo events and
	 *  leaves the segment's first tick, the seconds elapsed at it and its tempo
	 *  in \a cursor / \a elapsed / \a bpm (which start as the global segment). */
	void segmentForSeconds(double seconds, int globalTempo, tick_t* cursor,
		double* elapsed, int* bpm) const;

	std::array<TempoMapEvent, MaxEvents> m_events{};
	int m_count = 0;
	bool m_active = false;
};

/*! The map plus the lock-free hand-off of it to the audio thread.
 *
 *  The map is authored on the control thread (the transport.* commands) and
 *  read on the audio thread (Song::followTempoMap, once per block). A reader
 *  must never see a half-written set, and it may neither lock nor allocate, so
 *  the hand-off is a seqlock: a writer brackets its edit with an ODD -> EVEN
 *  version bump, and a reader copies the fixed-size value and re-checks the
 *  version, retrying while a write is in progress.
 *
 *  Honest limits, because this is the one place the technique needs saying:
 *  the version bumps are the only atomics, so the value copy itself is a benign
 *  data race by the letter of the C++ memory model - the same trade every
 *  seqlock makes, and it is sound here because there is exactly ONE writer (the
 *  control thread) and it publishes only when an edit lands, not per block. The
 *  reader's cost is a fixed 128-event copy (about 3 KiB on the stack) and no
 *  retry in practice; a reader that has to retry re-reads, it never blocks.
 *
 *  When the map is NOT active - the default, and every project that predates
 *  the feature - the audio thread does not even call snapshot(): the timing path
 *  reads `active()` and returns.
 */
class TempoMapPublisher
{
public:
	/*! One mutation of the map, published the moment it lands. Routing every
	 *  write through here is what makes a forgotten publish impossible; \a fn
	 *  returns true when it changed the map. */
	template <typename Fn>
	bool edit(Fn&& fn)
	{
		m_version.fetch_add(1, std::memory_order_acq_rel);
		const bool changed = fn(m_map);
		m_version.fetch_add(1, std::memory_order_acq_rel);
		return changed;
	}

	//! The authoritative map. Control thread only (it is the value being
	//! written); the audio thread uses snapshot().
	const TempoMap& map() const noexcept { return m_map; }

	//! The map as of the last completed edit. Lock-free, allocation-free and
	//! bounded; safe to call from the audio thread.
	TempoMap snapshot() const noexcept
	{
		TempoMap copy;
		unsigned version = 0;
		do
		{
			version = m_version.load(std::memory_order_acquire);
			if ((version & 1u) != 0u) { continue; }  // a write is in progress
			copy = m_map;
		}
		while (m_version.load(std::memory_order_acquire) != version);
		return copy;
	}

private:
	TempoMap m_map{};
	std::atomic<unsigned> m_version{0};
};

} // namespace lmms

#endif // LMMS_TEMPO_MAP_H
