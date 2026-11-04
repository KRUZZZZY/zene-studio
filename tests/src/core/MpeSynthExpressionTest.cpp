/*
 * MpeSynthExpressionTest.cpp - MPE pressure and timbre are HEARD on a
 *                              built-in synthesiser (Q050-311)
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

/*! Q050-311. MpePlaybackTest proves the two axes LEAVE the engine as MIDI (its
 *  fixture maps them itself); this suite proves a built-in synthesiser - the
 *  build's real TripleOscillator module, which never sees MIDI - now SOUNDS
 *  them, through the real NotePlayHandle::play() -> InstrumentTrack::playNote()
 *  -> processAudioBuffer() path:
 *
 *   - pressure is intensity (level up, spectral shape unchanged), CC74 is
 *     brightness (high band up), each measured alone and together against the
 *     same note with no expression;
 *   - a live channel-pressure / CC74 message on one held note's member channel
 *     moves that note and leaves the neighbouring held note bit-identical;
 *   - an expression authored with note.expression_set reads back, saves, opens
 *     and renders identically; a project without MPE attributes opens to notes
 *     with no expression that render bit-identically and never enter the stage.
 *
 *  The DSP slots (the mapping, the exact neutral identity, the ramp, zero
 *  allocations) need no plug-in module and run on every platform; the render
 *  slots load a module, which a Windows test host cannot (see MpePlaybackTest),
 *  and skip there one by one.
 */

#include <QtTest>

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MpeExpression.h"
#include "MpeSynthExpression.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr int kKey = 60;
constexpr int kNeighbourKey = 64;
constexpr float kRate = 44100.f;

ControlResult run( const QString& id, const QJsonObject& args = {} )
{
	return ControlRegistry::instance()->invoke( id, args );
}

constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

#define SKIP_WITHOUT_MODULES() \
	do { if( !testHostCanLoadPluginModules() ) { \
		QSKIP( "a Windows test host cannot load plug-in modules (see MpePlaybackTest)" ); } } while( 0 )

//! Left-channel RMS of frames [from, size).
double rms( const std::vector<SampleFrame>& frames, std::size_t from = 0 )
{
	double sum = 0.;
	for( std::size_t i = from; i < frames.size(); ++i ) { sum += double( frames[i][0] ) * frames[i][0]; }
	return frames.size() > from ? std::sqrt( sum / double( frames.size() - from ) ) : 0.;
}

//! Brightness: RMS of the first difference over RMS of the signal - a gain-free
//! measure of how much of the energy sits in the high band.
double brightness( const std::vector<SampleFrame>& frames, std::size_t from = 1 )
{
	double diff = 0.;
	for( std::size_t i = std::max<std::size_t>( from, 1 ); i < frames.size(); ++i )
	{
		const double d = double( frames[i][0] ) - frames[i - 1][0];
		diff += d * d;
	}
	const double level = rms( frames, from );
	return level > 0. ? std::sqrt( diff / double( frames.size() - from ) ) / level : 0.;
}

//! A sine at \a hz, \a count frames.
std::vector<SampleFrame> sine( float hz, std::size_t count )
{
	std::vector<SampleFrame> out( count );
	for( std::size_t i = 0; i < count; ++i )
	{
		const float v = 0.5f * std::sin( 6.28318530718f * hz * float( i ) / kRate );
		out[i] = SampleFrame( v, v );
	}
	return out;
}

std::vector<SampleFrame> slice( const std::vector<SampleFrame>& in, std::size_t from, std::size_t count )
{
	return { in.begin() + from, in.begin() + from + count };
}

bool identical( const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b )
{
	if( a.size() != b.size() ) { return false; }
	for( std::size_t i = 0; i < a.size(); ++i )
	{
		if( a[i][0] != b[i][0] || a[i][1] != b[i][1] ) { return false; }
	}
	return true;
}

} // namespace

class MpeSynthExpressionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		if( testHostCanLoadPluginModules() )
		{
#ifdef LMMS_TEST_PLUGIN_DIR
			qputenv( "LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR );
#endif
		}
		Engine::init( true );
		Engine::audioEngine()->audioDev()->stopProcessing();
		BufferManager::init( Engine::audioEngine()->framesPerPeriod() );
		NotePlayHandleManager::init();
		ControlRegistry::setReady( true );
		m_frames = Engine::audioEngine()->framesPerPeriod();
	}

	void cleanupTestCase()
	{
		MpeExpression::setEnabled( false );
		ControlRegistry::setReady( false );
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	// ---- the stage itself (every platform) -------------------------------

	void theMappingIsNeutralAtZeroAndFullAtFullScale()
	{
		QCOMPARE( MpeSynthExpression::pressureGain( 0 ), 1.f );
		QCOMPARE( MpeSynthExpression::pressureGain( 127 ), MpeSynthExpression::MaxPressureGain );
		QCOMPARE( MpeSynthExpression::pressureGain( 500 ), MpeSynthExpression::MaxPressureGain );
		QCOMPARE( MpeSynthExpression::brightness( 0 ), 0.f );
		QCOMPARE( MpeSynthExpression::brightness( -4 ), 0.f );
		QCOMPARE( MpeSynthExpression::brightness( 127 ), MpeSynthExpression::MaxBrightness );
	}

	void neutralValuesAreTheExactIdentity()
	{
		const std::vector<SampleFrame> input = sine( 3000.f, 1024 );
		std::vector<SampleFrame> shaped = input;
		MpeSynthExpression::State state;
		MpeSynthExpression::process( state, shaped.data(), 512, 0, 0, kRate );
		MpeSynthExpression::process( state, shaped.data() + 512, 512, 0, 0, kRate );
		QVERIFY( identical( shaped, input ) );
	}

	void pressureIsIntensityAndTimbreIsBrightness()
	{
		const std::size_t n = 4096;
		auto shape = []( std::vector<SampleFrame> buf, int pressure, int timbre )
		{
			MpeSynthExpression::State state;
			MpeSynthExpression::process( state, buf.data(), static_cast<f_cnt_t>( buf.size() ),
				pressure, timbre, kRate );
			return buf;
		};
		const std::vector<SampleFrame> low = sine( 100.f, n );
		const std::vector<SampleFrame> high = sine( 8000.f, n );
		const std::size_t settle = 256;

		// Pressure: every band by the same gain.
		const double gain = MpeSynthExpression::pressureGain( 64 );
		QVERIFY( std::fabs( rms( shape( low, 64, 0 ), settle ) / rms( low, settle ) - gain ) < 0.005 );
		QVERIFY( std::fabs( rms( shape( high, 64, 0 ), settle ) / rms( high, settle ) - gain ) < 0.005 );

		// Timbre: the low band (well under the 1 kHz corner) nearly untouched,
		// the high band raised towards 1 + brightness.
		const double lowRatio = rms( shape( low, 0, 127 ), settle ) / rms( low, settle );
		const double highRatio = rms( shape( high, 0, 127 ), settle ) / rms( high, settle );
		std::fprintf( stdout, "MPE311_EVIDENCE dsp timbre127 low100Hz=%.4f high8kHz=%.4f\n", lowRatio, highRatio );
		QVERIFY2( lowRatio < 1.1, qPrintable( QString::number( lowRatio ) ) );
		QVERIFY2( highRatio > 3.5, qPrintable( QString::number( highRatio ) ) );
	}

	void aChangeRampsAcrossTheBlockWithoutAllocating()
	{
		std::vector<SampleFrame> buf( 512, SampleFrame( 0.25f, 0.25f ) );
		MpeSynthExpression::State state;
		MpeSynthExpression::process( state, buf.data(), 256, 0, 0, kRate );	// primed at neutral

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		MpeSynthExpression::process( state, buf.data() + 256, 256, 127, 0, kRate );
		test::tlCountAllocations = false;
		QCOMPARE( static_cast<qulonglong>( test::tlAllocationCount ), 0ull );

		// Ramped, not stepped: the first shaped frame is near unity, the last at 2x.
		QVERIFY( buf[256][0] < 0.26f );
		QVERIFY( std::fabs( buf[511][0] - 0.5f ) < 1e-5f );
		QCOMPARE( state.gain, MpeSynthExpression::MaxPressureGain );
	}

	// ---- a real built-in synthesiser (module) ----------------------------

	//! Acceptance 1: one note rendered with no expression, a neutral capture,
	//! pressure only, CC74 only and both, through TripleOscillator.
	void aBuiltInSynthSoundsPressureAndTimbreDistinctly()
	{
		SKIP_WITHOUT_MODULES();
		QVERIFY( freshTrack() );

		const auto plain = renderNote( plainNote() ).frames;
		const auto neutral = renderNote( expressiveNote( 0, 0 ) ).frames;
		const auto pressure = renderNote( expressiveNote( 64, 0 ) ).frames;
		const auto timbre = renderNote( expressiveNote( 0, 64 ) ).frames;
		const auto both = renderNote( expressiveNote( 64, 64 ) ).frames;

		const std::size_t from = m_frames;	// skip the onset block
		const double level = rms( plain, from );
		const double bright = brightness( plain, from );
		QVERIFY2( level > 0.01, "TripleOscillator rendered silence" );

		const double pLevel = rms( pressure, from ) / level, pBright = brightness( pressure, from ) / bright;
		const double tLevel = rms( timbre, from ) / level, tBright = brightness( timbre, from ) / bright;
		const double bLevel = rms( both, from ) / level, bBright = brightness( both, from ) / bright;
		std::fprintf( stdout, "MPE311_EVIDENCE tripleoscillator level/brightness vs plain: "
			"pressure64=%.4f/%.4f timbre64=%.4f/%.4f both=%.4f/%.4f\n",
			pLevel, pBright, tLevel, tBright, bLevel, bBright );
		std::fflush( stdout );

		// A captured-but-neutral note sounds exactly like a note with none.
		QVERIFY( identical( neutral, plain ) );
		// Pressure: louder by the mapping's gain, same spectral shape.
		QVERIFY( std::fabs( pLevel - MpeSynthExpression::pressureGain( 64 ) ) < 0.01 );
		QVERIFY( std::fabs( pBright - 1. ) < 0.01 );
		// Timbre: brighter (the measure is gain-free, so pressure cannot pay for it).
		QVERIFY2( tBright > 1.15, qPrintable( QString::number( tBright ) ) );
		// Both: the pressure gain on top of the timbre render, the timbre's brightness.
		QVERIFY( std::fabs( bLevel / tLevel - MpeSynthExpression::pressureGain( 64 ) ) < 0.01 );
		QVERIFY( std::fabs( bBright - tBright ) < 0.01 );
	}

	//! Acceptance 1, live: device.mpe_set on, two held notes on two member
	//! channels; pressure on one channel and CC74 on the other each move only
	//! their own note, through InstrumentTrack::processInEvent.
	void liveExpressionMovesOnlyItsOwnNote()
	{
		SKIP_WITHOUT_MODULES();
		QVERIFY( freshTrack() );
		QVERIFY( run( QStringLiteral( "device.mpe_set" ), { { QStringLiteral( "enabled" ), true } } ).ok );

		const LiveRun control = playLive( false, false );
		const LiveRun pressed = playLive( true, false );
		const LiveRun shaded = playLive( false, true );
		QVERIFY( run( QStringLiteral( "device.mpe_set" ), { { QStringLiteral( "enabled" ), false } } ).ok );
		QVERIFY( !control.a.empty() && !control.b.empty() );

		// The block after the change's ramp block.
		const std::size_t at = 3 * m_frames;
		const auto block = [this, at]( const std::vector<SampleFrame>& v ) { return slice( v, at, m_frames ); };
		const double aGain = rms( block( pressed.a ) ) / rms( block( control.a ) );
		const double bBright = brightness( block( shaded.b ) ) / brightness( block( control.b ) );
		std::fprintf( stdout, "MPE311_EVIDENCE live: pressure ch1 note gain=%.4f, cc74 ch2 note brightness=%.4f\n",
			aGain, bBright );
		std::fflush( stdout );

		// Before the event, nothing differs.
		QVERIFY( identical( slice( pressed.a, 0, 2 * m_frames ), slice( control.a, 0, 2 * m_frames ) ) );
		QVERIFY( std::fabs( aGain - MpeSynthExpression::pressureGain( 64 ) ) < 0.01 );
		QVERIFY2( bBright > 1.15, qPrintable( QString::number( bBright ) ) );
		// The neighbours are bit-identical to the run with no event.
		QVERIFY( identical( pressed.b, control.b ) );
		QVERIFY( identical( shaded.a, control.a ) );
	}

	//! Acceptance 2: an agent-authored expression reads back, survives the
	//! project file and renders the same after opening; a project without MPE
	//! attributes opens to plain notes that render bit-identically and never
	//! enter the stage.
	void expressionRoundTripsAndOldProjectsSoundTheSame()
	{
		SKIP_WITHOUT_MODULES();
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		// The old (no-expression) project.
		QVERIFY( freshTrack() );
		const Rendered plainBefore = renderNote( *m_clip->notes().front() );
		const QString oldPath = dir.filePath( QStringLiteral( "old.mmp" ) );
		QVERIFY( saveAndReopen( oldPath ) );
		const QByteArray oldText = readFile( oldPath );
		QVERIFY( !oldText.contains( "mpepressure" ) && !oldText.contains( "mpetimbre" ) && !oldText.contains( "mpepitch" ) );
		const Note* reopened = m_clip->notes().front();
		QVERIFY( !reopened->hasMpeExpression() );
		const Rendered plainAfter = renderNote( *reopened );
		QVERIFY( identical( plainAfter.frames, plainBefore.frames ) );
		QVERIFY2( !plainAfter.stageRan, "a note without expression entered the MPE synth stage" );

		// The expressive project, authored through the agent surface.
		const QJsonObject target{ { QStringLiteral( "clip" ), control::clipIdOf( m_clip ) },
			{ QStringLiteral( "note" ), control::noteIdOf( m_clip->notes().front() ) } };
		QJsonObject set = target;
		set.insert( QStringLiteral( "pressure" ), 100 );
		set.insert( QStringLiteral( "timbre" ), 80 );
		const ControlResult wrote = run( QStringLiteral( "note.expression_set" ), set );
		QVERIFY2( wrote.ok, qPrintable( wrote.errorMessage ) );
		const Rendered expressive = renderNote( *m_clip->notes().front() );
		QVERIFY( expressive.stageRan );
		QVERIFY( rms( expressive.frames, m_frames ) > 1.3 * rms( plainBefore.frames, m_frames ) );

		const QString newPath = dir.filePath( QStringLiteral( "expressive.mmp" ) );
		QVERIFY( saveAndReopen( newPath ) );
		QVERIFY( readFile( newPath ).contains( "mpepressure=\"100\"" ) );
		const QJsonObject got = run( QStringLiteral( "note.expression_get" ),
			QJsonObject{ { QStringLiteral( "clip" ), control::clipIdOf( m_clip ) },
				{ QStringLiteral( "note" ), control::noteIdOf( m_clip->notes().front() ) } } ).result;
		QCOMPARE( got.value( QStringLiteral( "has_expression" ) ).toBool(), true );
		QCOMPARE( got.value( QStringLiteral( "pressure" ) ).toInt(), 100 );
		QCOMPARE( got.value( QStringLiteral( "timbre" ) ).toInt(), 80 );
		QVERIFY( identical( renderNote( *m_clip->notes().front() ).frames, expressive.frames ) );
	}

private:
	struct Rendered { std::vector<SampleFrame> frames; bool stageRan = false; };
	struct LiveRun { std::vector<SampleFrame> a; std::vector<SampleFrame> b; };

	static constexpr int kBlocks = 6;

	//! A fresh project: one TripleOscillator track (all three oscillators saw,
	//! so the timbre axis has harmonics to brighten) and one clip with one note.
	bool freshTrack()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>( Track::create( Track::Type::Instrument, song ) );
		if( m_track == nullptr ) { return false; }
		Instrument* synth = m_track->loadInstrument( QStringLiteral( "tripleoscillator" ) );
		if( synth == nullptr || dynamic_cast<DummyInstrument*>( synth ) != nullptr ) { return false; }
		QDomDocument doc;
		QDomElement parent = doc.createElement( QStringLiteral( "parent" ) );
		QDomElement settings = synth->saveState( doc, parent );
		for( int i = 0; i < 3; ++i ) { settings.setAttribute( QStringLiteral( "wavetype%1" ).arg( i ), 2 ); }
		synth->restoreState( settings );
		m_clip = dynamic_cast<MidiClip*>( m_track->createClip( TimePos( 0 ) ) );
		if( m_clip == nullptr ) { return false; }
		m_clip->addNote( plainNote(), false );
		return true;
	}

	Note plainNote() const
	{
		return Note( TimePos::fromFrames( m_frames * 64, Engine::framesPerTick() ), TimePos( 0 ), kKey );
	}

	Note expressiveNote( int pressure, int timbre ) const
	{
		Note note = plainNote();
		note.setMpeExpression( MpeNoteExpression{ 0, pressure, timbre } );
		return note;
	}

	Rendered renderNote( const Note& note )
	{
		NotePlayHandle handle( m_track, 0, m_frames * 64, note, nullptr, -1, NotePlayHandle::Origin::MidiClip );
		Rendered out;
		for( int b = 0; b < kBlocks; ++b ) { playBlock( &handle, out.frames ); }
		out.stageRan = handle.mpeSynthState().primed;
		return out;
	}

	void playBlock( NotePlayHandle* handle, std::vector<SampleFrame>& into )
	{
		std::vector<SampleFrame> buffer( m_frames, SampleFrame( 0.f, 0.f ) );
		handle->play( std::span<SampleFrame>( buffer ) );
		into.insert( into.end(), buffer.begin(), buffer.end() );
	}

	//! Two live notes (key 60 on member channel 1, key 64 on channel 2), six
	//! blocks each; after block 2 optionally pressure 64 on channel 1 and/or
	//! CC74 64 on channel 2.
	LiveRun playLive( bool pressure, bool timbre )
	{
		LiveRun out;
		m_track->processInEvent( MidiEvent( MidiNoteOn, 1, kKey, 100 ) );
		m_track->processInEvent( MidiEvent( MidiNoteOn, 2, kNeighbourKey, 100 ) );
		NotePlayHandle* a = m_track->playingNote( kKey );
		NotePlayHandle* b = m_track->playingNote( kNeighbourKey );
		if( a == nullptr || b == nullptr ) { return {}; }
		for( int block = 0; block < kBlocks; ++block )
		{
			if( block == 2 && pressure ) { m_track->processInEvent( MidiEvent( MidiChannelPressure, 1, 64 ) ); }
			if( block == 2 && timbre )
			{
				m_track->processInEvent( MidiEvent( MidiControlChange, 2, MpeTimbreController, 64 ) );
			}
			playBlock( a, out.a );
			playBlock( b, out.b );
		}
		m_track->processInEvent( MidiEvent( MidiNoteOff, 1, kKey, 0 ) );
		m_track->processInEvent( MidiEvent( MidiNoteOff, 2, kNeighbourKey, 0 ) );
		// Reset the channels' expression so the next run starts neutral.
		m_track->processInEvent( MidiEvent( MidiChannelPressure, 1, 0 ) );
		m_track->processInEvent( MidiEvent( MidiControlChange, 2, MpeTimbreController, 0 ) );
		return out;
	}

	bool saveAndReopen( const QString& path )
	{
		if( !run( QStringLiteral( "project.save" ), { { QStringLiteral( "path" ), path } } ).ok ) { return false; }
		Engine::getSong()->clearProject();
		Engine::getSong()->setLoadOnLaunch( false );
		if( !run( QStringLiteral( "project.open" ), { { QStringLiteral( "path" ), path } } ).ok ) { return false; }
		m_track = nullptr;
		for( Track* track : Engine::getSong()->tracks() )
		{
			if( auto* instrument = dynamic_cast<InstrumentTrack*>( track ) ) { m_track = instrument; }
		}
		if( m_track == nullptr || m_track->getClips().empty() ) { return false; }
		m_clip = dynamic_cast<MidiClip*>( m_track->getClips().front() );
		return m_clip != nullptr && m_clip->notes().size() == 1;
	}

	static QByteArray readFile( const QString& path )
	{
		QFile file( path );
		return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
	}

	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	f_cnt_t m_frames = 0;
};

QTEST_GUILESS_MAIN( MpeSynthExpressionTest )
#include "MpeSynthExpressionTest.moc"
