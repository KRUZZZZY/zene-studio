/*
 * MpeInputPathTest.cpp - MPE capture through the real MIDI input path (task #601)
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

//! MPE (task #601) is per-note expression: the controller gives every note its
//! own MIDI channel, and the bend / channel pressure / CC74 arriving on that
//! channel belongs to that note (docs/MPE.md). This half drives the *real* input path
//! (InstrumentTrack::processInEvent) end to end, which needs a live Engine.

#include <QtTest>

#include <QDomDocument>

#include <algorithm>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Midi.h"
#include "MidiEvent.h"
#include "MpeExpression.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"

class MpeInputPathTest : public QObject
{
	Q_OBJECT

private:
	bool m_engineUp = false;

private slots:
	void initTestCase()
	{
		using namespace lmms;
		Engine::init( true );
		Engine::audioEngine()->audioDev()->stopProcessing();
		BufferManager::init( Engine::audioEngine()->framesPerPeriod() );
		NotePlayHandleManager::init();
		m_engineUp = true;
		// MPE input is off by default; every slot that needs it turns it on
		// and turns it back off.
		QVERIFY( !MpeExpression::isEnabled() );
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		MpeExpression::setEnabled( false );
		if ( m_engineUp )
		{
			Engine::getSong()->clearProject();
			Engine::destroy();
			m_engineUp = false;
		}
	}

	//! (h) The real input path: an MPE stream through
	//! InstrumentTrack::processInEvent lands on the right note handles and not
	//! on the others.
	void inputPathStampsTheRightNotes()
	{
		using namespace lmms;

		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create( Track::Type::Instrument, song ) );
		QVERIFY( track != nullptr );

		// MPE input mode is off by default; the last third of this slot turns it
		// back off and re-checks that nothing is captured then.
		MpeExpression::setEnabled( true );

		// Note-on on member channel 2 (index 1), before any bend.
		track->processInEvent( MidiEvent( MidiNoteOn, 1, 60, 100 ) );
		NotePlayHandle* note60 = track->playingNote( 60 );
		QVERIFY( note60 != nullptr );
		QVERIFY( note60->hasMpeExpression() );
		QCOMPARE( note60->mpePitchCents(), 0 );

		// The note's own channel bends: only that note moves, and the
		// instrument's channel-wide pitch stays put (otherwise every note on
		// the track would bend).
		track->processInEvent( MidiEvent( MidiPitchBend, 1, 12288 ) );
		QCOMPARE( note60->mpePitchCents(), 2400 );
		QCOMPARE( note60->mpeExpression().pitchSemitones(), 24.f );
		QCOMPARE( track->pitchModel()->value(), 0.f );
		QCOMPARE( track->mpeExpression().current( 1 ).pitchCents, 2400 );

		track->processInEvent( MidiEvent( MidiChannelPressure, 1, 90 ) );
		QCOMPARE( note60->mpePressure(), 90 );
		track->processInEvent( MidiEvent( MidiControlChange, 1, MpeTimbreController, 44 ) );
		QCOMPARE( note60->mpeTimbre(), 44 );

		// A second note on a second member channel is untouched by the first.
		track->processInEvent( MidiEvent( MidiNoteOn, 2, 64, 100 ) );
		NotePlayHandle* note64 = track->playingNote( 64 );
		QVERIFY( note64 != nullptr );
		QCOMPARE( note64->mpePitchCents(), 0 );
		QCOMPARE( note64->mpePressure(), 0 );
		QCOMPARE( note64->mpeTimbre(), 0 );
		track->processInEvent( MidiEvent( MidiPitchBend, 2, 4096 ) );
		QCOMPARE( note64->mpePitchCents(), -2400 );
		QCOMPARE( note60->mpePitchCents(), 2400 );

		// A master-channel bend is still channel-wide (the pre-MPE path), and
		// it does not disturb the captured per-note expression.
		track->processInEvent( MidiEvent( MidiPitchBend, static_cast<int8_t>( 0 ), 12288 ) );
		QVERIFY( track->pitchModel()->value() != 0.f );
		QCOMPARE( note60->mpePitchCents(), 2400 );
		QCOMPARE( note64->mpePitchCents(), -2400 );

		// Release drops the channel binding, keeping the captured values.
		track->processInEvent( MidiEvent( MidiNoteOff, 1, 60 ) );
		QCOMPARE( track->mpeExpression().activeNoteCount( 1 ), 0 );
		QCOMPARE( note60->mpePitchCents(), 2400 );

		// With MPE off (the default, and every project that never enabled it)
		// the same stream captures nothing at all.
		MpeExpression::setEnabled( false );
		track->processInEvent( MidiEvent( MidiNoteOn, 3, 67, 100 ) );
		NotePlayHandle* note67 = track->playingNote( 67 );
		QVERIFY( note67 != nullptr );
		QVERIFY( !note67->hasMpeExpression() );
		QCOMPARE( note67->mpePitchCents(), 0 );
		QCOMPARE( track->mpeExpression().activeNoteCount( 3 ), 0 );
		// ... and the old channel-wide bend behaviour is back.
		track->processInEvent( MidiEvent( MidiPitchBend, 3, 16383 ) );
		QVERIFY( track->pitchModel()->value() > 0.f );
		QVERIFY( !note67->hasMpeExpression() );

		song->clearProject();
	}

	//! (j) Q050-039: an MPE note-off carries release velocity. The real input
	//! path stores it on the released note, the note keeps it through
	//! save -> load, and a zero-velocity or non-MPE note-off captures nothing.
	void noteOffVelocityIsCapturedOnTheReleasedNote()
	{
		using namespace lmms;

		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create( Track::Type::Instrument, song ) );
		QVERIFY( track != nullptr );

		// Set the port's base velocity to the note-volume default (100) so the
		// note's volume scale (0..200, 100 = default) and the MIDI velocity
		// scale agree: the captured value is the velocity itself.
		track->midiPort()->setBaseVelocity( DefaultVolume );

		MpeExpression::setEnabled( true );

		constexpr int kMemberChannel = 3;
		constexpr int kVelocity = 96;   // non-default (0 means "no release velocity")

		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 60, 100 ) );
		NotePlayHandle* note = track->playingNote( 60 );
		QVERIFY( note != nullptr );
		// Nothing captured before the note-off: the note has no release velocity.
		QCOMPARE( note->releaseVelocity(), -1 );

		track->processInEvent( MidiEvent( MidiNoteOff, kMemberChannel, 60, kVelocity ) );

		// (criterion 1) The released note carries the velocity the note-off sent.
		QCOMPARE( note->releaseVelocity(), kVelocity );
		QCOMPARE( note->midiReleaseVelocity( track->midiPort()->baseVelocity() ), kVelocity );

		// ... and that is what a SAVED note reads back: save -> load keeps it.
		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = note->saveState( doc, parent );
		QVERIFY( element.hasAttribute( "relvel" ) );
		QCOMPARE( element.attribute( "relvel" ).toInt(), kVelocity );
		Note saved;
		saved.restoreState( element );
		QCOMPARE( saved.releaseVelocity(), kVelocity );
		QCOMPARE( saved.midiReleaseVelocity( track->midiPort()->baseVelocity() ), kVelocity );

		// (criterion 3) A note-off carrying velocity 0 captures nothing: the
		// note keeps playing back a release velocity of 0, as before.
		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 64, 100 ) );
		NotePlayHandle* zero = track->playingNote( 64 );
		QVERIFY( zero != nullptr );
		track->processInEvent( MidiEvent( MidiNoteOff, kMemberChannel, 64, 0 ) );
		QCOMPARE( zero->releaseVelocity(), -1 );
		QCOMPARE( zero->midiReleaseVelocity( track->midiPort()->baseVelocity() ), 0 );

		// A note-off delivered as note-on velocity 0 (MidiClientRaw's spelling)
		// is the same zero-velocity case.
		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 65, 100 ) );
		NotePlayHandle* zeroOn = track->playingNote( 65 );
		QVERIFY( zeroOn != nullptr );
		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 65, 0 ) );
		QCOMPARE( zeroOn->releaseVelocity(), -1 );

		// (criterion 3) With MPE disabled the same stream captures nothing.
		MpeExpression::setEnabled( false );
		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 67, 100 ) );
		NotePlayHandle* nonMpe = track->playingNote( 67 );
		QVERIFY( nonMpe != nullptr );
		track->processInEvent( MidiEvent( MidiNoteOff, kMemberChannel, 67, kVelocity ) );
		QCOMPARE( nonMpe->releaseVelocity(), -1 );
		QCOMPARE( nonMpe->midiReleaseVelocity( track->midiPort()->baseVelocity() ), 0 );

		song->clearProject();
	}

	//! (review r1) Q050-039: the same capture at the port's DEFAULT base
	//! velocity (63), where one velocity step is 1.587 note-volume units: the
	//! velocity that arrived must be what the handle, the saved note and
	//! midiReleaseVelocity() all read back, with nothing set to 100 to make
	//! the scales agree.
	void noteOffVelocityIsExactAtTheDefaultBaseVelocity()
	{
		using namespace lmms;

		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create( Track::Type::Instrument, song ) );
		QVERIFY( track != nullptr );

		// The premise of this slot, named: the port is at the base velocity
		// MidiPort's model is born with, and this slot does not touch it.
		QCOMPARE( track->midiPort()->baseVelocity(), MidiMaxVelocity / 2 );

		MpeExpression::setEnabled( true );

		constexpr int kMemberChannel = 3;
		constexpr int kVelocity = 96;
		// The note-volume value the capture stores for 96 at base velocity 63:
		// 96 * 100 / 63 = 152.38, rounded to 152. Pinned so the exactness
		// assertions below say which conversion they are holding to.
		constexpr int kStored = 152;

		track->processInEvent( MidiEvent( MidiNoteOn, kMemberChannel, 60, 100 ) );
		NotePlayHandle* note = track->playingNote( 60 );
		QVERIFY( note != nullptr );

		track->processInEvent( MidiEvent( MidiNoteOff, kMemberChannel, 60, kVelocity ) );

		// The stored value, and the velocity it reads back as: exactly the
		// one the note-off carried, at the default base velocity.
		QCOMPARE( note->releaseVelocity(), kStored );
		QCOMPARE( note->midiReleaseVelocity( track->midiPort()->baseVelocity() ), kVelocity );

		// ... and the saved note keeps it: save -> load stores the volume
		// value, which still reads back as the velocity that arrived.
		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = note->saveState( doc, parent );
		QVERIFY( element.hasAttribute( "relvel" ) );
		QCOMPARE( element.attribute( "relvel" ).toInt(), kStored );
		Note saved;
		saved.restoreState( element );
		QCOMPARE( saved.releaseVelocity(), kStored );
		QCOMPARE( saved.midiReleaseVelocity( track->midiPort()->baseVelocity() ), kVelocity );

		MpeExpression::setEnabled( false );
		song->clearProject();
	}

	//! (review r1) Q050-039: capture (Note::releaseVelocityFromMidi) and
	//! playback (Note::midiReleaseVelocity) are exact inverses for EVERY
	//! velocity the volume scale can name at the two base velocities this
	//! unit runs at - the port's default (63) and the scale's default (100) -
	//! so no other velocity hides the rounding the 96 in the slots above
	//! depends on.
	void releaseVelocityRoundTripsAtEveryRepresentableVelocity()
	{
		using namespace lmms;

		const int bases[] = { MidiMaxVelocity / 2, DefaultVolume };
		for( const int base : bases )
		{
			// The volume scale (0..200) names velocities up to twice the base
			// velocity, so at 63 MIDI's 127 saturates to it and only 0..126
			// can round-trip.
			const int top = std::min( MidiMaxVelocity,
				static_cast<int>( MaxVolume ) * base / DefaultVolume );
			for( int velocity = 1; velocity <= top; ++velocity )
			{
				Note note;
				note.setReleaseVelocity( Note::releaseVelocityFromMidi( velocity, base ) );
				QCOMPARE( note.midiReleaseVelocity( base ), velocity );
			}
		}
	}

};

QTEST_GUILESS_MAIN(MpeInputPathTest)
#include "MpeInputPathTest.moc"
