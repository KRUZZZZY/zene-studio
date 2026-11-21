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
 * 0.5.0 E23: ONE read is made on the audio thread - followedKey(), which a track
 * that FOLLOWS the chord track asks for each note it starts (chord follow, the
 * non-destructive twin of note.follow_chords): the note sounds on the nearest
 * tone of the chord (or its scale) the track holds where the note starts, and
 * the clip keeps its own keys. So every edit takes the audio engine's
 * model-change guard, and the tones each event sounds are worked out on the
 * editing thread (m_tones) - the audio read is a scan over integers. The
 * followers (which tracks follow, and in which mode) are part of the track and
 * persist as <follow> children of its element. Everything else here is edited
 * and read by commands, and by the arranger's chord lane.
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

#include <cstdint>
#include <functional>
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

//! 0.5.0 E23: how a track follows the chord track while it plays.
enum class ChordFollowMode
{
	Off,    //!< it plays its notes as written
	Chord,  //!< each note moves to the nearest tone of the chord sounding where it starts
	Scale   //!< ... of that chord's scale (its chord where the event names no scale)
};

//! A track that follows the chord track, by its stable id (Track::id(), the `trk-<n>` number).
struct ChordFollower
{
	int trackId = -1;
	ChordFollowMode mode = ChordFollowMode::Chord;
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
	//! The most tracks that may follow the chord track at once.
	static constexpr int MaxFollowers = 256;

	//! True when the track holds no event at all.
	bool empty() const { return m_events.empty(); }
	int size() const { return static_cast<int>(m_events.size()); }

	/*! Whether the project file needs a <chord-track> element. False for an
	 *  empty track, which is what keeps a project that never used a chord
	 *  byte-identical to how it saved before this feature existed. */
	bool shouldPersist() const { return !m_events.empty() || !m_followers.empty(); }

	//! Drops every event and every follower. Called by Song::clearProject, so a
	//! new project never inherits the previous one's chords.
	void clear();
	//! Drops every event and keeps the followers (chord.clear, a replacing detection).
	void clearEvents();

	//! Index of the event at \a pos, or -1.
	int indexAt(tick_t pos) const;
	//! The event at \a pos, or nullptr.
	const ChordEvent* findAt(tick_t pos) const;
	//! 0.5.0: the event SOUNDING at \a pos - the latest one starting at or before it, when its effectiveLength (with
	//! DefaultEventTicks as the fallback) reaches past it - or nullptr where the track is silent. A chord ends the
	//! harmony of every earlier one, so an earlier, longer event does not sound again after a later one stops.
	const ChordEvent* soundingAt(tick_t pos) const;
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

	// --- 0.5.0 E23: chord follow ---

	//! The tracks that follow, in the order they were set.
	const std::vector<ChordFollower>& followers() const { return m_followers; }
	//! How the track with stable id \a trackId follows (Off when it does not).
	ChordFollowMode followOf(int trackId) const;
	/*! Sets how \a trackId follows; Off drops it. False - nothing changes - when
	 *  the id is negative or a new follower would pass MaxFollowers. */
	bool setFollow(int trackId, ChordFollowMode mode);

	/*! AUDIO THREAD. The key a note of \a key, starting at song tick \a pos,
	 *  sounds on a track with stable id \a trackId: the nearest tone (down on a
	 *  tie) of what the chord track sounds there in the track's mode, or \a key
	 *  itself when the track does not follow or the chord track is silent there.
	 *  Reads integers only: no allocation, no lock, no string. */
	int followedKey(int trackId, int key, tick_t pos) const;

	//! "off", "chord" or "scale": a mode as the project file and the control surface spell it.
	static QString followModeName(ChordFollowMode mode);
	//! The mode @a name spells; false - @a mode untouched - for any other text.
	static bool followModeFromName(const QString& name, ChordFollowMode* mode);

	//! Moves on every change; the arranger's chord lane repaints when it does.
	unsigned revision() const { return m_revision; }

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
	//! Restores the track from toXml()'s output. False and the track UNCHANGED
	//! when the text does not parse or is not a chord track: a restore that
	//! cannot read its snapshot must not destroy the state it failed to read.
	//! (loadSettings still clears first - absence means "no chord track".)
	bool fromXml(const QString& xml);

private:
	//! Builds the <chord-track> element in \a doc, NOT yet inserted, so the
	//! writer can append it to the song and toXml() can make it a document
	//! root (one builder, two homes - the element cannot diverge).
	QDomElement toElement(QDomDocument& doc) const;

	//! The pitch classes an event sounds, one bit each (bit 0 = C): its chord's, and its scale's (0: none).
	struct EventTones
	{
		std::uint16_t chord = 0;
		std::uint16_t scale = 0;
	};

	//! Runs \a change under the model-change guard, then re-derives m_tones and moves the revision.
	void edit(const std::function<void()>& change);
	//! The index of the event sounding at \a pos (soundingAt's rule), or -1.
	int soundingIndex(tick_t pos) const;

	std::vector<ChordEvent> m_events;
	//! Index-aligned with m_events; derived on the editing thread (edit()).
	std::vector<EventTones> m_tones;
	std::vector<ChordFollower> m_followers;
	unsigned m_revision = 0;
};

} // namespace lmms

#endif // LMMS_CHORD_TRACK_H
