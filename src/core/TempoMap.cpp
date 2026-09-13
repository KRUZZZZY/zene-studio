/*
 * TempoMap.cpp - the tempo map's arithmetic and its project-file form.
 *
 * The value type itself is in include/TempoMap.h; this translation unit holds
 * the parts that are not one-liners, and the TWO static_asserts that pin this
 * header's tempo bounds to the engine's own (include/Song.h MinTempo/MaxTempo).
 * The bounds are repeated in the header so it stays includable without Song.h -
 * Song.h includes TempoMap.h, so the reverse would be a cycle.
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

#include "TempoMap.h"

#include <QDomDocument>
#include <QDomElement>
#include <QDomNodeList>
#include <QString>

#include "Song.h"

namespace lmms
{

static_assert(TempoMapMinTempo == MinTempo, "TempoMap's lower tempo bound drifted from Song.h's");
static_assert(TempoMapMaxTempo == MaxTempo, "TempoMap's upper tempo bound drifted from Song.h's");
static_assert(TempoMapDefaultTempo == DefaultTempo, "TempoMap's default tempo drifted from Song.h's");

namespace
{

/*! The engine's own ticks -> time conversion, in seconds.
 *
 *  TimePos::ticksToMilliseconds is the ONLY definition of that conversion in
 *  this tree, and it is what the map's integration uses too, so an event's
 *  segment and the global fallback cannot drift apart. (Its arithmetic is
 *  `ticks * 1250 / bpm` milliseconds.) */
double secondsPerTick(int bpm)
{
	return TimePos::ticksToMilliseconds(1, static_cast<bpm_t>(bpm)) / 1000.0;
}

//! Is \a tempo inside the engine's own bounds (Song.h MinTempo..MaxTempo)?
bool tempoInRange(int tempo)
{
	return tempo >= TempoMapMinTempo && tempo <= TempoMapMaxTempo;
}

//! Is this half a time signature the engine will accept? A numerator in range
//! and a denominator that is a power of two up to TempoMapLargestDenominator.
bool isSupportedTimeSignature(int numerator, int denominator)
{
	return numerator >= TempoMapMinNumerator && numerator <= TempoMapMaxNumerator
		&& denominator <= TempoMapLargestDenominator
		&& isSupportedTempoMapDenominator(denominator);
}

} // namespace

bool TempoMap::validEvent(const TempoMapEvent& event)
{
	if (!event.hasTempo && !event.hasTimeSignature) { return false; }
	if (event.tick < 0) { return false; }
	if (event.hasTempo && !tempoInRange(event.tempo)) { return false; }
	if (!event.hasTimeSignature) { return true; }
	return isSupportedTimeSignature(event.numerator, event.denominator);
}

int TempoMap::indexOfTick(tick_t tick) const
{
	for (int i = 0; i < m_count; ++i)
	{
		if (m_events[static_cast<std::size_t>(i)].tick == tick) { return i; }
	}
	return -1;
}

int TempoMap::lastTempoIndexAtOrBefore(tick_t tick) const
{
	for (int i = m_count - 1; i >= 0; --i)
	{
		const TempoMapEvent& event = m_events[static_cast<std::size_t>(i)];
		if (event.hasTempo && event.tick <= tick) { return i; }
	}
	return -1;
}

int TempoMap::lastTimeSigIndexAtOrBefore(tick_t tick) const
{
	for (int i = m_count - 1; i >= 0; --i)
	{
		const TempoMapEvent& event = m_events[static_cast<std::size_t>(i)];
		if (event.hasTimeSignature && event.tick <= tick) { return i; }
	}
	return -1;
}

bool TempoMap::addEvent(const TempoMapEvent& event)
{
	if (!validEvent(event)) { return false; }

	const int at = indexOfTick(event.tick);
	if (at >= 0)
	{
		// Add-or-replace PER PROPERTY: an event at a tick that already carries
		// the other half merges into it rather than dropping it.
		TempoMapEvent merged = m_events[static_cast<std::size_t>(at)];
		if (event.hasTempo)
		{
			merged.hasTempo = true;
			merged.tempo = event.tempo;
		}
		if (event.hasTimeSignature)
		{
			merged.hasTimeSignature = true;
			merged.numerator = event.numerator;
			merged.denominator = event.denominator;
		}
		m_events[static_cast<std::size_t>(at)] = merged;
		return true;
	}

	if (m_count >= MaxEvents) { return false; }

	int insert = 0;
	while (insert < m_count && m_events[static_cast<std::size_t>(insert)].tick < event.tick)
	{
		++insert;
	}
	for (int i = m_count; i > insert; --i)
	{
		m_events[static_cast<std::size_t>(i)] = m_events[static_cast<std::size_t>(i - 1)];
	}
	m_events[static_cast<std::size_t>(insert)] = event;
	++m_count;
	return true;
}

