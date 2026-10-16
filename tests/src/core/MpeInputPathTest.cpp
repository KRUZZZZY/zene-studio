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

};

QTEST_GUILESS_MAIN(MpeInputPathTest)
#include "MpeInputPathTest.moc"
