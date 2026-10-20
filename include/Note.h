/*
 * Note.h - declaration of class note which contains all information about a
 *          note + definitions of several constants and enums
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_NOTE_H
#define LMMS_NOTE_H

#include <memory>
#include <optional>
#include <vector>

#include "volume.h"
#include "panning.h"
#include "MpeExpression.h"
#include "MpeStreamSnapshot.h"
#include "SerializingObject.h"
#include "NoteConditions.h"
#include "TimePos.h"


namespace lmms
{


class DetuningHelper;
class AutomationClip;


enum class Key : int
{
	C = 0,
	Cis = 1, Des = 1,
	D = 2,
	Dis = 3, Es = 3,
	E = 4, Fes = 4,
	F = 5,
	Fis = 6, Ges = 6,
	G = 7,
	Gis = 8, As = 8,
	A = 9,
	Ais = 10, B = 10,
	H = 11
} ;


enum class Octave : int
{
	Octave_m1,	// MIDI standard starts at C-1
	Octave_0,
	Octave_1,
	Octave_2,
	Octave_3,
	Octave_4,
	Octave_5,
	Octave_6,
	Octave_7,
	Octave_8,
	Octave_9,	// incomplete octave, MIDI only goes up to G9
};

const int FirstOctave = -1;
const int KeysPerOctave = 12;

constexpr inline auto operator+(Octave octave, Key key) -> int
{
	return static_cast<int>(octave) * KeysPerOctave + static_cast<int>(key);
}

constexpr auto DefaultOctave = Octave::Octave_4;
const int DefaultKey = DefaultOctave + Key::A;
//! Number of physical keys, limited to MIDI range (valid for both MIDI 1.0 and 2.0)
const int NumKeys = 128;

const int DefaultMiddleKey = Octave::Octave_4 + Key::C;
const int DefaultBaseKey = Octave::Octave_4 + Key::A;
const float DefaultBaseFreq = 440.f;

const float MaxDetuning = 5 * 12.0f;



class LMMS_EXPORT Note : public SerializingObject
{
public:
	Note( const TimePos & length = TimePos( 0 ),
		const TimePos & pos = TimePos( 0 ),
		int key = DefaultKey,
		volume_t volume = DefaultVolume,
		panning_t panning = DefaultPanning,
		std::shared_ptr<DetuningHelper> detuning = nullptr);
	Note( const Note & note );
	~Note() override;

	Note& operator=(const Note& note);

	//! Performs a deep copy and returns an owning raw pointer
	Note* clone() const;

	/*! The note's stable id, the number in "note-<n>" (SPEC-stable-ids.md
	 *  slice 2).
	 *
	 *  Assigned ONCE, in the constructor, and never changed while the note is
	 *  alive: it names the object, not its position in its clip's note list -
	 *  which is the list rearrangeAllNotes() re-sorts after every edit, so an
	 *  index is the least stable address in the document. The only other writer
	 *  is loadSettings(), which takes the value the project file carries so a
	 *  re-save keeps it; a note element with no id leaves the constructor's
	 *  value in place, which is what makes legacy assignment deterministic
	 *  (MidiClip::loadSettings recreates the notes in document order).
	 *
	 *  A COPY is a NEW note and allocates a FRESH id: Note::clone() is what the
	 *  clipboard, the piano roll's duplicate and MidiClip's copy constructor
	 *  use, and two notes wearing one id would make the id an ambiguous address.
	 */
	int id() const { return m_id; }

	//! Take \a id from a project file. Raises the project counter above it, so
	//! the number can never be handed to a new object (see ProjectIds).
	void setId(int id);

	// Note types
	enum class Type
	{
		Regular = 0,
		Step
	};

	Type type() const { return m_type; }
	inline void setType(Type t) { m_type = t; }

	/*! Whether this is a slide (portamento) note: its pitch glides from the
	 *  nearest preceding note in the same clip to its own key over its whole
	 *  length (FL-style slide note, SPEC-slide-notes D-1). Serialized as the
	 *  optional "slide" attribute - notes without it (i.e. every project
	 *  saved before slide notes existed) load as regular notes. */
	bool slide() const { return m_slide; }
	void setSlide( bool slide ) { m_slide = slide; }

	/*! The chance, in [0, 1], that this note is played at all in a take.
	 *  1 - the default, and what every project saved before note probability
	 *  existed loads as - means "always". Serialized as the optional "prob"
	 *  attribute, written only when it is not 1. */
	float probability() const { return m_probability; }
	void setProbability( float probability );

	/*! Multiplicative velocity jitter, in [0, 1]. 0 - the default - leaves
	 *  the note's velocity exactly alone; a value j multiplies it by a factor
	 *  in [1-j, 1+j] drawn from the seeded roll. Serialized as the optional
	 *  "veljit" attribute, written only when it is not 0. */
	float velocityJitter() const { return m_velocityJitter; }
	void setVelocityJitter( float jitter );

	/*! 0.5.0 L07. A muted note stays in the clip and is drawn, but is not played
	 *  ("mute" attribute, written only when set). */
	bool muted() const { return m_muted; }
	void setMuted( bool muted ) { m_muted = muted; }

	/*! The note-off velocity, 0..200 on the note volume's scale, or -1 - the
	 *  default - for none: the note-off then carries velocity 0, exactly as
	 *  before this existed ("relvel" attribute, written only when set). */
	/*! 0.5.0 L07 (W3): repeats, recurrence, articulation, group and lyric (NoteConditions.h),
	 *  written only when set ("rep", "recl"/"recm", "art", "grp", "lyr"). */
	const NoteConditions& conditions() const { return m_conditions; }
	void setConditions(const NoteConditions& conditions) { m_conditions = conditions; }
	bool recursOnPass(int pass) const { return m_conditions.recursOnPass(pass); }

	int releaseVelocity() const { return m_releaseVelocity; }
	void setReleaseVelocity( int velocity );
	int midiReleaseVelocity( int midiBaseVelocity ) const;
	/*! 0.5.0 Q050-039: the velocity an incoming MIDI note-off carries, as this
	 *  note's volume scale (0..200, 100 = the base velocity) stores it. The
	 *  exact inverse of midiReleaseVelocity() at the same base velocity, so
	 *  the velocity that arrived is the velocity playback emits: both convert
	 *  with rounding, because at the default base velocity (63) one velocity
	 *  step is 1.587 volume units and truncation handed back 95 for an
	 *  incoming 96. The scale names velocities up to twice the base velocity,
	 *  so MIDI velocity 127 saturates to it at base velocities below 64. */
	static int releaseVelocityFromMidi( int midiVelocity, int midiBaseVelocity );

	/*! The timing the note had before its first quantise, kept until it is
	 *  restored or frozen (note.restore_timing) - Cubase's "undo quantize" that
	 *  survives other edits and a save ("qpos"/"qlen", written only when kept). */
	bool hasUnquantisedTiming() const { return m_unquantisedPos >= 0; }
	//! Keep the current timing as the unquantised one, unless one is kept already.
	void rememberTiming();
	//! Put the kept timing back and forget it; false when none was kept.
	bool restoreTiming();
	void forgetTiming() { m_unquantisedPos = m_unquantisedLength = -1; }

	/*! MPE per-note expression (task #601): the pitch bend / channel pressure /
	 *  CC74 an MPE controller sent on *this note's own* MIDI channel.
	 *
	 *  Not the slide note above (that is a portamento the piano roll draws and
	 *  the note glides along); not Note::detuning() (a per-note pitch
	 *  automation curve); and not the channel-wide bend in
	 *  MidiEvent::pitchBend(), which bends every note of the channel. Storing
	 *  it here is what makes it per-note.
	 *
	 *  Serialized as the optional "mpepitch"/"mpepressure"/"mpetimbre"
	 *  attributes, written only when expression was captured: notes (and whole
	 *  projects) saved with no expression serialize byte-identically to before,
	 *  and a build without this feature simply ignores the attributes. */
	bool hasMpeExpression() const { return m_mpeCaptured; }
	MpeNoteExpression mpeExpression() const { return { m_mpePitchCents, m_mpePressure, m_mpeTimbre }; }
	//! Bend offset in 1/100 semitone, clamped to +-MpeNoteExpression::MaxPitchCents
	int mpePitchCents() const { return m_mpePitchCents; }
	//! Channel pressure 0..127
	int mpePressure() const { return m_mpePressure; }
	//! CC74 (timbre) 0..127
	int mpeTimbre() const { return m_mpeTimbre; }

	//! The piano roll's (future) editing entry point: sets all three axes and
	//! marks the note as carrying expression. Individual setters only touch
	//! their own axis, and any of them marks the note as carrying expression.
	void setMpeExpression( const MpeNoteExpression& expression );
	void setMpePitchCents( int cents );
	void setMpePressure( int pressure );
	void setMpeTimbre( int timbre );
	//! Drops the expression entirely (and the serialized attributes with it).
	void clearMpeExpression();

	/*! Q050-016: the note's time-varying MPE expression stream (pitch,
	 *  pressure, timbre points at tick offsets from the note's start), the
	 *  per-note curves the scalar expression above is a single sample of.
	 *
	 *  A note that carries no stream - every note saved before this feature,
	 *  and a note whose stream was cleared - has `hasMpeStream() == false` and
	 *  serializes exactly as before: the stream is three optional attributes,
	 *  written only when non-empty. Playback reads the stream to schedule the
	 *  axes at their own offsets; a note without one keeps the scalar path.
	 *
	 *  Round 3 (realtime): the stream is an immutable MpeStreamSnapshot behind
	 *  an atomic pointer (include/MpeStreamSnapshot.h has the protocol and its
	 *  memory-ordering argument). An edit never changes a snapshot in place: it
	 *  builds a new one and publishes it, so a copy taken concurrently - the
	 *  audio thread's NotePlayHandle - holds either the old snapshot or the new
	 *  one, never a half-written vector, and the old one is never freed on the
	 *  realtime path. mpeStream() is valid until this note publishes again,
	 *  i.e. for the thread that edits the note, and for a handle's own copy. */
	bool hasMpeStream() const
	{
		const MpeStreamSnapshot* snapshot = m_mpeStream.load( std::memory_order_acquire );
		return snapshot != nullptr && !snapshot->stream().empty();
	}
	const MpeExpressionStream& mpeStream() const { return m_mpeStream.load( std::memory_order_acquire )->stream(); }
	//! Publish \a stream as a new snapshot (an empty one clears). Allocates:
	//! editing threads only.
	void setMpeStream( const MpeExpressionStream& stream );
	//! Drops the whole stream (and its serialized attributes with it).
	void clearMpeStream();

	//! Types of per-note automation. Currently only detuning/pitch bending is supported.
	enum class ParameterType
	{
		Detuning = 0
	};
	AutomationClip* parameterCurve(ParameterType paramType);

	// used by GUI
	inline void setSelected( const bool selected ) { m_selected = selected; }
	inline void setOldKey( const int oldKey ) { m_oldKey = oldKey; }
	inline void setOldPos( const TimePos & oldPos ) { m_oldPos = oldPos; }

	inline void setOldLength( const TimePos & oldLength )
	{
		m_oldLength = oldLength;
	}
	inline void setIsPlaying( const bool isPlaying )
	{
		m_isPlaying = isPlaying;
	}


	void setLength( const TimePos & length );
	void setPos( const TimePos & pos );
	void setKey( const int key );
	virtual void setVolume( volume_t volume );
	virtual void setPanning( panning_t panning );
	void quantizeLength( const int qGrid );
	void quantizePos( const int qGrid );

	static inline bool lessThan( const Note * lhs, const Note * rhs )
	{
		// function to compare two notes - must be called explicitly when
		// using qSort
		if( (int)( *lhs ).pos() < (int)( *rhs ).pos() )
		{
			return true;
		}
		else if( (int)( *lhs ).pos() > (int)( *rhs ).pos() )
		{
			return false;
		}
		return ( (int)( *lhs ).key() > (int)( *rhs ).key() );
	}

	inline bool selected() const
	{
		return m_selected;
	}

	inline int oldKey() const
	{
		return m_oldKey;
	}

	inline TimePos oldPos() const
	{
		return m_oldPos;
	}

	inline TimePos oldLength() const
	{
		return m_oldLength;
	}

	inline bool isPlaying() const
	{
		return m_isPlaying;
	}

	inline TimePos endPos() const
	{
		const int l = length();
		return pos() + l;
	}

	inline const TimePos & length() const
	{
		return m_length;
	}

	inline const TimePos & pos() const
	{
		return m_pos;
	}

	inline TimePos pos( TimePos basePos ) const
	{
		const int bp = basePos;
		return m_pos - bp;
	}

	inline int key() const
	{
		return m_key;
	}

	inline volume_t getVolume() const
	{
		return m_volume;
	}

	int midiVelocity( int midiBaseVelocity ) const
	{
		return std::min(MidiMaxVelocity, getVolume() * midiBaseVelocity / DefaultVolume);
	}

	inline panning_t getPanning() const
	{
		return m_panning;
	}

	static QString classNodeName()
	{
		return "note";
	}

	inline QString nodeName() const override
	{
		return classNodeName();
	}

	static TimePos quantized( const TimePos & m, const int qGrid );

	const std::shared_ptr<DetuningHelper>& detuning() const { return m_detuning; }

	bool hasDetuningInfo() const;
	bool withinRange(int tickStart, int tickEnd) const;

	void createDetuning();
	//! 0.5.0 L07: drop this note's detuning curve (a copy of the note may share it), so a new one starts empty.
	void clearDetuning() { m_detuning.reset(); }


