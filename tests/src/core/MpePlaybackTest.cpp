/*
 * MpePlaybackTest.cpp - per-note MPE pressure and timbre reach the instrument
 *                       (task #649)
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

/*! The proof task #649 owes: pressure and timbre are APPLIED, not merely
 *  stored.
 *
 *  What "applied" is measured as: ONE audio block is rendered through the real
 *  playback path - NotePlayHandle::play() -> InstrumentTrack::processOutEvent()
 *  -> Instrument::handleMidiEvent() -> the instrument's playNoteImpl() - for
 *  the same note twice, once carrying the expression and once carrying none,
 *  and the two blocks are compared. The instrument is
 *  tests/src/plugins/MpeTestConsumer.cpp, an in-tree MIT test subject that
 *  turns the expression it consumes into its own output level
 *  (`level = kBaseLevel * (1 + pressure/127) * (1 + timbre/127)`). It is the
 *  VEHICLE of this proof and nothing else in the tree can be: the built-in
 *  synthesisers never see MIDI at all (they are driven by a note's frequency
 *  and volume, and Instrument::handleMidiEvent is the base no-op), and every
 *  instrument that WOULD consume channel pressure and CC74 (Vestige, LV2,
 *  CLAP, Carla, a hardware MIDI port) is a hosted binary that is not installed
 *  on this box. Without the fixture the honest alternatives are "the events
 *  left the engine" - the drivable-but-inert shape this project has already
 *  paid for twice - or nothing.
 *
 *  Why the assertions cannot pass without the feature:
 *
 *   - the fixture's level is 1.0x when NO expression reached it, so if the
 *     route is missing every measured ratio below is exactly 1.0 and each
 *     `... == 1 + value/127` assertion fails;
 *   - the neutral case pins the route to the VALUES and not to the mere
 *     presence of an event: a note whose captured axes are both 0 must render
 *     byte-for-byte the same level as a note with no expression at all;
 *   - pressure and timbre are measured separately AND together, so one axis
 *     arriving cannot pay for the other.
 *
 *  The second half drives the LIVE half of the route: with MPE input on, a
 *  channel-pressure message arriving on the sounding note's own member channel
 *  reaches the same instrument and moves the next block - through
 *  InstrumentTrack::processInEvent() and the real MpeExpression state machine,
 *  not by calling the helper directly.
 *
 *  docs/KNOWN-LIMITATIONS.md carries the release's one-line statement this
 *  test backs; docs/MPE.md §4 is the per-axis table it closes.
 */

#include <QtTest>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Midi.h"
#include "MpeExpression.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! The one key every measurement plays. Any key inside the default 0..127
//! range works; the fixture ignores pitch entirely (its subject is the two
//! expression axes).
constexpr int kKey = 60;

//! What the fixture's own mapping promises for a pair of axes: +100% per axis
//! at full scale (tests/src/plugins/MpeTestConsumer.cpp, expressionGain()).
float expectedGain( int pressure, int timbre )
{
	constexpr float kFullScale = 127.f;
	return ( 1.0f + pressure / kFullScale ) * ( 1.0f + timbre / kFullScale );
}

} // namespace

class MpePlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The fixture module is the only plugin this test wants the scan to
		// see. Both variables must be set before anything reaches
		// PluginFactory::instance(): it reads its search paths in the
		// constructor and remembers what it found (src/core/PluginFactory.cpp,
		// setupSearchPaths and discoverPlugins). The private scan cache is
		// deliberate - a record another run wrote about this path must not
		// decide what this run finds.
		QVERIFY2( m_cacheDir.isValid(), "no temporary directory for the plugin scan cache" );
		const QByteArray fixtureDir = QByteArrayLiteral( LMMS_MPE_CONSUMER_DIR );
		qputenv( "LMMS_PLUGIN_DIR", fixtureDir );
		qputenv( "LMMS_PLUGIN_SCAN_CACHE",
			QFile::encodeName( m_cacheDir.filePath( QStringLiteral( "mpe-scan-cache.json" ) ) ) );

		Engine::init( true );
		// This test renders its blocks itself; nothing may pull on them in the
		// background.
		Engine::audioEngine()->audioDev()->stopProcessing();
		BufferManager::init( Engine::audioEngine()->framesPerPeriod() );
		NotePlayHandleManager::init();
		m_engineUp = true;

		m_frames = Engine::audioEngine()->framesPerPeriod();

		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(
			Track::create( Track::Type::Instrument, song ) );
		QVERIFY2( m_track != nullptr, "no instrument track" );

		// The fixture has to be FOUND before loadInstrument() is called: a
		// missing plugin is a DummyInstrument (src/core/Instrument.cpp,
		// Instrument::instantiate), which renders silence - and a silent
		// subject would make every level assertion below fail for a reason
		// that has nothing to do with the feature. Name the scan's own answer
		// instead.
		PluginFactory* factory = PluginFactory::instance();
		const PluginFactory::PluginInfo info = factory->pluginInfo( "mpe_test_consumer" );
		QVERIFY2( info.descriptor != nullptr,
			qPrintable( QStringLiteral( "the MPE test consumer fixture was not discovered in %1: %2; scan: %3" )
				.arg( QString::fromLocal8Bit( fixtureDir ),
					factory->errorString( QStringLiteral( "mpe_test_consumer" ) ),
					factory->scanReport() ) ) );

		Instrument* consumer = m_track->loadInstrument( QStringLiteral( "mpe_test_consumer" ) );
		QVERIFY2( consumer != nullptr && dynamic_cast<DummyInstrument*>( consumer ) == nullptr,
			"the MPE test consumer fixture loaded as the dummy instrument" );
	}

	void cleanupTestCase()
	{
		MpeExpression::setEnabled( false );
		if( m_engineUp )
		{
			Engine::getSong()->clearProject();
			Engine::destroy();
			m_engineUp = false;
		}
	}

	//! The acceptance measurement: one block with the captured expression and
	//! the same block without it, through the same instrument.
	void pressureAndTimbreReachTheInstrumentOnPlayback()
	{
		const float plain = renderWithExpression( 0, 0, false );
		const float neutral = renderWithExpression( 0, 0, true );
		const float pressure = renderWithExpression( 64, 0, true );
		const float timbre = renderWithExpression( 0, 64, true );
		const float both = renderWithExpression( 64, 64, true );

		std::fprintf( stdout,
			"MPE_EVIDENCE block=%u frames level plain=%.6f neutral=%.6f pressure64=%.6f "
			"timbre64=%.6f both64=%.6f\n",
			static_cast<unsigned>( m_frames ), plain, neutral, pressure, timbre, both );
		std::fflush( stdout );

		// The subject sounded at all (a silent subject - the dummy instrument -
		// would make the ratios below meaningless rather than failing).
		QVERIFY2( plain > 0.05f,
			qPrintable( QStringLiteral( "the consumer fixture rendered %1 for a note with no "
				"expression; expected a level near its base level" ).arg( plain ) ) );

		// A captured-but-neutral note is not a retune: the values are what
		// reach the instrument, not the presence of the events.
		QVERIFY2( std::fabs( neutral - plain ) < 1e-6f,
			qPrintable( QStringLiteral( "a neutral expression changed the block: %1 vs %2" )
				.arg( neutral ).arg( plain ) ) );

		// Each axis on its own, and the two together - so neither axis can
		// hide behind the other.
		const struct { const char* name; float measured; float wanted; } cases[] = {
			{ "pressure 64", pressure / plain, expectedGain( 64, 0 ) },
			{ "timbre 64", timbre / plain, expectedGain( 0, 64 ) },
			{ "pressure 64 + timbre 64", both / plain, expectedGain( 64, 64 ) },
		};
		for( const auto& c : cases )
		{
			QVERIFY2( std::fabs( c.measured - c.wanted ) < 0.01f,
				qPrintable( QStringLiteral( "%1: the block with the expression is %2x the block "
					"without it, and the fixture's own mapping asks for %3x - the axis did not "
					"reach the instrument" ).arg( QLatin1String( c.name ) )
						.arg( c.measured, 0, 'f', 4 ).arg( c.wanted, 0, 'f', 4 ) ) );
		}
	}

	//! The live half: a channel-pressure message arriving on the sounding
	//! note's own member channel moves the next block, through the real input
	//! path (InstrumentTrack::processInEvent -> MpeExpression ->
	//! loadMpeExpressionOntoChannelNotes).
	void aLivePressureEventReachesTheSoundingNote()
	{
		QVERIFY2( !MpeExpression::isEnabled(), "MPE input was left on by another test" );
		MpeExpression::setEnabled( true );

		// Note-on on member channel 1: the note is born with that channel's
		// (neutral) expression and is tracked by the MPE state machine.
		m_track->processInEvent( MidiEvent( MidiNoteOn, 1, kKey, 100 ) );
		NotePlayHandle* note = m_track->playingNote( kKey );
		QVERIFY2( note != nullptr, "the note-on did not start a note handle" );

		const float before = renderBlock( note );

		m_track->processInEvent( MidiEvent( MidiChannelPressure, 1, 64 ) );
		QCOMPARE( note->mpePressure(), 64 );

		const float after = renderBlock( note );

		std::fprintf( stdout, "MPE_EVIDENCE live pressure on channel 1: before=%.6f after=%.6f\n",
			before, after );
		std::fflush( stdout );

		QVERIFY2( before > 0.05f, "the note rendered nothing before the pressure event" );
		QVERIFY2( std::fabs( after / before - expectedGain( 64, 0 ) ) < 0.01f,
			qPrintable( QStringLiteral( "a live channel-pressure event on the note's own channel "
				"moved the block by %1x; the fixture asks for %2x" )
					.arg( after / before, 0, 'f', 4 )
					.arg( expectedGain( 64, 0 ), 0, 'f', 4 ) ) );

		m_track->processInEvent( MidiEvent( MidiNoteOff, 1, kKey ) );
		MpeExpression::setEnabled( false );
	}

private:
	//! One block of one note, rendered through the real playback path. The
	//! level is read from the LAST frame of the block: the declicking fade-in
	//! and the note's own attack live at the front of it, and a constant level
	//! means the tail is the value the whole sustained part holds.
	float renderBlock( NotePlayHandle* note )
	{
		std::vector<SampleFrame> buffer( m_frames, SampleFrame( 0.f, 0.f ) );
		note->play( std::span<SampleFrame>( buffer ) );
		return buffer[m_frames - 1][0];
	}

	//! Builds a fresh note (optionally carrying expression), plays ONE block
	//! of it through the track's instrument and returns the level it produced.
	float renderWithExpression( int pressure, int timbre, bool expression )
	{
		Note note( TimePos::fromFrames( m_frames * 8, Engine::framesPerTick() ),
			TimePos( 0 ), kKey );
		if( expression )
		{
			note.setMpeExpression( MpeNoteExpression{ 0, pressure, timbre } );
		}

		NotePlayHandle handle( m_track, 0, m_frames * 8, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );
		return renderBlock( &handle );
	}

	bool m_engineUp = false;
	InstrumentTrack* m_track = nullptr;
	f_cnt_t m_frames = 0;
	QTemporaryDir m_cacheDir;
};

QTEST_GUILESS_MAIN(MpePlaybackTest)
#include "MpePlaybackTest.moc"