bool TempoMap::removeEvent(tick_t tick)
{
	const int at = indexOfTick(tick);
	if (at < 0) { return false; }
	for (int i = at; i + 1 < m_count; ++i)
	{
		m_events[static_cast<std::size_t>(i)] = m_events[static_cast<std::size_t>(i + 1)];
	}
	--m_count;
	return true;
}

bool TempoMap::set(std::span<const TempoMapEvent> events)
{
	if (events.size() > static_cast<std::size_t>(MaxEvents)) { return false; }
	TempoMap candidate;
	candidate.m_active = m_active;
	for (const TempoMapEvent& event : events)
	{
		if (!candidate.addEvent(event)) { return false; }
	}
	*this = candidate;
	return true;
}

int TempoMap::tempoAtTick(tick_t tick, int globalTempo) const
{
	// An inactive or empty map is never consulted: EVERY tick answers the global
	// tempo, which is the value the engine used before a tempo map existed.
	if (!m_active || m_count == 0) { return globalTempo; }
	const int index = lastTempoIndexAtOrBefore(tick);
	if (index < 0) { return globalTempo; }  // before the first event: global
	// On and after an event: its tempo, held until the next one; past the last
	// event the last one holds indefinitely.
	return m_events[static_cast<std::size_t>(index)].tempo;
}

TempoMapTimeSignature TempoMap::timeSignatureAtTick(tick_t tick,
	const TempoMapTimeSignature& global) const
{
	if (!m_active || m_count == 0) { return global; }
	const int index = lastTimeSigIndexAtOrBefore(tick);
	if (index < 0) { return global; }
	const TempoMapEvent& event = m_events[static_cast<std::size_t>(index)];
	return TempoMapTimeSignature{ event.numerator, event.denominator };
}

double TempoMap::secondsAtTick(tick_t tick, int globalTempo) const
{
	if (!m_active || m_count == 0)
	{
		// THE BYTE-IDENTITY PATH: the pre-change conversion, verbatim.
		return TimePos::ticksToMilliseconds(tick, static_cast<bpm_t>(globalTempo)) / 1000.0;
	}

	double seconds = 0.0;
	tick_t cursor = 0;
	int bpm = globalTempo;  // before the first tempo event: the global tempo
	for (int i = 0; i < m_count; ++i)
	{
		const TempoMapEvent& event = m_events[static_cast<std::size_t>(i)];
		// An event AT `tick` is excluded on purpose: the time already elapsed
		// when T is reached does not depend on the tempo chosen AT T, which is
		// what makes this function continuous at every event.
		if (event.tick >= tick) { break; }
		if (!event.hasTempo) { continue; }
		seconds += static_cast<double>(event.tick - cursor) * secondsPerTick(bpm);
		cursor = event.tick;
		bpm = event.tempo;
	}
	return seconds + static_cast<double>(tick - cursor) * secondsPerTick(bpm);
}

void TempoMap::segmentForSeconds(double seconds, int globalTempo, tick_t* cursor,
	double* elapsed, int* bpm) const
{
	(void)globalTempo;
	if (!m_active || m_count == 0) { return; }  // the global segment, unchanged
	int index = 0;
	while (index < m_count)
	{
		const TempoMapEvent& event = m_events[static_cast<std::size_t>(index)];
		++index;
		if (!event.hasTempo) { continue; }
		const double segment = static_cast<double>(event.tick - *cursor) * secondsPerTick(*bpm);
		if (*elapsed + segment > seconds) { return; }
		*elapsed += segment;
		*cursor = event.tick;
		*bpm = event.tempo;
	}
}

