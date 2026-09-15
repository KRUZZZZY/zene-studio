/*
 * ChordTrack.h - the project's chord track: an ordered list of chords that
 *                persists in the project file
 *
 * WHAT IT IS. One chord event per position: which chord (a name from the
 * pre-existing vocabulary - include/ChordVocabulary.h is a view over
 * ChordTable, and this class adds no table of its own), which root pitch class,
 * in which octave, for how long. A chord track is the DAW's own way of writing
 * harmony down SEPARATELY from the notes: FL's chord rack, Live's scale-aware
 * chord tools and Cubase's chord track all keep the harmony as an entity so it
 * can be edited as harmony - transposed, re-voiced, re-generated into a clip -
 * instead of being inferred from (and destroyed by) the notes every time.
 *
 * WHERE IT LIVES, AND WHY THAT DECIDES ITS REVERSIBILITY. The track is project
 * state on the Song, serialised as ONE <chord-track> element inside <song> and
 * written ONLY when it holds an event, so a project that never used a chord
 * re-saves byte for byte as before (the rule TempoMap::shouldPersist,
 * ModulationLayer::shouldPersist and GroovePool::shouldPersist all follow). It
 * is NOT inside the track container and is not a JournallingObject, so a Song
 * journal checkpoint does not carry it: the inverse of an edit is a recorded
 * ACTION checkpoint that writes the captured element back
 * (control::addUndoStep), never a claimed checkpoint. The commands that write
 * NOTES from this track reverse through the CLIP's checkpoint instead, because
 * what they change is the clip's note list.
 *
 * BOUNDS, stated so nothing here is implicit:
 *   - at most MaxEvents events (a set past the bound is refused, and the bound
 *     is reported on the wire);
 *   - positions are non-negative and events are kept in ascending position
 *     order; an event's position is its identity, so setting one at a position
 *     that already holds one REPLACES it (the groove pool's name-as-key rule);
 *   - length 0 means "until the next event" (or the caller's own default for
 *     the last one), which is how a chord track holds a harmony without
 *     repeating a length on every event;
 *   - the chord name must be a CHORD entry and the scale name a SCALE entry of
 *     the pre-existing vocabulary, or the event is refused - the track cannot
 *     hold a name nothing else in the product can read.
 *
 * NOTHING here is called from the audio thread; the track is edited by commands
 * and read by commands.
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
 */

#ifndef LMMS_CHORD_TRACK_H
#define LMMS_CHORD_TRACK_H

#include <vector>

#include <QString>

#include "LmmsTypes.h" // tick_t
#include "TimePos.h"   // DefaultTicksPerBar

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! One chord of the chord track.
 *
 *  \a chord and \a scale are NAMES from the pre-existing vocabulary
 *  (ChordVocabulary::chordNames() / scaleNames()); \a root is a pitch class
 *  (0..11) and \a octave the engine's octave (Note.h: octave 4 holds middle C).
 *  The absolute sounding key of the root is ChordVocabulary::keyOf(root, octave).
 */
struct ChordEvent
{
	tick_t pos = 0;                    //!< where the chord starts, in ticks
	//! How long it sounds; 0 means "until the next event" (or the caller's
	//! own default for the last one).
	tick_t length = 0;
	int root = 0;                      //!< pitch class 0..11 (C = 0)
	int octave = 4;                    //!< the octave the root sounds in
	QString chord = QStringLiteral("Major"); //!< a CHORD entry's name
	//! The key this chord was written in, a SCALE entry's name; empty when no
	//! key is stated (a chord track is not obliged to name one).
	QString scale;
};

/*! The project's chord track (see the header comment for where it lives).
 *
 *  Ordering is the whole contract: events are kept sorted by position and a
 *  set at an existing position replaces that event, so `events()` is what a
 *  reader walks and index i is always the i-th chord of the progression.
 */
class ChordTrack
{
public:
	//! The most events one project may hold. Past this a new position is
	//! refused; replacing an existing position is always allowed.
	static constexpr int MaxEvents = 64;
	//! The shortest event a chord may be. A zero-length chord is not silence
	//! (length 0 already means "hold"), it is a chord that does not sound.
	static constexpr tick_t MinEventTicks = 1;
	//! The bound a generated/held event takes when the caller names none.
	static constexpr tick_t DefaultEventTicks = DefaultTicksPerBar;

