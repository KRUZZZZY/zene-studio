/*
 * ChordTrack.cpp - the project's chord track: ordering, lookup and the
 *                  <chord-track> element
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "ChordTrack.h"

#include <QDomDocument>
#include <QDomElement>

#include "ChordVocabulary.h"

namespace lmms
{

namespace
{

const QString kTrackElement = QStringLiteral("chord-track");
const QString kEventElement = QStringLiteral("chord");

// The attributes, spelled once. The names are what the project file carries,
// so they are part of the format and are not derived from the C++ field names.
const QString kPos = QStringLiteral("pos");
const QString kLength = QStringLiteral("len");
const QString kRoot = QStringLiteral("root");
const QString kOctave = QStringLiteral("octave");
const QString kChord = QStringLiteral("chord");
const QString kScale = QStringLiteral("scale");

} // namespace


int ChordTrack::indexAt(tick_t pos) const
{
	for (int index = 0; index < size(); ++index)
	{
		if (m_events[static_cast<std::size_t>(index)].pos == pos) { return index; }
	}
	return -1;
}


const ChordEvent* ChordTrack::findAt(tick_t pos) const
{
	const int index = indexAt(pos);
	return index < 0 ? nullptr : &m_events[static_cast<std::size_t>(index)];
}


tick_t ChordTrack::effectiveLength(int index, tick_t fallbackTicks) const
{
	if (index < 0 || index >= size()) { return fallbackTicks; }
	const ChordEvent& event = m_events[static_cast<std::size_t>(index)];
	if (event.length > 0) { return event.length; }
	if (index + 1 < size())
	{
		const tick_t distance = m_events[static_cast<std::size_t>(index) + 1].pos - event.pos;
		if (distance > 0) { return distance; }
	}
	return fallbackTicks;
}


bool ChordTrack::isWritable(const ChordEvent& event, QString* error)
{
	if (!rangesAreWritable(event, error)) { return false; }
	return nameResolves(event, error);
}


bool ChordTrack::rangesAreWritable(const ChordEvent& event, QString* error)
{
	const auto fail = [error](const QString& reason) {
		if (error != nullptr) { *error = reason; }
		return false;
	};

	if (event.pos < 0)
	{
		return fail(QStringLiteral("the position is %1; a chord track starts at tick 0")
			.arg(static_cast<qint64>(event.pos)));
	}
	if (event.length < 0)
	{
		return fail(QStringLiteral("the length is %1; it is non-negative, and 0 means "
			"\\\"until the next chord\\\"").arg(static_cast<qint64>(event.length)));
	}
	if (event.length > 0 && event.length < MinEventTicks)
	{
		return fail(QStringLiteral("the length is %1; the shortest chord is %2 tick(s)")
			.arg(static_cast<qint64>(event.length)).arg(MinEventTicks));
	}
	if (event.root < 0 || event.root > 11)
	{
		return fail(QStringLiteral("the root is %1; a root is a pitch class 0..11 (C = 0)")
			.arg(event.root));
	}
	if (event.octave < ChordVocabulary::MinOctave || event.octave > ChordVocabulary::MaxOctave)
	{
		return fail(QStringLiteral("the octave is %1; this engine's octaves are %2..%3")
			.arg(event.octave).arg(ChordVocabulary::MinOctave).arg(ChordVocabulary::MaxOctave));
	}
	return true;
}


bool ChordTrack::nameResolves(const ChordEvent& event, QString* error)
{
	if (ChordVocabulary::chordByName(event.chord) == nullptr)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("'%1' is not a chord of this engine's vocabulary (see "
				"chord.progression_list for the %2 it knows)")
				.arg(event.chord).arg(ChordVocabulary::chordNames().size());
		}
		return false;
	}
	if (!event.scale.isEmpty() && ChordVocabulary::scaleByName(event.scale) == nullptr)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("'%1' is not a scale of this engine's vocabulary (see "
				"chord.progression_list for the %2 it knows)")
				.arg(event.scale).arg(ChordVocabulary::scaleNames().size());
		}
		return false;
	}
	return true;
}


bool ChordTrack::set(const ChordEvent& event, bool* replaced)
{
	if (replaced != nullptr) { *replaced = false; }
	if (!isWritable(event, nullptr)) { return false; }

	const int existing = indexAt(event.pos);
	if (existing >= 0)
	{
		m_events[static_cast<std::size_t>(existing)] = event;
		if (replaced != nullptr) { *replaced = true; }
		return true;
	}
	if (size() >= MaxEvents) { return false; }

	// Insertion keeps the ascending-position invariant the class promises, so
	// nothing downstream has to sort and index i is always the i-th chord.
	std::size_t slot = 0;
	while (slot < m_events.size() && m_events[slot].pos < event.pos) { ++slot; }
	m_events.insert(m_events.begin() + static_cast<std::ptrdiff_t>(slot), event);
	return true;
}


bool ChordTrack::removeAt(tick_t pos)
{
	const int index = indexAt(pos);
	if (index < 0) { return false; }
	m_events.erase(m_events.begin() + index);
	return true;
}


QDomElement ChordTrack::toElement(QDomDocument& doc) const
{
	QDomElement trackElement = doc.createElement(kTrackElement);
	trackElement.setAttribute(QStringLiteral("chords"), size());
	trackElement.setAttribute(QStringLiteral("max-chords"), MaxEvents);
	for (const ChordEvent& event : m_events)
	{
		QDomElement chordElement = doc.createElement(kEventElement);
		chordElement.setAttribute(kPos, static_cast<qint64>(event.pos));
		// len is written ONLY when it is set: 0 is the default and the format
		// carries it as an absence, the rule the note attributes follow.
		if (event.length > 0) { chordElement.setAttribute(kLength, static_cast<qint64>(event.length)); }
		chordElement.setAttribute(kRoot, event.root);
		chordElement.setAttribute(kOctave, event.octave);
		chordElement.setAttribute(kChord, event.chord);
		if (!event.scale.isEmpty()) { chordElement.setAttribute(kScale, event.scale); }
		trackElement.appendChild(chordElement);
	}
	return trackElement;
}


void ChordTrack::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	parent.appendChild(toElement(doc));
}


bool ChordTrack::loadSettings(const QDomElement& element)
{
	// CLEARED FIRST, unconditionally - see the header: the state an absent
	// element has to restore is the empty track.
	clear();
	if (element.isNull() || element.tagName() != kTrackElement) { return false; }

	for (QDomElement child = element.firstChildElement(kEventElement); !child.isNull();
		child = child.nextSiblingElement(kEventElement))
	{
		ChordEvent event;
		event.pos = static_cast<tick_t>(child.attribute(kPos).toLongLong());
		event.length = static_cast<tick_t>(child.attribute(kLength, QStringLiteral("0")).toLongLong());
		event.root = child.attribute(kRoot, QStringLiteral("0")).toInt();
		event.octave = child.attribute(kOctave, QStringLiteral("4")).toInt();
		event.chord = child.attribute(kChord);
		event.scale = child.attribute(kScale);
		// An unreadable event is skipped rather than loaded as a wrong chord:
		// a name this build cannot resolve is a file this build cannot play.
		set(event, nullptr);
	}
	return true;
}


QString ChordTrack::toXml() const
{
	// The track element IS the document root, so the text is a well-formed
	// document fromXml() can parse straight back (and a reader can diff).
	QDomDocument doc(QStringLiteral("zene-chord-track"));
	doc.appendChild(toElement(doc));
	return doc.toString();
}


bool ChordTrack::fromXml(const QString& xml)
{
	clear();
	QDomDocument doc;
	if (!doc.setContent(xml)) { return false; }
	return loadSettings(doc.documentElement());
}

} // namespace lmms