tick_t TempoMap::tickAtSeconds(double seconds, int globalTempo) const
{
	if (seconds <= 0.0) { return 0; }
	tick_t cursor = 0;
	double elapsed = 0.0;
	int bpm = globalTempo;
	segmentForSeconds(seconds, globalTempo, &cursor, &elapsed, &bpm);

	// The closed form can be one tick out at a segment boundary (a division and
	// a truncation), so it is corrected against the exact forward function
	// instead of being trusted. At most ONE step either way.
	const tick_t estimate = cursor + static_cast<tick_t>((seconds - elapsed) / secondsPerTick(bpm));
	if (estimate > 0 && secondsAtTick(estimate, globalTempo) > seconds) { return estimate - 1; }
	if (secondsAtTick(estimate + 1, globalTempo) <= seconds) { return estimate + 1; }
	return estimate;
}

float TempoMap::framesPerTickAtTick(tick_t tick, sample_rate_t sampleRate, int globalTempo) const
{
	const int bpm = tempoAtTick(tick, globalTempo);
	// ENGINE-EXACT: this is Engine::updateFramesPerTick()'s expression verbatim,
	// so an empty (or inactive) map evaluates to the engine's own scalar
	// bit-for-bit. bpm is validated non-zero on the way in.
	return sampleRate * 60.0f * 4 / DefaultTicksPerBar / bpm;
}

double TempoMap::framesAtTick(tick_t tick, sample_rate_t sampleRate, int globalTempo) const
{
	return secondsAtTick(tick, globalTempo) * static_cast<double>(sampleRate);
}

tick_t TempoMap::tickAtFrame(f_cnt_t frame, sample_rate_t sampleRate, int globalTempo) const
{
	if (sampleRate == 0) { return 0; }
	return tickAtSeconds(static_cast<double>(frame) / static_cast<double>(sampleRate), globalTempo);
}

void TempoMap::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement mapElement = doc.createElement(QStringLiteral("tempo-map"));
	mapElement.setAttribute(QStringLiteral("active"), m_active ? 1 : 0);
	mapElement.setAttribute(QStringLiteral("events"), m_count);
	mapElement.setAttribute(QStringLiteral("max-events"), MaxEvents);
	for (int i = 0; i < m_count; ++i)
	{
		const TempoMapEvent& event = m_events[static_cast<std::size_t>(i)];
		QDomElement eventElement = doc.createElement(QStringLiteral("event"));
		eventElement.setAttribute(QStringLiteral("tick"), static_cast<int>(event.tick));
		if (event.hasTempo)
		{
			eventElement.setAttribute(QStringLiteral("bpm"), event.tempo);
		}
		if (event.hasTimeSignature)
		{
			eventElement.setAttribute(QStringLiteral("numerator"), event.numerator);
			eventElement.setAttribute(QStringLiteral("denominator"), event.denominator);
		}
		mapElement.appendChild(eventElement);
	}
	parent.appendChild(mapElement);
}

bool TempoMap::loadSettings(const QDomElement& element)
{
	clear();
	if (element.isNull()) { return false; }
	setActive(element.attribute(QStringLiteral("active"), QStringLiteral("0")).toInt() != 0);

	bool any = false;
	const QDomNodeList events = element.elementsByTagName(QStringLiteral("event"));
	for (int i = 0; i < events.count(); ++i)
	{
		const QDomElement node = events.at(i).toElement();
		if (node.isNull()) { continue; }
		TempoMapEvent event;
		event.tick = static_cast<tick_t>(
			node.attribute(QStringLiteral("tick"), QStringLiteral("0")).toInt());
		if (node.hasAttribute(QStringLiteral("bpm")))
		{
			event.hasTempo = true;
			event.tempo = node.attribute(QStringLiteral("bpm")).toInt();
		}
		if (node.hasAttribute(QStringLiteral("numerator"))
			&& node.hasAttribute(QStringLiteral("denominator")))
		{
			event.hasTimeSignature = true;
			event.numerator = node.attribute(QStringLiteral("numerator")).toInt();
			event.denominator = node.attribute(QStringLiteral("denominator")).toInt();
		}
		if (addEvent(event)) { any = true; }
	}

	// An element holding nothing this build accepts loads as the EMPTY map -
	// the state a project with no tempo map is in - so a truncated or future
	// block degrades to "no map" and never to a wrong timeline.
	if (!any) { clear(); }
	return any;
}

} // namespace lmms
