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

#include <algorithm>
#include <limits>

#include <QDomDocument>
#include <QDomElement>

#include "AudioEngine.h"
#include "ChordVocabulary.h"
#include "Engine.h"
#include "Note.h"

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
// 0.5.0 E23: one <follow track= mode=> child per following track.
const QString kFollowElement = QStringLiteral("follow");
const QString kFollowTrack = QStringLiteral("track");
const QString kFollowMode = QStringLiteral("mode");

//! The pitch classes of @a chord (a chord or scale entry) on @a root, one bit each; 0 for no entry.
std::uint16_t toneMask(const ChordVocabulary::Chord* chord, int root)
{
	if (chord == nullptr) { return 0; }
	std::uint16_t mask = 0;
	for (const int pitchClass : ChordVocabulary::pitchClassesOf(*chord, root)) { mask |= std::uint16_t(1u << pitchClass); }
	return mask;
}

//! The key nearest @a key whose pitch class is in @a mask (down on a tie), or @a key. No allocation.
int nearestTone(int key, std::uint16_t mask)
{
	if (mask == 0) { return key; }
	for (int distance = 0; distance < 12; ++distance)
	{
		for (const int candidate : {key - distance, key + distance})
		{
			if (candidate < 0 || candidate >= NumKeys) { continue; }
			if ((mask >> ChordVocabulary::pitchClassOf(candidate) & 1) != 0) { return candidate; }
		}
	}
	return key;
}

} // namespace


void ChordTrack::edit(const std::function<void()>& change)
{
	// followedKey() reads the events, their tones and the followers on the audio thread.
	AudioEngine* engine = Engine::audioEngine();
	if (engine != nullptr) { engine->requestChangeInModel(); }
	change();
	m_tones.resize(m_events.size());
	for (std::size_t i = 0; i < m_events.size(); ++i)
	{
		const ChordEvent& event = m_events[i];
		m_tones[i].chord = toneMask(ChordVocabulary::chordByName(event.chord), event.root);
		m_tones[i].scale = event.scale.isEmpty() ? 0 : toneMask(ChordVocabulary::scaleByName(event.scale), event.root);
	}
	++m_revision;
	if (engine != nullptr) { engine->doneChangeInModel(); }
}


void ChordTrack::clear()
{
	edit([this] {
		m_events.clear();
		m_followers.clear();
	});
}


QString ChordTrack::followModeName(ChordFollowMode mode)
{
	switch (mode)
	{
	case ChordFollowMode::Chord: return QStringLiteral("chord");
	case ChordFollowMode::Scale: return QStringLiteral("scale");
	default: return QStringLiteral("off");
	}
}


bool ChordTrack::followModeFromName(const QString& name, ChordFollowMode* mode)
{
	for (const ChordFollowMode candidate : {ChordFollowMode::Off, ChordFollowMode::Chord, ChordFollowMode::Scale})
	{
		if (name == followModeName(candidate)) { *mode = candidate; return true; }
	}
	return false;
}


void ChordTrack::clearEvents()
{
	edit([this] { m_events.clear(); });
}


ChordFollowMode ChordTrack::followOf(int trackId) const
{
	for (const ChordFollower& follower : m_followers)
	{
		if (follower.trackId == trackId) { return follower.mode; }
	}
	return ChordFollowMode::Off;
}


bool ChordTrack::setFollow(int trackId, ChordFollowMode mode)
{
	if (trackId < 0) { return false; }
	const auto it = std::find_if(m_followers.begin(), m_followers.end(),
		[trackId](const ChordFollower& follower) { return follower.trackId == trackId; });
	if (it == m_followers.end() && mode != ChordFollowMode::Off && static_cast<int>(m_followers.size()) >= MaxFollowers)
	{
		return false;
	}
	edit([this, it, trackId, mode] {
		if (it != m_followers.end() && mode == ChordFollowMode::Off) { m_followers.erase(it); }
		else if (it != m_followers.end()) { it->mode = mode; }
		else if (mode != ChordFollowMode::Off) { m_followers.push_back({trackId, mode}); }
	});
	return true;
}


int ChordTrack::followedKey(int trackId, int key, tick_t pos) const
{
	const ChordFollowMode mode = followOf(trackId);
	if (mode == ChordFollowMode::Off) { return key; }
	const int index = soundingIndex(pos);
	if (index < 0 || index >= static_cast<int>(m_tones.size())) { return key; }
	const EventTones& tones = m_tones[static_cast<std::size_t>(index)];
	return nearestTone(key, mode == ChordFollowMode::Scale && tones.scale != 0 ? tones.scale : tones.chord);
}


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


int ChordTrack::soundingIndex(tick_t pos) const
{
	int last = -1;
	for (int index = 0; index < size() && m_events[static_cast<std::size_t>(index)].pos <= pos; ++index) { last = index; }
	if (last < 0) { return -1; }
	const ChordEvent& event = m_events[static_cast<std::size_t>(last)];
	return pos - event.pos < effectiveLength(last, DefaultEventTicks) ? last : -1;  // no pos + length overflow
}


