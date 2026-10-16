/*
 * MpeExpression.h - per-note MPE (MIDI Polyphonic Expression) expression
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
 *
 */

#ifndef LMMS_MPE_EXPRESSION_H
#define LMMS_MPE_EXPRESSION_H

#include <array>
#include <atomic>
#include <vector>

#include "LmmsTypes.h"

namespace lmms
{

class MidiEvent;

/*! The MPE member-zone timbre controller: the "Y" axis of an MPE controller.
 *  (The MIDI spec calls CC74 "Sound Controller 5"/brightness; MPE fixes its
 *  meaning to per-note timbre. Midi.h has no constant for it.) */
const int MpeTimbreController = 74;

/*! One note's captured MPE expression.
 *
 *  This is deliberately *not* the same thing as a slide note (a per-note
 *  portamento in the piano roll, SPEC-slide-notes) and *not* the channel-wide
 *  pitch bend that MidiEvent::pitchBend() carries: MPE gives every note its own
 *  MIDI channel, so what arrives on that channel belongs to that note.
 *
 *  Values are stored as the controller sent them - no scaling by instrument
 *  pitch range, no mixing with Note::detuning(). */
struct MpeNoteExpression
{
	//! MPE's default member-channel pitch-bend range is +/-48 semitones.
	static constexpr int MaxPitchCents = 48 * 100;

	//! Bend offset from the centre, in 1/100 semitone (+-4800 = +-48 st).
	int pitchCents = 0;
	//! Channel pressure (MPE "Z" axis) at capture, 0..127.
	int pressure = 0;
	//! CC74 (MPE "Y" axis) at capture, 0..127.
	int timbre = 0;

	static int clampPitchCents( int cents );
	static int clamp7Bit( int value );

	float pitchSemitones() const { return pitchCents / 100.f; }
	//! True when the note carries no expression at all (the default).
	bool isNeutral() const;
	bool operator==( const MpeNoteExpression& other ) const;
	bool operator!=( const MpeNoteExpression& other ) const { return !( *this == other ); }
};

/*! One point of a per-note MPE expression stream (Q050-016): the axis value
 *  effective from \a tick (a tick offset from the note's own start) until the
 *  next point. Pitch is a bend offset in 1/100 semitone, pressure and timbre
 *  are 0..127. */
struct MpeExpressionPoint
{
	tick_t tick = 0;
	int value = 0;

	bool operator==( const MpeExpressionPoint& other ) const
	{
		return tick == other.tick && value == other.value;
	}
	bool operator!=( const MpeExpressionPoint& other ) const { return !( *this == other ); }
};

/*! A note's time-varying MPE expression (Q050-016): the three MPE axes as
 *  ordered point streams, instead of the single scalar snapshot
 *  MpeNoteExpression carries.
 *
 *  Points are sorted by tick, and each holds until the next one. An axis with
 *  no points is not in the stream at all - that is the default for every note
 *  saved before this feature, and such a note serializes exactly as it did
 *  before. A note that carries a stream is played through the scheduled path
 *  (NotePlayHandle), which sends pitch/pressure/timbre at each point's own
 *  offset; a note without one keeps the scalar playback path unchanged.
 *
 *  Realtime contract: the playback path only READS a shared snapshot (no
 *  allocation, no lock - copying a note into its handle shares the snapshot
 *  with one lock-free reference count, the same shape as the detuning helper);
 *  writes detach (copy-on-write) on the UI/control thread, so the audio
 *  thread never observes a half-written vector.
 */
class MpeExpressionStream
{
public:
	//! Match notes.expression_set's own cap on the per-curve point count
	//! (ControlCommandsNoteCurves.cpp), so both editors bound the same way.
	static constexpr int MaxPoints = 256;

	bool empty() const { return m_pitch.empty() && m_pressure.empty() && m_timbre.empty(); }
	bool hasPitch() const { return !m_pitch.empty(); }
	bool hasPressure() const { return !m_pressure.empty(); }
	bool hasTimbre() const { return !m_timbre.empty(); }

	const std::vector<MpeExpressionPoint>& pitch() const { return m_pitch; }
	const std::vector<MpeExpressionPoint>& pressure() const { return m_pressure; }
	const std::vector<MpeExpressionPoint>& timbre() const { return m_timbre; }

	//! Replace one axis. Points are clamped to the axis's range and sorted by
	//! tick (a stable sort: two points sharing a tick keep their given order).
	void setPitch( std::vector<MpeExpressionPoint> points );
	void setPressure( std::vector<MpeExpressionPoint> points );
	void setTimbre( std::vector<MpeExpressionPoint> points );
	void clear();

