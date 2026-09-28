/*
 * SessionPlaybackRenderTest.cpp - the RENDER proof of board card #597.
 *
 * The claim: a launched session slot whose cell holds a MIDI pattern produces
 * AUDIBLE output in a song whose arrangement is silent. SessionPlaybackTest
 * proves the hand-off (content reaches the render path); this proves the sound.
 *
 * The fixture is built so the claim cannot be satisfied accidentally: the song's
 * only arrangement clip carries an all-zero buffer, so with nothing launched the
 * export is digital silence for its whole length. So "the render became
 * non-silent" can only have come from the session clip.
 *
 * WHY ITS OWN FILE: SessionSchedulerRenderTest.cpp's fixture is deliberately
 * plugin-free, and this proof needs a sounding instrument. It guards on the
 * instrument being loadable and SKIPS - never passes - when it is not.
 *
 * THREE ENVIRONMENT REQUIREMENTS this file documents, each measured rather than
 * guessed, and each one a trap for the next person:
 *   1. NotePlayHandleManager::init() - called ONLY from src/core/main.cpp:579, so
 *      a test binary that plays a NOTE must call it or acquire() dereferences a
 *      null pool (SIGSEGV in NotePlayHandle.cpp:736). MpePlaybackTest,
 *      MpeInputPathTest and SessionTuningTest all call it for the same reason.
 *   2. ControlRegistry::setReady(true) - the control surface refuses engine
 *      commands until startup is declared finished; without it session.set_grid
 *      answers "engine_starting".
 *   3. MidiClip::addNote must be called with quant_pos = FALSE: with the default
 *      it evaluates gui::getGUI()->pianoRoll() unguarded (src/tracks/MidiClip.cpp
 *      :184) and segfaults headless. The agent surface passes false too
 *      (src/core/ControlCommandsNotes.cpp:126).
 *
 * The launch goes through the AGENT SURFACE (session.launch_slot), not through
 * SessionScheduler::requestLaunch(): the command is what publishes the cell's
 * content, and SPEC section 5 requires a capability be proved through the agent
 * surface rather than through internal APIs.
 */
#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <vector>

#include "ControlRegistry.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "OutputSettings.h"
#include "PatternStore.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "SessionScheduler.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;

//! Invoke a command through the registry, exactly as the socket does.
ControlResult run( const QString& id, const QJsonObject& args = QJsonObject() )
{
	return ControlRegistry::instance()->invoke( id, args );
}

/*! The WAV's sample payload, or an empty array when there is no `data` chunk.
 *  Searched from offset 12, so the RIFF/WAVE preamble cannot match. */
QByteArray pcmData( const QByteArray& wav )
{
	const int at = wav.indexOf( "data", 12 );
	if( at < 0 || at + 8 > wav.size() )
	{
		return QByteArray();
	}
	return wav.mid( at + 8 );
}

/*! True when every sample byte is zero.
 *
 *  Measured on the PAYLOAD, not the file: a WAV header is never all-zero bytes,
 *  so a whole-file comparison would report silence as sound. */
bool isSilent( const QByteArray& wav )
{
	const QByteArray pcm = pcmData( wav );
	if( pcm.isEmpty() )
	{
		return true;
	}
	for( const char byte : pcm )
	{
		if( byte != '\0' )
		{
			return false;
		}
	}
	return true;
}

} // namespace


class SessionPlaybackRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
		// 1. The play-handle pool exists only if main() built it; a test binary
		//    that plays a note must build it itself.
		NotePlayHandleManager::init();
		// 2. Commands need the surface told that startup finished.
		ControlRegistry::setReady( true );
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! THE PROOF: a launched session slot renders audio where the arrangement is
	//! silent.
	void aLaunchedSlotWithContentRendersAudio()
	{
		if( !buildFixture( /*withContent=*/true ) )
		{
			QSKIP( "no sounding instrument is loadable in this environment" );
		}

		const QByteArray silent = render();
		QVERIFY2( !silent.isEmpty(), "the export produced no file" );
		QVERIFY2( isSilent( silent ),
			"the fixture's arrangement is not silent, so a launch cannot be what makes it sound" );

		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		const ControlResult launch = run( QStringLiteral( "session.launch_slot" ),
			{{QStringLiteral( "track" ), 0}, {QStringLiteral( "scene" ), 0}} );
		QVERIFY2( launch.ok, qPrintable( launch.errorMessage ) );

		const QByteArray launched = render();

		const SessionModel& model = Engine::getSong()->sessionModel();
		const SessionSlotPlayback view = scheduler.playbackForColumn( 0 );
		std::fprintf( stderr,
			"DIAG: grid=%dx%d cell.patternId=%d empty=%d | playing=%d patternId=%d pos=%d | active=%d | patternLen=%d\n",
			model.trackCount(), model.sceneCount(),
			model.slot( 0, 0 ).patternId(), int( model.slot( 0, 0 ).isEmpty() ),
			int( view.playing ), view.patternId, int( view.positionTicks ),
			int( scheduler.trackIsSessionActive( 0 ) ),
			static_cast<int>( Engine::patternStore()->lengthOfPattern( 0 ) ) );

		QVERIFY2( scheduler.completedLaunches() > 0,
			"the launch never reached the audio thread" );
		QVERIFY2( !isSilent( launched ),
			"a launched session slot holding a pattern rendered SILENCE - the content "
			"reached the scheduler but not the output" );

		std::fprintf( stdout, "AB_EVIDENCE session-slot-silent pcm_bytes=%d\n",
			static_cast<int>( pcmData( silent ).size() ) );
		std::fprintf( stdout, "AB_EVIDENCE session-slot-launched pcm_bytes=%d\n",
			static_cast<int>( pcmData( launched ).size() ) );
		std::fflush( stdout );
	}

	//! Every quantisation name the surface accepts must survive a set/get round
	//! trip. The parser maps a name through the vocabulary list, and that list is
	//! NOT ordered by the enum's value (`four_bars` is 4 but sits third), so an
	//! index-as-value parser returns an unrelated value: "four_bars" would come
	//! back as something else entirely, and any clip set to it would silently
	//! quantise on a different grid. Proved through the surface, which is what an
	//! agent actually calls.
	void everyQuantisationNameSurvivesASetGetRoundTrip()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY2( run( QStringLiteral( "session.set_grid" ),
			{{QStringLiteral( "tracks" ), 1}, {QStringLiteral( "scenes" ), 1}} ).ok,
			"session.set_grid refused" );

		const QStringList names = {QStringLiteral( "none" ),
			QStringLiteral( "1/16" ), QStringLiteral( "1/8" ),
			QStringLiteral( "1/4" ), QStringLiteral( "1/2" ),
			QStringLiteral( "bar" ), QStringLiteral( "two_bars" ),
			QStringLiteral( "four_bars" ), QStringLiteral( "8_bars" )};

		for ( const QString& name : names )
		{
			const ControlResult slot = run( QStringLiteral( "session.set_slot" ),
				{{QStringLiteral( "track" ), 0}, {QStringLiteral( "scene" ), 0},
				 {QStringLiteral( "type" ), QStringLiteral( "midi" )},
				 {QStringLiteral( "pattern" ), 0},
				 {QStringLiteral( "quantisation" ), name}} );
			QVERIFY2( slot.ok, qPrintable( slot.errorMessage ) );

			const ControlResult state = run( QStringLiteral( "session.get_state" ) );
			QVERIFY( state.ok );
			QString readBack;
			for ( const QJsonValue& cell : state.result.value( QStringLiteral( "slots" ) ).toArray() )
			{
				const QJsonObject entry = cell.toObject();
				if ( entry.value( QStringLiteral( "track" ) ).toInt() == 0
					&& entry.value( QStringLiteral( "scene" ) ).toInt() == 0 )
				{
					readBack = entry.value( QStringLiteral( "quantisation" ) ).toString();
				}
			}
			QVERIFY2( readBack == name,
				qPrintable( QStringLiteral( "quantisation '%1' read back as '%2'")
					.arg( name ).arg( readBack ) ) );
		}
	}

	//! The control: a column taken over by a cell that holds NOTHING produces no
	//! sound.
	//!
	//! Launched through the SCHEDULER, not the command, and that is the point:
	//! `session.launch_slot` REFUSES an empty cell ("there is no clip to launch;
	//! session.set_slot defines one") - measured - so the agent surface cannot
	//! reach this state at all. The state is still reachable from the engine
	//! (SessionSchedulerRenderTest drives it the same way), and the render path
	//! must be silent in it: the take-over is real and there is nothing to play.
	void aColumnTakenOverByAnEmptyCellStaysSilent()
	{
		if( !buildFixture( /*withContent=*/false ) )
		{
			QSKIP( "no sounding instrument is loadable in this environment" );
		}

		const QByteArray before = render();
		QVERIFY( isSilent( before ) );

		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) );

		const QByteArray after = render();
		QVERIFY2( scheduler.completedLaunches() > 0, "the launch never fired" );
		QVERIFY2( isSilent( after ),
			"a column taken over by an EMPTY cell produced sound" );
	}