	//! True when the track holds no event at all.
	bool empty() const { return m_events.empty(); }
	int size() const { return static_cast<int>(m_events.size()); }

	/*! Whether the project file needs a <chord-track> element. False for an
	 *  empty track, which is what keeps a project that never used a chord
	 *  byte-identical to how it saved before this feature existed. */
	bool shouldPersist() const { return !m_events.empty(); }

	//! Drops every event. Called by Song::clearProject, so a new project never
	//! inherits the previous one's chords.
	void clear() { m_events.clear(); }

	//! Index of the event at \a pos, or -1.
	int indexAt(tick_t pos) const;
	//! The event at \a pos, or nullptr.
	const ChordEvent* findAt(tick_t pos) const;
	//! The event at \a index; the caller has proved the index.
	const ChordEvent& at(int index) const { return m_events[static_cast<std::size_t>(index)]; }
	//! Every event, in ascending position order.
	const std::vector<ChordEvent>& events() const { return m_events; }

	/*! The length an event sounds for, given where it is: its own \a length
	 *  when that is set, otherwise the distance to the next event, otherwise
	 *  \a fallbackTicks. A read of the TRACK's geometry rather than of one
	 *  event, because "0 means hold" can only be resolved with the neighbours. */
	tick_t effectiveLength(int index, tick_t fallbackTicks) const;

	/*! Whether \a event may be stored: a non-negative position, a chord name
	 *  that is a chord entry of the vocabulary, a scale name that is empty or a
	 *  scale entry, and an octave and root inside their ranges. \a error
	 *  receives the reason when it is false. */
	static bool isWritable(const ChordEvent& event, QString* error = nullptr);

	/*! The RANGE half of isWritable(): position, length, root and octave. Split
	 *  out so one function does not carry both the ranges and the name lookups
	 *  (Gate 4's complexity ratchet, the same split the command groups make). */
	static bool rangesAreWritable(const ChordEvent& event, QString* error = nullptr);

	/*! The NAME half of isWritable(): whether \a event's chord (and, when it is
	 *  there, its scale) is a name this engine's vocabulary resolves. Split out
	 *  so one function does not carry both the range checks and the lookup
	 *  (Gate 4's complexity ratchet, the same split the command groups make). */
	static bool nameResolves(const ChordEvent& event, QString* error = nullptr);

	/*! Stores \a event under its POSITION: replaces the event at that position
	 *  in place, or inserts it in ascending order.
	 *
	 *  False - and nothing changes - when \a event is not writable, or when its
	 *  position is new and the track is already at MaxEvents.
	 *  \a replaced receives whether an existing event was overwritten. */
	bool set(const ChordEvent& event, bool* replaced = nullptr);

	//! Removes the event at \a pos. False when there is none.
	bool removeAt(tick_t pos);

	//! Appends the whole track as one <chord-track> element.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;

	/*! Reads a <chord-track> element.
	 *
	 *  The track is CLEARED FIRST, unconditionally. That is the load-bearing
	 *  part: the element is written only when the track is non-empty, so the
	 *  state a restore must be able to reach is "no chord track at all", and an
	 *  absent or empty element is exactly that state. A reader that only added
	 *  would leave a restored project holding the previous project's chords.
	 *
	 *  False when \a element is not a chord track at all; the track is still
	 *  empty in that case, which is the same state an absent element leaves.
	 */
	bool loadSettings(const QDomElement& element);

	/*! The track as a standalone XML document, for the SPEC A16 before-state of
	 *  a recorded action checkpoint and for the command group's read-back. */
	QString toXml() const;
	//! Restores the track from toXml()'s output. False - empty track - when the
	//! text does not parse or is not a chord track.
	bool fromXml(const QString& xml);

private:
	//! Builds the <chord-track> element in \a doc, NOT yet inserted, so the
	//! writer can append it to the song and toXml() can make it a document
	//! root (one builder, two homes - the element cannot diverge).
	QDomElement toElement(QDomDocument& doc) const;

	std::vector<ChordEvent> m_events;
};

} // namespace lmms

#endif // LMMS_CHORD_TRACK_H