protected:
	/*! Q050-016 round 3: hand this note's stream reference to the deferred
	 *  reclaimer instead of freeing it here. NotePlayHandle's destructor - the
	 *  realtime teardown path - calls it, so the final release of a stream on
	 *  the audio thread never frees (MpeStreamSnapshot::reclaimRetired does,
	 *  later, on a control thread). */
	void retireMpeStreamDeferred() noexcept;

	void saveSettings( QDomDocument & doc, QDomElement & parent ) override;
	void loadSettings( const QDomElement & _this ) override;


private:
	/*! The stable id, the number in "note-<n>" (SPEC-stable-ids.md slice 2).
	 *  Declared FIRST (and initialised first) so the member-init lists below
	 *  follow declaration order - the release configuration builds with
	 *  -Werror=reorder. */
	int m_id;

	// for piano roll editing
	bool m_selected;
	int m_oldKey;
	TimePos m_oldPos;
	TimePos m_oldLength;
	bool m_isPlaying;

	int m_key;
	volume_t m_volume;
	panning_t m_panning;
	TimePos m_length;
	TimePos m_pos;
	std::shared_ptr<DetuningHelper> m_detuning;

	Type m_type = Type::Regular;
	bool m_slide = false;

	// MIDI depth. Both default to the behaviour of every project saved before
	// these existed: always play, velocity untouched.
	float m_probability = 1.f;
	float m_velocityJitter = 0.f;

	// MPE per-note expression (task #601). m_mpeCaptured distinguishes "no
	// expression" (nothing is written to the project file) from a captured
	// expression that happens to be all zeros.
	bool m_mpeCaptured = false;
	int m_mpePitchCents = 0;
	int m_mpePressure = 0;
	int m_mpeTimbre = 0;

	// Q050-016: the optional per-note expression stream, an immutable
	// snapshot behind an atomic pointer (round 3; MpeStreamSnapshot.h). Copies
	// take a counted reference (lock-free, allocation-free - the audio thread
	// builds its NotePlayHandle that way); setMpeStream()/clearMpeStream()/
	// operator= swap the pointer with one atomic exchange and release the old
	// snapshot, which is freed only off the realtime path.
	std::atomic<const MpeStreamSnapshot*> m_mpeStream{ nullptr };
	//! Swap m_mpeStream to \a next (taking over its reference) and release
	//! the previous snapshot.
	void publishMpeStream( const MpeStreamSnapshot* next ) noexcept;

	// 0.5.0 L07: mute, release velocity, the pre-quantise timing (-1 = none).
	bool m_muted = false;
	int m_releaseVelocity = -1;
	tick_t m_unquantisedPos = -1;
	tick_t m_unquantisedLength = -1;
	NoteConditions m_conditions;
};

using NoteVector = std::vector<Note*>;

struct NoteBounds
{
	TimePos start;
	TimePos end;
	int lowest;
	int highest;
};


std::optional<NoteBounds> boundsForNotes(const NoteVector& notes);


} // namespace lmms

#endif // LMMS_NOTE_H
