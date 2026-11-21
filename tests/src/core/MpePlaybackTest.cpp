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
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "../plugins/MpeConsumerProbe.h"
#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Midi.h"
#include "MpeExpression.h"
#include "MpeStreamSnapshot.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PluginFactory.h"
#include "RealtimeScope.h"
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

//! What the fixture's mapping promises for a note-off velocity (Q050-039):
//! +100% at full scale (tests/src/plugins/MpeTestConsumer.cpp, releaseGain()).
float expectedReleaseGain( int releaseVelocity )
{
	constexpr float kFullScale = 127.f;
	return 1.0f + releaseVelocity / kFullScale;
}

//! Q050-039 (review r1): the fixture's release block is 1 + velocity/127x the
//! sustained one, so the measured ratio NAMES the velocity the instrument
//! received - the exact integer behind the level, not a tolerance around it.
int namedReleaseVelocity( float released, float sustained )
{
	constexpr float kFullScale = 127.f;
	return static_cast<int>( std::lround( ( released / sustained - 1.0f ) * kFullScale ) );
}

/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime - the
 * module's import descriptor names zene.exe (ERROR_MOD_NOT_FOUND, 126) and the
 * product loads them inside zene.exe; AudioPluginTest.cpp carries the full
 * mechanism and the CI evidence.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

//! Installs the LMMS_TESTING stream-scan counter for one slot and always
//! removes it, so a failed assertion cannot leave a dangling counter behind.
struct MpeStreamScanCounterScope
{
	explicit MpeStreamScanCounterScope( std::size_t* counter )
	{
		NotePlayHandle::setMpeStreamPointScanCounterForTest( counter );
	}
	~MpeStreamScanCounterScope()
	{
		NotePlayHandle::setMpeStreamPointScanCounterForTest( nullptr );
	}
};

} // namespace

class MpePlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// This suite's whole subject is the fixture module below, and a Windows
		// test host cannot load it, so skip here rather than fail at the
		// discovery: a skipped initTestCase skips every slot (the tree relies on
		// that in AudioPluginTest.cpp), and cleanupTestCase() stays behind its
		// m_engineUp guard, which this path never sets.
		if( !testHostCanLoadPluginModules() )
		{
			QSKIP( "the MPE test consumer fixture cannot be loaded in a Windows test host: plugin "
				"modules link the zene executable, so on Windows their import descriptor names "
				"zene.exe and a test host cannot satisfy it; the product loads them inside zene.exe "
				"where that resolves by construction (CI msvc-x64: QLibrary::load -> "
				"ERROR_MOD_NOT_FOUND, 126)" );
		}

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

	//! Q050-039: a note's release velocity reaches the instrument as the
	//! MidiNoteOff velocity and colours its release block. The fixture asks for
	//! +velocity/127 on that block, so the measured ratio names the velocity it
	//! received.
	void noteOffVelocityReachesTheInstrumentOnPlayback()
	{
		// Align the note-volume scale (100 = default) with MIDI velocity, so the
		// note's stored release velocity IS the velocity the note-off emits.
		m_track->midiPort()->setBaseVelocity( DefaultVolume );

		constexpr int kReleaseVelocity = 96;

		Note note( TimePos::fromFrames( m_frames * 8, Engine::framesPerTick() ),
			TimePos( 0 ), kKey );
		note.setReleaseVelocity( kReleaseVelocity );

		NotePlayHandle handle( m_track, 0, m_frames * 8, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );

		const float sustained = renderBlock( &handle );
		QVERIFY2( sustained > 0.05f, "the note rendered nothing before its note-off" );

		// The note-off sends midiReleaseVelocity() to the instrument.
		handle.noteOff( 0 );
		const float released = renderBlock( &handle );

		const float wanted = expectedReleaseGain( kReleaseVelocity );
		std::fprintf( stdout,
			"MPE_EVIDENCE release velocity %d: sustained=%.6f released=%.6f ratio=%.6f\n",
			kReleaseVelocity, sustained, released, released / sustained );
		std::fflush( stdout );

		QVERIFY2( std::fabs( released / sustained - wanted ) < 0.01f,
			qPrintable( QStringLiteral( "the release block is %1x the sustained block; the "
				"fixture asks for %2x from the note-off velocity - the velocity did not "
				"reach the instrument" ).arg( released / sustained, 0, 'f', 4 )
				.arg( wanted, 0, 'f', 4 ) ) );

		// (review r1) the tolerance above cannot tell velocity 95 from 96 (the
		// ratios are 0.008 apart): the measured ratio also has to NAME the
		// exact velocity the note carries.
		QCOMPARE( namedReleaseVelocity( released, sustained ), kReleaseVelocity );

		// The shared track's port is left at the base velocity it was born
		// with, the way this suite found it.
		m_track->midiPort()->setBaseVelocity( MidiMaxVelocity / 2 );
	}

	//! (review r1) Q050-039: the same playback at the port's DEFAULT base
	//! velocity (63), with nothing set to 100 to make the scales agree, and
	//! the EXACT velocity checked: the release block must be 1 + 96/127x the
	//! sustained one, which names the velocity the instrument received.
	void noteOffVelocityIsExactAtTheDefaultBaseVelocity()
	{
		// The premise of this slot, named: the port sits at the base velocity
		// MidiPort's model is born with.
		QCOMPARE( m_track->midiPort()->baseVelocity(), MidiMaxVelocity / 2 );

		// What the capture stores for a note-off velocity of 96 at base
		// velocity 63 (InstrumentTrack::captureMpeReleaseVelocity ->
		// Note::releaseVelocityFromMidi): 96 * 100 / 63 = 152.38, rounded.
		constexpr int kReleaseVelocity = 96;
		constexpr int kStored = 152;

		Note note( TimePos::fromFrames( m_frames * 8, Engine::framesPerTick() ),
			TimePos( 0 ), kKey );
		note.setReleaseVelocity( kStored );

		NotePlayHandle handle( m_track, 0, m_frames * 8, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );

		const float sustained = renderBlock( &handle );
		QVERIFY2( sustained > 0.05f, "the note rendered nothing before its note-off" );

		// The note-off sends midiReleaseVelocity() to the instrument.
		handle.noteOff( 0 );
		const float released = renderBlock( &handle );

		const int named = namedReleaseVelocity( released, sustained );
		std::fprintf( stdout,
			"MPE_EVIDENCE default base velocity: stored=%d sustained=%.6f released=%.6f "
			"ratio=%.6f named velocity=%d\n",
			kStored, sustained, released, released / sustained, named );
		std::fflush( stdout );

		QCOMPARE( named, kReleaseVelocity );
	}

	//! An MPE event consumed by the owner must still reach the layer tracks
	//! selected by the live note-on, where their own matching note handles are
	//! updated through the same input path.
	void aLiveExpressionEventReachesLayerNotes()
	{
		QVERIFY2( !MpeExpression::isEnabled(), "MPE input was left on by another test" );
		const int layerIndex = m_track->layers().addLayer( QStringLiteral( "mpe_test_consumer" ), nullptr );
		QVERIFY2( layerIndex > 0, "the MPE consumer could not be loaded as a layer" );
		InstrumentTrack* layerTrack = m_track->layers().layer( layerIndex );
		QVERIFY( layerTrack != nullptr );

		MpeExpression::setEnabled( true );
		m_track->processInEvent( MidiEvent( MidiNoteOn, 1, kKey, 100 ) );
		NotePlayHandle* ownerNote = m_track->playingNote( kKey );
		NotePlayHandle* layerNote = layerTrack->playingNote( kKey );
		QVERIFY( ownerNote != nullptr );
		QVERIFY( layerNote != nullptr );

		m_track->processInEvent( MidiEvent( MidiPitchBend, 1, 12288 ) );
		m_track->processInEvent( MidiEvent( MidiChannelPressure, 1, 64 ) );
		m_track->processInEvent( MidiEvent( MidiControlChange, 1, MpeTimbreController, 50 ) );

		QCOMPARE( ownerNote->mpePitchCents(), 2400 );
		QCOMPARE( ownerNote->mpePressure(), 64 );
		QCOMPARE( ownerNote->mpeTimbre(), 50 );
		QCOMPARE( layerNote->mpePitchCents(), 2400 );
		QCOMPARE( layerNote->mpePressure(), 64 );
		QCOMPARE( layerNote->mpeTimbre(), 50 );

		m_track->processInEvent( MidiEvent( MidiNoteOff, 1, kKey ) );
		MpeExpression::setEnabled( false );
	}

	//! Q050-016 (the acceptance's MIDI-output half): a note carrying an
	//! expression STREAM reaches the instrument with each axis's change at the
	//! frame offset the point's tick schedules. The consumer probe records the
	//! offset each event arrived with, so the assertion is the offset itself,
	//! not a block the change can be inferred to have happened in.
	void scheduledExpressionStreamReachesTheInstrumentAtItsOffsets()
	{
		auto* probe = dynamic_cast<lmms::testmpe::ConsumerProbe*>( m_track->instrument() );
		QVERIFY2( probe != nullptr, "the MPE test consumer does not implement the probe" );

		// Several tick offsets must fall inside one period for the scheduled
		// frame offset to be observable: raise the tempo so frames-per-tick is
		// small next to the period.
		Engine::getSong()->tempoModel().setValue( 999 );
		Engine::updateFramesPerTick();
		const float framesPerTick = Engine::framesPerTick();
		QVERIFY( framesPerTick > 0.f );
		const int ticksPerPeriod = static_cast<int>( m_frames / framesPerTick );
		QVERIFY2( ticksPerPeriod >= 4,
			qPrintable( QStringLiteral( "only %1 tick(s) per period - the offsets cannot be told "
				"apart" ).arg( ticksPerPeriod ) ) );

		const f_cnt_t frames = m_frames * 8;
		const int pitchTick = 1;
		const int pressureTick = 2;
		const int timbreTick = 3;

		// A reference note with no expression in the same block, so the base
		// level is measured rather than hard-coded.
		Note plain( TimePos::fromFrames( frames, framesPerTick ), TimePos( 0 ), kKey );
		NotePlayHandle plainHandle( m_track, 0, frames, plain, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );
		const float plainLevel = renderBlock( &plainHandle );

		Note note( TimePos::fromFrames( frames, framesPerTick ), TimePos( 0 ), kKey );
		MpeExpressionStream stream;
		stream.setPitch( { { static_cast<tick_t>( pitchTick ), 1200 } } );
		stream.setPressure( { { static_cast<tick_t>( pressureTick ), 64 } } );
		stream.setTimbre( { { static_cast<tick_t>( timbreTick ), 32 } } );
		note.setMpeStream( stream );
		QVERIFY( note.hasMpeStream() );

		NotePlayHandle handle( m_track, 0, frames, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );
		// One period is enough: every point sits inside it.
		const float level = renderBlock( &handle );

		const int channel = handle.midiChannel();
		std::fprintf( stdout, "MPE_EVIDENCE stream offsets channel=%d level=%.6f "
			"pitch=%d@%llu pressure=%d@%llu timbre=%d@%llu fpt=%.3f\n", channel, level,
			probe->value( lmms::testmpe::Axis::Pitch, channel ),
			static_cast<unsigned long long>( probe->lastOffset( lmms::testmpe::Axis::Pitch, channel ) ),
			probe->value( lmms::testmpe::Axis::Pressure, channel ),
			static_cast<unsigned long long>( probe->lastOffset( lmms::testmpe::Axis::Pressure, channel ) ),
			probe->value( lmms::testmpe::Axis::Timbre, channel ),
			static_cast<unsigned long long>( probe->lastOffset( lmms::testmpe::Axis::Timbre, channel ) ),
			framesPerTick );
		std::fflush( stdout );

		QVERIFY2( plainLevel > 0.05f && level > 0.05f, "a note rendered nothing" );
		QCOMPARE( probe->value( lmms::testmpe::Axis::Pitch, channel ), 1200 );
		QCOMPARE( probe->value( lmms::testmpe::Axis::Pressure, channel ), 64 );
		QCOMPARE( probe->value( lmms::testmpe::Axis::Timbre, channel ), 32 );
		// Each event arrived at the frame its tick names: frame = tick * fpt.
		QCOMPARE( static_cast<long long>(
			probe->lastOffset( lmms::testmpe::Axis::Pitch, channel ) ),
			static_cast<long long>( std::lround( pitchTick * framesPerTick ) ) );
		QCOMPARE( static_cast<long long>(
			probe->lastOffset( lmms::testmpe::Axis::Pressure, channel ) ),
			static_cast<long long>( std::lround( pressureTick * framesPerTick ) ) );
		QCOMPARE( static_cast<long long>(
			probe->lastOffset( lmms::testmpe::Axis::Timbre, channel ) ),
			static_cast<long long>( std::lround( timbreTick * framesPerTick ) ) );

		// ... and the audio carries all three axes, by the ratios they ask for.
		const float wanted = ( 1.0f + 64.0f / 127.0f ) * ( 1.0f + 32.0f / 127.0f )
			* ( 1.0f + 1200.0f / 4800.0f );
		QVERIFY2( std::fabs( level / plainLevel - wanted ) < 0.01f,
			qPrintable( QStringLiteral( "the streamed block is %1x the plain one; the three "
				"scheduled axes ask for %2x" ).arg( level / plainLevel, 0, 'f', 4 )
					.arg( wanted, 0, 'f', 4 ) ) );
	}

	//! new-p20-84 (realtime): a period seeks to the stream points that are in
	//! its window instead of walking every earlier point again. The counter the
	//! LMMS_TESTING seam increments is deterministic work, not time: a history
	//! rescan counts the whole stream once per period, a seek counts each point
	//! once as the window passes it.
	void aPeriodSeeksToTheWindowInsteadOfRescanningTheHistory()
	{
		Engine::getSong()->tempoModel().setValue( 999 );
		Engine::updateFramesPerTick();
		const float framesPerTick = Engine::framesPerTick();
		QVERIFY2( framesPerTick > 0.f, "no frames per tick" );

		// A full stream, one point per tick, so "the whole stream is history"
		// is a real length rather than a handful of points.
		std::vector<MpeExpressionPoint> points;
		points.reserve( static_cast<std::size_t>( MpeExpressionStream::MaxPoints ) );
		for( int i = 0; i < MpeExpressionStream::MaxPoints; ++i )
		{
			points.push_back( MpeExpressionPoint{ static_cast<tick_t>( i ), i % 127 } );
		}

		const f_cnt_t period = m_frames;
		QVERIFY( period > 0 );
		const tick_t historyTicks = static_cast<tick_t>( MpeExpressionStream::MaxPoints + 8 );
		const int periodsToPassHistory = static_cast<int>(
			std::ceil( static_cast<double>( historyTicks ) * framesPerTick / period ) );
		QVERIFY( periodsToPassHistory > 0 );
		// Extra periods after the stream is behind the play position: the old
		// walk still scans every point on each of them.
		const int periods = periodsToPassHistory + 40;
		const f_cnt_t frames = static_cast<f_cnt_t>( periods + 4 ) * period;

		Note note( TimePos::fromFrames( frames, framesPerTick ), TimePos( 0 ), kKey );
		MpeExpressionStream stream;
		stream.setPitch( points );
		note.setMpeStream( stream );
		QVERIFY( note.hasMpeStream() );

		NotePlayHandle handle( m_track, 0, frames, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );

		std::size_t examined = 0;
		{
			const MpeStreamScanCounterScope scope( &examined );
			std::vector<SampleFrame> buffer( m_frames, SampleFrame( 0.f, 0.f ) );
			for( int i = 0; i < periods; ++i )
			{
				handle.play( std::span<SampleFrame>( buffer ) );
			}
		}

		// Each point belongs to exactly one half-open window, so a scheduler
		// that seeks examines the set once; 2x leaves room for a boundary
		// without ever allowing the per-period rescan.
		const std::size_t budget =
			2 * static_cast<std::size_t>( MpeExpressionStream::MaxPoints );
		std::fprintf( stdout,
			"MPE_EVIDENCE stream scan periods=%d points=%d examined=%llu budget=%llu\n",
			periods, MpeExpressionStream::MaxPoints,
			static_cast<unsigned long long>( examined ),
			static_cast<unsigned long long>( budget ) );
		std::fflush( stdout );
		QVERIFY2( examined <= budget,
			qPrintable( QStringLiteral( "the scheduler examined %1 stream points over %2 periods; "
				"at most %3 expected - it is walking the stream's history again" )
				.arg( static_cast<qulonglong>( examined ) ).arg( periods )
				.arg( static_cast<qulonglong>( budget ) ) ) );
	}

	//! Q050-016 round 2 (realtime): acquiring a handle for a streamed note -
	//! the audio thread's own path (InstrumentTrack::play ->
	//! NotePlayHandleManager::acquire -> the Note copy) - allocates nothing the
	//! plain note does not. The whole acquire is not allocation-free for ANY
	//! note (the handle builds its upstream detuning object), so this slot is
	//! the comparison; the stream's own operations are measured absolutely
	//! (0 allocations, 0 frees) in MpeStreamSnapshotTest and in
	//! MpeNoteStorageTest::streamedNoteCopiesAllocateNothing.
	void streamedHandleAcquisitionAllocatesNothingExtra()
	{
		// Warm up: the pool, the track's handle list and one-shot caches
		// allocate on first use, and that is not the stream's bill.
		Note warmNote( TimePos( 384 ), TimePos( 0 ), kKey );
		for( int i = 0; i < 2; ++i )
		{
			NotePlayHandle* warm = NotePlayHandleManager::acquire(
				m_track, 0, m_frames * 8, warmNote );
			NotePlayHandleManager::release( warm );
		}

		Note plain( TimePos( 384 ), TimePos( 0 ), kKey );
		Note streamed( TimePos( 384 ), TimePos( 0 ), kKey );
		MpeExpressionStream stream;
		stream.setPitch( { { 0, 0 }, { 96, 1200 } } );
		stream.setPressure( { { 0, 64 } } );
		stream.setTimbre( { { 48, 32 } } );
		streamed.setMpeStream( stream );
		QVERIFY( streamed.hasMpeStream() );

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		NotePlayHandle* plainHandle = NotePlayHandleManager::acquire(
			m_track, 0, m_frames * 8, plain );
		const std::uint64_t plainAllocations = lmms::test::tlAllocationCount;
		lmms::test::resetAllocationCount();
		NotePlayHandle* streamedHandle = NotePlayHandleManager::acquire(
			m_track, 0, m_frames * 8, streamed );
		const std::uint64_t streamedAllocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		std::fprintf( stdout, "MPE_EVIDENCE handle allocations plain=%llu streamed=%llu\n",
			static_cast<unsigned long long>( plainAllocations ),
			static_cast<unsigned long long>( streamedAllocations ) );
		std::fflush( stdout );

		NotePlayHandleManager::release( plainHandle );
		NotePlayHandleManager::release( streamedHandle );

		QCOMPARE( streamedAllocations, plainAllocations );
	}

	//! Q050-016 round 3 (realtime): the note is edited and then cleared while
	//! a handle for it is alive. The handle still plays exactly the stream it
	//! was started with (the edit published new snapshots; it never touched
	//! the one the handle holds), and when the handle is released on the
	//! realtime path holding the LAST reference, the snapshot is retired, not
	//! freed: it stays alive until a control-thread reclaim frees it.
	void editingWhileAHandlePlaysKeepsItsStreamAndItsReleaseNeverFrees()
	{
		auto* probe = dynamic_cast<lmms::testmpe::ConsumerProbe*>( m_track->instrument() );
		QVERIFY2( probe != nullptr, "the MPE test consumer does not implement the probe" );
		Engine::getSong()->tempoModel().setValue( 999 );
		Engine::updateFramesPerTick();
		const float framesPerTick = Engine::framesPerTick();
		QVERIFY( m_frames / framesPerTick >= 4 );

		MpeStreamSnapshot::reclaimRetired();
		const std::size_t live = MpeStreamSnapshot::liveCount();
		const std::size_t retired = MpeStreamSnapshot::retiredCount();
		const f_cnt_t frames = m_frames * 8;
		Note note( TimePos::fromFrames( frames, framesPerTick ), TimePos( 0 ), kKey );
		MpeExpressionStream started;
		started.setPitch( { { 1, 1200 } } );
		started.setPressure( { { 2, 64 } } );
		started.setTimbre( { { 3, 32 } } );
		note.setMpeStream( started );

		NotePlayHandle* handle = NotePlayHandleManager::acquire( m_track, 0, frames, note );

		// The editing thread replaces the stream, then removes it.
		MpeExpressionStream edited;
		edited.setPitch( { { 1, -1200 } } );
		edited.setPressure( { { 2, 10 } } );
		edited.setTimbre( { { 3, 5 } } );
		note.setMpeStream( edited );
		note.clearMpeStream();
		QVERIFY( !note.hasMpeStream() );
		QCOMPARE( MpeStreamSnapshot::liveCount(), live + 1 );  // only the handle's

		renderBlock( handle );
		const int channel = handle->midiChannel();
		QCOMPARE( probe->value( lmms::testmpe::Axis::Pitch, channel ), 1200 );
		QCOMPARE( probe->value( lmms::testmpe::Axis::Pressure, channel ), 64 );
		QCOMPARE( probe->value( lmms::testmpe::Axis::Timbre, channel ), 32 );
		QCOMPARE( static_cast<long long>( probe->lastOffset( lmms::testmpe::Axis::Timbre, channel ) ),
			static_cast<long long>( std::lround( 3 * framesPerTick ) ) );

		{
			const RealtimeScope realtime;
			NotePlayHandleManager::release( handle );
		}
		std::fprintf( stdout, "MPE_EVIDENCE realtime release live=%llu->%llu retired=%llu->%llu\n",
			static_cast<unsigned long long>( live + 1 ),
			static_cast<unsigned long long>( MpeStreamSnapshot::liveCount() ),
			static_cast<unsigned long long>( retired ),
			static_cast<unsigned long long>( MpeStreamSnapshot::retiredCount() ) );
		std::fflush( stdout );
		QCOMPARE( MpeStreamSnapshot::liveCount(), live + 1 );
		QCOMPARE( MpeStreamSnapshot::retiredCount(), retired + 1 );

		QCOMPARE( MpeStreamSnapshot::reclaimRetired(), retired + 1 );
		QCOMPARE( MpeStreamSnapshot::liveCount(), live );
		QCOMPARE( MpeStreamSnapshot::retiredCount(), std::size_t{ 0 } );
	}

	//! new-p16-146: a frequency mark is spent by the update it asks for. Once
	//! updateFrequency() has applied it, a later period must not recompute the
	//! note's frequency - only a new mark does. Without that, every later
	//! play() period of a marked note recomputes its frequency.
	void aFrequencyMarkIsSpentByItsUpdate()
	{
		Note note( TimePos::fromFrames( m_frames * 8, Engine::framesPerTick() ),
			TimePos( 0 ), kKey );
		NotePlayHandle handle( m_track, 0, m_frames * 8, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip );

		handle.setFrequencyUpdate();  // a pitch change marks the note...
		renderBlock( &handle );       // ...and the next period applies the mark
		const float applied = handle.frequency();

		// An input nothing marks: an MPE pitch bend of one octave (frequency ratio 2).
		handle.setMpePitchCents( 1200 );
		renderBlock( &handle );
		QVERIFY2( std::fabs( handle.frequency() - applied ) < 1e-3f,
			qPrintable( QStringLiteral( "the spent mark recomputed the frequency: %1 Hz became %2 Hz" )
				.arg( applied, 0, 'f', 3 ).arg( handle.frequency(), 0, 'f', 3 ) ) );

		// A new mark is honoured: the bend is applied on the next period.
		handle.setFrequencyUpdate();
		renderBlock( &handle );
		QVERIFY2( std::fabs( handle.frequency() - 2.f * applied ) < 1e-2f,
			qPrintable( QStringLiteral( "a new mark did not apply the bend: %1 Hz, wanted %2 Hz" )
				.arg( handle.frequency(), 0, 'f', 3 ).arg( 2.f * applied, 0, 'f', 3 ) ) );
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