const ChordEvent* ChordTrack::soundingAt(tick_t pos) const
{
	const int index = soundingIndex(pos);
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
		edit([this, existing, &event] { m_events[static_cast<std::size_t>(existing)] = event; });
		if (replaced != nullptr) { *replaced = true; }
		return true;
	}
	if (size() >= MaxEvents) { return false; }

	// Insertion keeps the ascending-position invariant the class promises, so
	// nothing downstream has to sort and index i is always the i-th chord.
	std::size_t slot = 0;
	while (slot < m_events.size() && m_events[slot].pos < event.pos) { ++slot; }
	edit([this, slot, &event] { m_events.insert(m_events.begin() + static_cast<std::ptrdiff_t>(slot), event); });
	return true;
}


bool ChordTrack::removeAt(tick_t pos)
{
	const int index = indexAt(pos);
	if (index < 0) { return false; }
	edit([this, index] { m_events.erase(m_events.begin() + index); });
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
	for (const ChordFollower& follower : m_followers)
	{
		QDomElement followElement = doc.createElement(kFollowElement);
		followElement.setAttribute(kFollowTrack, follower.trackId);
		followElement.setAttribute(kFollowMode, followModeName(follower.mode));
		trackElement.appendChild(followElement);
	}
	return trackElement;
}


void ChordTrack::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	parent.appendChild(toElement(doc));
}


namespace
{

//! Whether \a value is a whole number of ticks a tick_t can hold.
bool fitsTick(qint64 value)
{
	return value >= std::numeric_limits<tick_t>::min() && value <= std::numeric_limits<tick_t>::max();
}

/*! The one REQUIRED numeric attribute of a <chord>: pos, because nothing else
 *  identifies the event. Absent or malformed is not a chord at tick 0. */
bool requiredTick(const QDomElement& child, const QString& key, tick_t* out)
{
	bool ok = false;
	const qint64 value = child.attribute(key).toLongLong(&ok);
	if (!ok || !fitsTick(value)) { return false; }
	*out = static_cast<tick_t>(value);
	return true;
}

/*! An OPTIONAL tick attribute: an ABSENT one keeps \a fallback (old files do
 *  not carry len), but a PRESENT one that is not a whole number of ticks is
 *  malformed and is refused rather than read as the fallback. */
bool optionalTick(const QDomElement& child, const QString& key, tick_t fallback, tick_t* out)
{
	if (!child.hasAttribute(key)) { *out = fallback; return true; }
	const QString text = child.attribute(key);
	bool ok = false;
	const qint64 value = text.toLongLong(&ok);
	if (!ok || !fitsTick(value)) { return false; }
	*out = static_cast<tick_t>(value);
	return true;
}

//! An OPTIONAL int attribute: absent keeps \a fallback, malformed is refused.
bool optionalInt(const QDomElement& child, const QString& key, int fallback, int* out)
{
	if (!child.hasAttribute(key)) { *out = fallback; return true; }
	const QString text = child.attribute(key);
	bool ok = false;
	*out = text.toInt(&ok);
	return ok;
}

/*! One <chord> element as an event, or false when it cannot be read as one.
 *  Every NUMBER is checked for having parsed: a default would not fail the
 *  load but silently invent a different, perfectly valid chord - an absent or
 *  malformed pos becomes a chord at tick 0, a malformed octave becomes octave
 *  4. The pos is required; len, root and octave keep their documented defaults
 *  only where the file does not carry them. */
bool eventFromElement(const QDomElement& child, ChordEvent* out)
{
	if (!requiredTick(child, kPos, &out->pos)) { return false; }
	if (!optionalTick(child, kLength, 0, &out->length)) { return false; }
	if (!optionalInt(child, kRoot, 0, &out->root)) { return false; }
	// 4 is the writer's and the control layer's own default octave.
	if (!optionalInt(child, kOctave, 4, &out->octave)) { return false; }
	out->chord = child.attribute(kChord);
	out->scale = child.attribute(kScale);
	return true;
}

} // namespace


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
		if (!eventFromElement(child, &event)) { continue; }
		// An unreadable event is skipped rather than loaded as a wrong chord:
		// a name this build cannot resolve is a file this build cannot play.
		set(event, nullptr);
	}
	for (QDomElement child = element.firstChildElement(kFollowElement); !child.isNull();
		child = child.nextSiblingElement(kFollowElement))
	{
		ChordFollowMode mode = ChordFollowMode::Chord;  // an unknown mode follows the chord, the default
		followModeFromName(child.attribute(kFollowMode), &mode);
		// A number that does not parse is not a zero: 0 is a valid track ID, so
		// a malformed follower ID is SKIPPED, the same rule the malformed event
		// numbers follow - defaulting would silently make track 0 follow.
		bool trackIdParsed = false;
		const int trackId = child.attribute(kFollowTrack, QStringLiteral("-1")).toInt(&trackIdParsed);
		if (trackIdParsed) { setFollow(trackId, mode); }
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
	// Parse and validate BEFORE replacing anything: fromXml() restores a
	// snapshot over the live track, so a corrupt snapshot must leave it alone.
	// loadSettings() is the project loader's path and still clears first -
	// there an absent element legitimately means "no chord track at all".
	QDomDocument doc;
	if (!doc.setContent(xml)) { return false; }
	const QDomElement element = doc.documentElement();
	if (element.isNull() || element.tagName() != kTrackElement) { return false; }
	return loadSettings(element);
}

} // namespace lmms