private:
	/*! A silent arrangement plus a pattern that does sound.
	 *
	 *  \a withContent false leaves the session cell EMPTY while still building the
	 *  sounding pattern, so the control differs from the proof in one variable.
	 *  False when no sounding instrument could be loaded. */
	bool buildFixture( bool withContent )
	{
		Song* song = Engine::getSong();
		song->clearProject();

		// 1. A silent 1-second arrangement clip: the export needs a length, and
		//    nothing else may make a sound. Integer-derived samples: all zero.
		const std::size_t frames = static_cast<std::size_t>( kSampleRate );
		std::vector<SampleFrame> data( frames );
		auto buffer = std::make_shared<const SampleBuffer>( std::move( data ), kSampleRate );

		auto* track = new SampleTrack( song );
		track->setName( QStringLiteral( "SilentArrangement" ) );
		auto* clip = new SampleClip( track );
		clip->setSampleBuffer( buffer );
		clip->movePosition( TimePos( 0 ) );
		song->updateLength();

		// 2. The pattern store's own INSTRUMENT track holds the notes, and it is
		//    what PatternStore::play() walks. (A PatternTrack is a different
		//    thing: a PatternTrack : Track in the SONG that references a pattern;
		//    it carries no instrument and no notes.)
		auto* patternTrack = dynamic_cast<InstrumentTrack*>(
			Track::create( Track::Type::Instrument, Engine::patternStore() ) );
		if( patternTrack == nullptr )
		{
			return false;
		}
		Instrument* instrument = patternTrack->loadInstrument( QStringLiteral( "tripleoscillator" ) );
		if( instrument == nullptr || dynamic_cast<DummyInstrument*>( instrument ) != nullptr )
		{
			// A missing plugin becomes a DummyInstrument, which renders silence -
			// so a silent render would prove nothing. Skip rather than pass.
			return false;
		}

		const int pattern = 0;
		Engine::patternStore()->createClipsForPattern( pattern );

		auto* patternClip = dynamic_cast<MidiClip*>( patternTrack->getClip( 0 ) );
		if( patternClip == nullptr )
		{
			return false;
		}
		// A held note over the whole bar. quant_pos FALSE - see the header note.
		patternClip->addNote( Note( TimePos( 190 ), TimePos( 0 ), 60, 100 ), false );

		// 3. The grid and the cell, through the product's own commands, so the
		//    fixture cannot set a cell the real surface would have refused.
		const ControlResult grid = run( QStringLiteral( "session.set_grid" ),
			{{QStringLiteral( "tracks" ), 1}, {QStringLiteral( "scenes" ), 1}} );
		if( !grid.ok )
		{
			std::fprintf( stderr, "session.set_grid refused: %s\n",
				qPrintable( grid.errorMessage ) );
			return false;
		}
		if( withContent )
		{
			const ControlResult slot = run( QStringLiteral( "session.set_slot" ),
				{{QStringLiteral( "track" ), 0}, {QStringLiteral( "scene" ), 0},
				 {QStringLiteral( "type" ), QStringLiteral( "midi" )},
				 {QStringLiteral( "pattern" ), pattern}} );
			if( !slot.ok )
			{
				std::fprintf( stderr, "session.set_slot refused: %s\n",
					qPrintable( slot.errorMessage ) );
				return false;
			}
		}
		return true;
	}

	//! Renders through the real export path and returns the WAV bytes.
	QByteArray render()
	{
		const QString path = m_dir.filePath( QStringLiteral( "session-playback.wav" ) );
		QFile::remove( path );

		const OutputSettings settings( kSampleRate, 192,
			OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo );
		ProjectRenderer renderer( settings, ProjectRenderer::ExportFileFormat::Wave, path );
		if( !renderer.isReady() )
		{
			return QByteArray();
		}
		renderer.startProcessing();
		renderer.wait();

		QFile file( path );
		if( !file.open( QIODevice::ReadOnly ) )
		{
			return QByteArray();
		}
		const QByteArray bytes = file.readAll();
		file.close();
		return bytes;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN( SessionPlaybackRenderTest )
#include "SessionPlaybackRenderTest.moc"