	/*! The value effective at \a tick: the last point at or before it, or
	 *  \a fallback when no point is that early (the scalar snapshot, so a
	 *  stream composes with a capture instead of erasing it). */
	static int valueAt( const std::vector<MpeExpressionPoint>& points, tick_t tick, int fallback );
	int pitchCentsAt( tick_t tick, int fallback ) const { return valueAt( m_pitch, tick, fallback ); }
	int pressureAt( tick_t tick, int fallback ) const { return valueAt( m_pressure, tick, fallback ); }
	int timbreAt( tick_t tick, int fallback ) const { return valueAt( m_timbre, tick, fallback ); }

private:
	std::vector<MpeExpressionPoint> m_pitch;
	std::vector<MpeExpressionPoint> m_pressure;
	std::vector<MpeExpressionPoint> m_timbre;
};

/*! Per-channel MPE state for one MIDI input stream (task #601).
 *
 *  MPE works by giving each note its own MIDI channel: a master channel
 *  carries the normal channel-wide messages, and the member channels carry one
 *  note each with its *own* pitch bend, channel pressure and CC74. This class
 *  tracks exactly that - which note is sounding on which member channel, and
 *  that channel's current expression - so a caller can stamp the right
 *  expression onto the right note.
 *
 *  Realtime contract: every array is fixed-size and sized in the constructor,
 *  and noteOn()/noteOff()/handleExpressionEvent() allocate nothing and take no
 *  lock. The caller stores the result on its note handles with plain
 *  assignments.
 *
 *  Scope note: the model is one master channel excluded, every other channel
 *  a member - which covers MPE's lower zone (master 1, members 2-16) and its
 *  upper zone (master 16, members 1-15). Only pitch bend / channel pressure /
 *  CC74 are tracked. No on-screen UI reads this yet (docs/MPE.md).
 */
class MpeExpression
{
public:
	static constexpr int ChannelCount = 16;
	//! MPE's lower zone: channel index 0 is the master, 1..15 the members.
	static constexpr int DefaultMasterChannel = 0;
	static constexpr int DefaultBendRangeSemitones = MpeNoteExpression::MaxPitchCents / 100;
	//! Fixed cap on the notes tracked per member channel. Standard MPE is one
	//! note per member channel; the cap only bounds the "MPE+" case where a
	//! controller plays a second note on a channel that is still held.
	static constexpr int MaxActiveNotesPerChannel = 4;

	MpeExpression();

	/*! Process-wide MPE input mode. Deliberately NOT serialized: with it off
	 *  the MIDI input path is exactly what it was before this feature existed,
	 *  so a project never changes meaning because of it. The per-port/project
	 *  toggle and its UI are not implemented (docs/MPE.md). */
	static bool isEnabled();
	static void setEnabled( bool enabled );

	int masterChannel() const { return m_masterChannel; }
	//! \a channel is a 0-based MIDI channel (MidiEvent::channel()).
	void setMasterChannel( int channel );
	bool isMemberChannel( int channel ) const;

	int bendRangeSemitones() const { return m_bendRangeSemitones; }
	void setBendRangeSemitones( int semitones );

	// ---- input side (MIDI thread; no allocation, no locks) ----
	void noteOn( int channel, int key );
	void noteOff( int channel, int key );
	/*! Feed a captured input event.
	 *  \return true when the event was per-note expression on a member channel
	 *  (bend / channel pressure / CC74) and the caller must therefore *not*
	 *  treat it as a channel-wide message. The per-channel state is updated
	 *  either way. Note on/off are not consumption events: they return false
	 *  and are tracked for their channel's sake. */
	bool handleExpressionEvent( const MidiEvent& event );
	void reset();

	// ---- readback ----
	MpeNoteExpression current( int channel ) const;
	int activeNoteCount( int channel ) const;
	int activeNote( int channel, int index ) const;

	//! Raw 14-bit bend (0..16383, 8192 = centre) as a note's pitch offset.
	static int pitchCentsForBend( int bend14, int bendRangeSemitones );
	//! The inverse of pitchCentsForBend(): the 14-bit bend that carries
	//! \a cents for a member channel with \a bendRangeSemitones. Q050-016 uses
	//! it to turn a streamed pitch point into the MIDI bend event at the
	//! point's offset, so a hosted instrument hears the same bend the built-in
	//! frequency path applies.
	static int bendForPitchCents( int cents, int bendRangeSemitones );

private:
	struct ChannelState
	{
		int bend14 = 8192;	//!< centre, i.e. no bend
		int pressure = 0;
		int timbre = 0;
		std::array<int, MaxActiveNotesPerChannel> keys{};
		int keyCount = 0;
	};

	static bool validChannel( int channel );
	ChannelState& channelState( int channel ) { return m_channels[channel]; }
	const ChannelState& channelState( int channel ) const { return m_channels[channel]; }
	void addActiveNote( ChannelState& state, int key );
	void removeActiveNote( ChannelState& state, int key );

	std::array<ChannelState, ChannelCount> m_channels{};
	int m_masterChannel = DefaultMasterChannel;
	int m_bendRangeSemitones = DefaultBendRangeSemitones;

	static std::atomic_bool s_enabled;
};

} // namespace lmms

#endif // LMMS_MPE_EXPRESSION_H
