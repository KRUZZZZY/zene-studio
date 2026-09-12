/*
 * MpeExpressionTest.cpp - MPE per-channel capture and pitch math (task #601)
 *
 * Copyright (c) 2026 Zene Studio developers
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

//! MPE (task #601) is per-note expression: the controller gives every note its
//! own MIDI channel, and the bend / channel pressure / CC74 arriving on that
//! channel belongs to that note (docs/MPE.md). This half covers the per-channel state machine and the
//! pitch math, with no Engine: whether the expression reaches the *audio* is
//! proven by the headless render pair in docs/MPE.md.

#include <QtTest>

#include <cmath>

#include "Midi.h"
#include "MidiEvent.h"
#include "MpeExpression.h"
#include "Note.h"
#include "NotePlayHandle.h"

namespace
{

float frequencyForKey( int key, int mpePitchCents )
{
	using namespace lmms;
	const float base = DefaultBaseFreq
		* std::exp2( ( key - DefaultKey ) / 12.f );
	return base * NotePlayHandle::mpePitchRatio( mpePitchCents );
}


} // namespace


class MpeExpressionTest : public QObject
{
	Q_OBJECT

private slots:
	//! (a) The 14-bit bend -> cents conversion, exactly (integer math, no
	//! floating point in the stored value).
	void bendConvertsToCentsExactly()
	{
		using namespace lmms;

		// 8192 is the centre: no bend, no offset.
		QCOMPARE( MpeExpression::pitchCentsForBend( 8192, 48 ), 0 );
		// A quarter of the range up/down at the MPE default +-48 semitones.
		QCOMPARE( MpeExpression::pitchCentsForBend( 12288, 48 ), 2400 );
		QCOMPARE( MpeExpression::pitchCentsForBend( 4096, 48 ), -2400 );
		// The ends of the 14-bit range: asymmetric by one cent, as the raw
		// range itself is (8192 steps down, 8191 up).
		QCOMPARE( MpeExpression::pitchCentsForBend( 0, 48 ), -4800 );
		QCOMPARE( MpeExpression::pitchCentsForBend( MidiMaxPitchBend, 48 ), 4799 );
		// A half-range bend range halves the offset.
		QCOMPARE( MpeExpression::pitchCentsForBend( 12288, 24 ), 1200 );
		// Clamped on both axes, and a nonsense bend range degrades to one
		// semitone instead of dividing by zero.
		QCOMPARE( MpeExpression::pitchCentsForBend( MidiMaxPitchBend, 0 ), 99 );
	}

	//! (b) Capture: the acceptance test. Two notes on two member channels, bent
	//! and squeezed independently, must come back as two different per-note
	//! expressions - and a note that is not on a member channel must get none.
	void captureIsPerChannel()
	{
		using namespace lmms;

		MpeExpression mpe;	// master = channel 1 (index 0), members 2..16
		QVERIFY( !mpe.isMemberChannel( 0 ) );
		QVERIFY( mpe.isMemberChannel( 1 ) );
		QVERIFY( mpe.isMemberChannel( 15 ) );
		QVERIFY( !mpe.isMemberChannel( -1 ) );
		QVERIFY( !mpe.isMemberChannel( 16 ) );

		// An MPE controller plays two notes, one per member channel.
		mpe.noteOn( 1, 60 );
		mpe.noteOn( 2, 64 );
		QCOMPARE( mpe.activeNoteCount( 1 ), 1 );
		QCOMPARE( mpe.activeNote( 1, 0 ), 60 );
		QCOMPARE( mpe.activeNoteCount( 2 ), 1 );
		QCOMPARE( mpe.activeNote( 2, 0 ), 64 );

		// Each channel is performed independently: this is the whole feature.
		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiPitchBend, 1, 12288 ) ) );
		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiChannelPressure, 1, 100 ) ) );
		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiControlChange, 1, MpeTimbreController, 30 ) ) );

		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiPitchBend, 2, 4096 ) ) );
		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiChannelPressure, 2, 20 ) ) );
		QVERIFY( mpe.handleExpressionEvent( MidiEvent( MidiControlChange, 2, MpeTimbreController, 70 ) ) );

		const MpeNoteExpression first = mpe.current( 1 );
		const MpeNoteExpression second = mpe.current( 2 );
		QCOMPARE( first.pitchCents, 2400 );
		QCOMPARE( first.pressure, 100 );
		QCOMPARE( first.timbre, 30 );
		QCOMPARE( second.pitchCents, -2400 );
		QCOMPARE( second.pressure, 20 );
		QCOMPARE( second.timbre, 70 );
		// A test that could not tell the channels apart would pass with these
		// equal; they are not.
		QVERIFY( first != second );

		// The notes themselves: each is stamped with its own channel's state,
		// so note 60 moves up and note 64 moves down by the same amount.
		Note note60( TimePos( 384 ), TimePos( 0 ), 60 );
		Note note64( TimePos( 384 ), TimePos( 0 ), 64 );
		QVERIFY( !note60.hasMpeExpression() );
		note60.setMpeExpression( mpe.current( 1 ) );
		note64.setMpeExpression( mpe.current( 2 ) );
		QVERIFY( note60.hasMpeExpression() );
		QCOMPARE( note60.mpePitchCents(), 2400 );
		QCOMPARE( note60.mpePressure(), 100 );
		QCOMPARE( note60.mpeTimbre(), 30 );
		QCOMPARE( note64.mpePitchCents(), -2400 );
		QCOMPARE( note60.key() + note60.mpeExpression().pitchSemitones(), 84.f );
		QCOMPARE( note64.key() + note64.mpeExpression().pitchSemitones(), 40.f );

		// A note on the master channel gets nothing: its channel is not a
		// member, so its bend is still the channel-wide one.
		mpe.noteOn( 0, 67 );
		QCOMPARE( mpe.activeNoteCount( 0 ), 0 );
		QVERIFY( !mpe.handleExpressionEvent(
			MidiEvent( MidiPitchBend, static_cast<int8_t>( 0 ), 12288 ) ) );
		QCOMPARE( mpe.current( 0 ).pitchCents, 0 );

		// Only CC74 is timbre. Sustain (and anything else) must stay a normal
		// channel message, or the pedal would stop working.
		QVERIFY( !mpe.handleExpressionEvent( MidiEvent( MidiControlChange, 1, MidiControllerSustain, 127 ) ) );
		QVERIFY( !mpe.handleExpressionEvent( MidiEvent( MidiControlChange, 1, 1, 64 ) ) );
		QCOMPARE( mpe.current( 1 ).timbre, 30 );

		// Note-off drops the channel binding but not the values already
		// captured on the note handle.
		mpe.noteOff( 1, 60 );
		QCOMPARE( mpe.activeNoteCount( 1 ), 0 );
		QCOMPARE( note60.mpePitchCents(), 2400 );
		QCOMPARE( mpe.current( 1 ).pitchCents, 2400 );
	}

	//! (c) The per-channel note list is bounded and releases on note-off.
	void activeNotesAreBounded()
	{
		using namespace lmms;

		MpeExpression mpe;
		for( int i = 0; i < MpeExpression::MaxActiveNotesPerChannel + 2; ++i )
		{
			mpe.noteOn( 5, 60 + i );
		}
		QCOMPARE( mpe.activeNoteCount( 5 ), MpeExpression::MaxActiveNotesPerChannel );
		// Re-pressing a key already held does not add a second entry.
		mpe.noteOn( 5, 60 );
		QCOMPARE( mpe.activeNoteCount( 5 ), MpeExpression::MaxActiveNotesPerChannel );

		mpe.noteOff( 5, 60 );
		QCOMPARE( mpe.activeNoteCount( 5 ), MpeExpression::MaxActiveNotesPerChannel - 1 );
		QCOMPARE( mpe.activeNote( 5, 0 ), 61 );
		// A key that is not held is not there to remove.
		mpe.noteOff( 5, 99 );
		QCOMPARE( mpe.activeNoteCount( 5 ), MpeExpression::MaxActiveNotesPerChannel - 1 );
		// Out-of-range readback is -1, never a stray key.
		QCOMPARE( mpe.activeNote( 5, MpeExpression::MaxActiveNotesPerChannel ), -1 );
		QCOMPARE( mpe.activeNoteCount( 0 ), 0 );	// master channel

		mpe.reset();
		QCOMPARE( mpe.activeNoteCount( 5 ), 0 );
		QCOMPARE( mpe.current( 5 ).pitchCents, 0 );
	}

	//! (g) The playback path applies the expression as a frequency ratio, and
	//! applies nothing when there is no expression (the byte-identical case).
	void playbackAppliesThePitch()
	{
		using namespace lmms;

		QCOMPARE( NotePlayHandle::mpePitchRatio( 0 ), 1.f );
		// A semitone is a semitone whatever the scale: 2^(cents/1200).
		QCOMPARE( NotePlayHandle::mpePitchRatio( 1200 ), 2.f );
		QCOMPARE( NotePlayHandle::mpePitchRatio( -1200 ), 0.5f );
		QVERIFY( std::fabs( NotePlayHandle::mpePitchRatio( 100 ) - 1.0594631f ) < 1e-6f );

		// A4 (key 69) with +2400 cents is two octaves up...
		QVERIFY( std::fabs( frequencyForKey( DefaultKey, 2400 ) - 1760.f ) < 0.01f );
		// ... and with no expression it is exactly the pre-MPE frequency.
		QCOMPARE( frequencyForKey( DefaultKey, 0 ), DefaultBaseFreq );
		QCOMPARE( frequencyForKey( 60, 0 ),
			static_cast<float>( DefaultBaseFreq * std::exp2( ( 60 - DefaultKey ) / 12.f ) ) );
	}

};

QTEST_GUILESS_MAIN(MpeExpressionTest)
#include "MpeExpressionTest.moc"
