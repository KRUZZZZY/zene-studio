/*
 * SessionModelTest.cpp - Session View data layer end-to-end round trip
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

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "SessionModel.h"
#include "Song.h"

using namespace lmms;

namespace
{

QString readFile( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QString();
	}
	return QString::fromUtf8( file.readAll() );
}


//! The <z:scenes> ... </z:scenes> substring of a project file, for byte-exact
//! comparison across a save/load/save cycle (the section never nests). Since
//! ARCH-4 S8 this is the scene state's form in a project file; before S8 this
//! helper sliced the legacy <session> block, which the project writer no
//! longer emits (the block is the migration's input - SPEC-ARCH-4 row :403).
QString scenesSection( const QString& project )
{
	const int start = project.indexOf( QStringLiteral( "<z:scenes " ) );
	const int end = project.indexOf( QStringLiteral( "</z:scenes>" ) );
	if( start < 0 || end < 0 )
	{
		return QString();
	}
	return project.mid( start, end + 11 - start );
}

} // namespace


class SessionModelTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init( true );
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The deliverable: a saved project round-trips every clip-slot and scene
	//! field intact through a real .mmp file - as the native <z:scenes>
	//! section since ARCH-4 S8, with no legacy <session> block, and
	//! re-serialising the loaded model byte-identical.
	void songProjectRoundTripsSessionBlock()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY( song->sessionModel().isEmpty() );
		QVERIFY( !song->sessionModel().shouldPersist() );

		SessionModel& model = song->sessionModel();
		model.setTrackCount( 2 );
		model.setSceneCount( 2 );
		model.setGlobalLaunchQuantisation( LaunchQuantisation::FourBars );

		model.scene( 0 ).setName( QStringLiteral( "Verse" ) );
		model.scene( 1 ).setName( QStringLiteral( "Drop" ) );
		model.scene( 1 ).setTempoEnabled( true );
		model.scene( 1 ).setTempo( 174.0 );
		model.scene( 1 ).setTimeSigEnabled( true );
		model.scene( 1 ).setTimeSigNumerator( 7 );
		model.scene( 1 ).setTimeSigDenominator( 8 );
		model.scene( 1 ).setColor( QStringLiteral( "#ff8800" ) );  // 0.5.0 WF-062

		ClipSlot& midi = model.slot( 0, 0 );
		midi.setPatternReference( 11 );
		midi.setName( QStringLiteral( "Lead" ) );
		midi.setLaunchMode( LaunchMode::Repeat );
		midi.setLaunchQuantisation( LaunchQuantisation::Bar );
		midi.setLegato( true );
		midi.setLoopStart( 48 );
		midi.setLoopLength( 768 );
		midi.setGainDb( -3.25f );
		midi.setTranspose( 5 );
		midi.setDetune( -7 );
		midi.setRamMode( true );
		FollowAction jump;
		jump.type = FollowAction::Type::Jump;
		jump.chance = 0.6;
		jump.linked = false;
		jump.timeBars = 4.0;
		jump.jumpTo = 1;
		midi.addFollowAction( jump );
		FollowAction again;
		again.type = FollowAction::Type::PlayAgain;
		again.chance = 0.4;
		midi.addFollowAction( again );

		ClipSlot& audio = model.slot( 1, 1 );
		audio.setAudioReference( QStringLiteral( "samples/snare.wav" ) );
		audio.setName( QStringLiteral( "Snare" ) );
		audio.setLaunchMode( LaunchMode::Toggle );
		audio.setLaunchQuantisation( LaunchQuantisation::Global );
		audio.setLegato( true );
		audio.setLoopStart( 0 );
		audio.setLoopLength( 96 );
		audio.setGainDb( 1.5f );
		audio.setTranspose( -12 );
		audio.setDetune( 3 );

		QVERIFY( model.shouldPersist() );

		const QString first = dir.filePath( QStringLiteral( "first.mmp" ) );
		QVERIFY( song->saveProjectFile( first ) );
		const QString firstText = readFile( first );
		QVERIFY( !firstText.isEmpty() );

		// QDom serialises attributes in hash order, so verify the section
		// through the DOM rather than by substring. ARCH-4 S8: the document
		// form is the native top-level <z:scenes>, not the legacy block.
		QDomDocument parsed;
		QVERIFY( parsed.setContent( firstText.toUtf8() ) );
		const QDomElement savedScenes = parsed.documentElement()
			.firstChildElement( QStringLiteral( "song" ) )
			.firstChildElement( QStringLiteral( "z:scenes" ) );
		QVERIFY2( !savedScenes.isNull(),
			"the project writer must emit the native <z:scenes> section (ARCH-4 S8)" );
		QVERIFY2( parsed.documentElement()
				.firstChildElement( QStringLiteral( "song" ) )
				.firstChildElement( QStringLiteral( "session" ) ).isNull(),
			"the legacy <session> block must not be written any more - one "
			"authority for the scene state (SPEC-ARCH-4 migration row :403)" );
		QCOMPARE( savedScenes.attribute( QStringLiteral( "v" ) ).toInt(),
			SessionModel::CurrentVersion );
		QCOMPARE( savedScenes.attribute( QStringLiteral( "tracks" ) ).toInt(), 2 );
		QCOMPARE( savedScenes.attribute( QStringLiteral( "scenes" ) ).toInt(), 2 );
		QCOMPARE( savedScenes.attribute( QStringLiteral( "launchquantisation" ) ).toInt(),
			static_cast<int>( LaunchQuantisation::FourBars ) );
		QCOMPARE( savedScenes.elementsByTagName( QStringLiteral( "z:cell" ) ).length(), 2 );
		QVERIFY( firstText.contains( QStringLiteral( "pattern=\"11\"" ) ) );
		QVERIFY( firstText.contains( QStringLiteral( "src=\"samples/snare.wav\"" ) ) );
		const QString firstScenes = scenesSection( firstText );
		QVERIFY( !firstScenes.isEmpty() );

		// A new project must not inherit the previous one's session state.
		song->clearProject();
		QVERIFY( song->sessionModel().isEmpty() );
		QVERIFY( !song->sessionModel().hadSessionBlock() );

		song->loadProject( first );
		const SessionModel& loaded = song->sessionModel();
		QVERIFY( loaded.hadSessionBlock() );
		QCOMPARE( loaded.trackCount(), 2 );
		QCOMPARE( loaded.sceneCount(), 2 );
		QCOMPARE( static_cast<int>( loaded.globalLaunchQuantisation() ),
			static_cast<int>( LaunchQuantisation::FourBars ) );

		QCOMPARE( loaded.scene( 0 ).name(), QStringLiteral( "Verse" ) );
		QCOMPARE( loaded.scene( 1 ).name(), QStringLiteral( "Drop" ) );
		QCOMPARE( loaded.scene( 1 ).color(), QStringLiteral( "#ff8800" ) );
		QVERIFY2( loaded.scene( 0 ).color().isEmpty(), "a scene without a colour came back with one" );
		QVERIFY( loaded.scene( 1 ).tempoEnabled() );
		QVERIFY( loaded.scene( 1 ).tempo() == 174.0 );
		QVERIFY( loaded.scene( 1 ).timeSigEnabled() );
		QCOMPARE( loaded.scene( 1 ).timeSigNumerator(), 7 );
		QCOMPARE( loaded.scene( 1 ).timeSigDenominator(), 8 );
		QVERIFY( !loaded.scene( 0 ).tempoEnabled() );
		QVERIFY( !loaded.scene( 0 ).timeSigEnabled() );

		const ClipSlot& loadedMidi = loaded.slot( 0, 0 );
		QCOMPARE( static_cast<int>( loadedMidi.type() ), static_cast<int>( ClipSlot::Type::Midi ) );
		QCOMPARE( loadedMidi.patternId(), 11 );
		QCOMPARE( loadedMidi.name(), QStringLiteral( "Lead" ) );
		QCOMPARE( static_cast<int>( loadedMidi.launchMode() ), static_cast<int>( LaunchMode::Repeat ) );
		QCOMPARE( static_cast<int>( loadedMidi.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Bar ) );
		QCOMPARE( loadedMidi.legato(), true );
		QCOMPARE( loadedMidi.loopStart(), 48 );
		QCOMPARE( loadedMidi.loopLength(), 768 );
		QVERIFY( loadedMidi.gainDb() == -3.25f );
		QCOMPARE( loadedMidi.transpose(), 5 );
		QCOMPARE( loadedMidi.detune(), -7 );
		QCOMPARE( loadedMidi.ramMode(), true );
		QCOMPARE( loadedMidi.followActions().size(), std::size_t( 2 ) );
		QVERIFY( loadedMidi.followActions()[0] == jump );
		QVERIFY( loadedMidi.followActions()[1] == again );

		const ClipSlot& loadedAudio = loaded.slot( 1, 1 );
		QCOMPARE( static_cast<int>( loadedAudio.type() ), static_cast<int>( ClipSlot::Type::Audio ) );
		QCOMPARE( loadedAudio.audioSource(), QStringLiteral( "samples/snare.wav" ) );
		QCOMPARE( loadedAudio.patternId(), -1 );
		QCOMPARE( loadedAudio.name(), QStringLiteral( "Snare" ) );
		QCOMPARE( static_cast<int>( loadedAudio.launchMode() ), static_cast<int>( LaunchMode::Toggle ) );
		QCOMPARE( static_cast<int>( loadedAudio.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Global ) );
		QCOMPARE( loadedAudio.legato(), true );
		QCOMPARE( loadedAudio.loopLength(), 96 );
		QVERIFY( loadedAudio.gainDb() == 1.5f );
		QCOMPARE( loadedAudio.transpose(), -12 );
		QCOMPARE( loadedAudio.detune(), 3 );
		QVERIFY( loaded.slot( 0, 1 ).isEmpty() );

		// Save the loaded project again: the <z:scenes> section is byte-identical.
		const QString second = dir.filePath( QStringLiteral( "second.mmp" ) );
		QVERIFY( song->saveProjectFile( second ) );
		QCOMPARE( scenesSection( readFile( second ) ), firstScenes );

		// .mmpz is the same document, compressed (DataFile::writeFile).
		const QString compressed = dir.filePath( QStringLiteral( "compressed.mmpz" ) );
		QVERIFY( song->saveProjectFile( compressed ) );
		QVERIFY( !readFile( compressed ).startsWith( QStringLiteral( "<?xml" ) ) );
		song->clearProject();
		song->loadProject( compressed );
		QCOMPARE( song->sessionModel().slot( 0, 0 ).patternId(), 11 );
		QCOMPARE( song->sessionModel().slot( 1, 1 ).audioSource(), QStringLiteral( "samples/snare.wav" ) );
		QCOMPARE( song->sessionModel().scene( 1 ).timeSigNumerator(), 7 );
		QVERIFY( song->sessionModel().scene( 1 ).tempo() == 174.0 );
	}

	//! A project that never used the session view loads unchanged and
	//! re-saves byte-identically, with no <session> block.
	void preSessionProjectLoadsAndSavesUnchanged()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY( !song->sessionModel().shouldPersist() );

		const QString first = dir.filePath( QStringLiteral( "a.mmp" ) );
		QVERIFY( song->saveProjectFile( first ) );
		const QString firstText = readFile( first );
		QVERIFY( !firstText.isEmpty() );
		QVERIFY( !firstText.contains( QStringLiteral( "session" ) ) );

		song->loadProject( first );
		QVERIFY( !song->sessionModel().hadSessionBlock() );
		QVERIFY( song->sessionModel().isEmpty() );
		QVERIFY( !song->sessionModel().shouldPersist() );

		const QString second = dir.filePath( QStringLiteral( "b.mmp" ) );
		QVERIFY( song->saveProjectFile( second ) );
		const QString secondText = readFile( second );
		QVERIFY( !secondText.contains( QStringLiteral( "session" ) ) );
		QCOMPARE( secondText, firstText );
	}

	void malformedLegacySessionIsPreservedOnce()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		Song* song = Engine::getSong();
		song->clearProject();
		const QString source = dir.filePath( QStringLiteral( "legacy.mmp" ) );
		QVERIFY( song->saveProjectFile( source ) );
		QString project = readFile( source );
		const QString session = QStringLiteral(
			"<session version=\"1\" tracks=\"1\" scenes=\"1\"><scenes>"
			"<scene index=\"9\" name=\"must-survive\"/></scenes><clips/>"
			"</session>" );
		const int insertion = project.indexOf( QStringLiteral( "</song>" ) );
		QVERIFY( insertion >= 0 );
		project.insert( insertion, session );
		QVERIFY( QFile( source ).remove() );
		QFile file( source );
		QVERIFY( file.open( QIODevice::WriteOnly ) );
		QVERIFY( file.write( project.toUtf8() ) == project.toUtf8().size() );
		file.close();
		song->loadProject( source );
		const QString saved = dir.filePath( QStringLiteral( "saved.mmp" ) );
		QVERIFY( song->saveProjectFile( saved ) );
		const QString output = readFile( saved );
		QCOMPARE( output.count( QStringLiteral( "must-survive" ) ), 1 );
		QCOMPARE( output.count( QStringLiteral( "must-survive" ) ), 1 );
	}

	/*! A pattern attribute that is not an int restores as "no reference" (-1),
	 *  never as pattern 0: QString::toInt() returns 0 when the conversion
	 *  fails, and 0 is a VALID pattern id, so a malformed or overflowing
	 *  reference silently pointed the cell at the first pattern in the store.
	 */
	void anUnreadablePatternIdIsNotPatternZero()
	{
		QDomDocument doc;
		doc.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"999999999999\"/>" ) );
		const QDomElement overflow = doc.documentElement();
		QVERIFY( overflow.isElement() );
		ClipSlot slot;
		slot.restoreState( overflow );
		QCOMPARE( static_cast<int>( slot.type() ), static_cast<int>( ClipSlot::Type::Midi ) );
		QCOMPARE( slot.patternId(), -1 );

		doc.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"abc\"/>" ) );
		ClipSlot text;
		text.restoreState( doc.documentElement() );
		QCOMPARE( text.patternId(), -1 );

		// A missing attribute keeps its documented default.
		doc.setContent( QStringLiteral( "<clip type=\"1\"/>" ) );
		ClipSlot absent;
		absent.restoreState( doc.documentElement() );
		QCOMPARE( absent.patternId(), -1 );

		// A well-formed id still round-trips, including 0 itself.
		doc.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"7\"/>" ) );
		ClipSlot good;
		good.restoreState( doc.documentElement() );
		QCOMPARE( good.patternId(), 7 );
		doc.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"0\"/>" ) );
		ClipSlot zero;
		zero.restoreState( doc.documentElement() );
		QCOMPARE( zero.patternId(), 0 );
	}

	/*! A <session> launchquantisation that is not an integer restores as the
	 *  default grid (Bar), the fallback a non-grid integer already gets.
	 *  QString::toInt() returns 0 when the conversion fails, and 0 is the None
	 *  grid, so a project with launchquantisation="bar" launched at once instead
	 *  of on the bar line. A real "0" must still restore as None.
	 */
	void anUnreadableSessionLaunchQuantisationFallsBackToBar()
	{
		const QString block = QStringLiteral(
			"<session version=\"1\" tracks=\"1\" scenes=\"1\" launchquantisation=\"%1\"><scenes/><clips/></session>" );
		QDomDocument doc;
		doc.setContent( block.arg( QStringLiteral( "bar" ) ) );
		SessionModel text;
		QVERIFY( text.restoreState( doc.documentElement() ) );
		QCOMPARE( static_cast<int>( text.globalLaunchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Bar ) );

		doc.setContent( block.arg( QStringLiteral( "0" ) ) );
		SessionModel none;
		QVERIFY( none.restoreState( doc.documentElement() ) );
		QCOMPARE( static_cast<int>( none.globalLaunchQuantisation() ),
			static_cast<int>( LaunchQuantisation::None ) );
	}

	//! The native <z:scenes> section persists the same attribute through the
	//! same conversion, so a spoiled value there must fall back the same way.
	void anUnreadableSceneSectionLaunchQuantisationFallsBackToBar()
	{
		QDomDocument legacy;
		legacy.setContent( QStringLiteral( "<session version=\"1\" tracks=\"1\" scenes=\"1\" launchquantisation=\"4\"><scenes/><clips/></session>" ) );
		SessionModel source;
		QVERIFY( source.restoreState( legacy.documentElement() ) );

		QDomDocument doc;
		QDomElement root = doc.createElement( QStringLiteral( "song" ) );
		doc.appendChild( root );
		QDomElement section = source.saveScenesSection( doc, root );
		QVERIFY( !section.isNull() );
		section.setAttribute( QStringLiteral( "launchquantisation" ), QStringLiteral( "bar" ) );

		SessionModel scenes;
		QVERIFY( scenes.restoreScenesSection( section ) );
		QCOMPARE( static_cast<int>( scenes.globalLaunchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Bar ) );
	}

	void malformedLegacySessionDimensionsArePreserved()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		Song* song = Engine::getSong();
		song->clearProject();
		const QString source = dir.filePath( QStringLiteral( "legacy.mmp" ) );
		QVERIFY( song->saveProjectFile( source ) );
		const QString baseProject = readFile( source );
		const QStringList sessions = {
			QStringLiteral( "<session version=\"1\" tracks=\"bad\" scenes=\"1\" marker=\"bad-tracks\"><scenes/><clips/></session>" ),
			QStringLiteral( "<session version=\"1\" tracks=\"257\" scenes=\"1\" marker=\"large-tracks\"><scenes/><clips/></session>" ),
			QStringLiteral( "<session version=\"1\" tracks=\"1\" scenes=\"513\" marker=\"large-scenes\"><scenes/><clips/></session>" )
		};
		const QStringList markers = {
			QStringLiteral( "bad-tracks" ), QStringLiteral( "large-tracks" ), QStringLiteral( "large-scenes" )
		};

		int index = 0;
		for( const QString& session : sessions )
		{
			song->clearProject();
			QString project = baseProject;
			const int insertion = project.indexOf( QStringLiteral( "</song>" ) );
			QVERIFY( insertion >= 0 );
			project.insert( insertion, session );
			QVERIFY( QFile::remove( source ) );
			QFile file( source );
			QVERIFY( file.open( QIODevice::WriteOnly ) );
			QVERIFY( file.write( project.toUtf8() ) == project.toUtf8().size() );
			file.close();

			song->loadProject( source );
			const QString saved = dir.filePath( QStringLiteral( "saved.mmp" ) );
			QVERIFY( song->saveProjectFile( saved ) );
			const QString output = readFile( saved );
			QCOMPARE( output.count( markers.at( index ) ), 1 );
			QVERIFY( !output.contains( QStringLiteral( "<z:scenes" ) ) );
			++index;
		}
	}

	/*! A legacy <session> block holding more valid sub-scenes than SubScene::MaxCount is refused as a whole, like its
	 *  malformed dimensions. Reading only the first MaxCount rows silently saved a shorter list than the file held.
	 *  Rows that do not parse are still left out and do not count towards the limit.
	 */
	void excessLegacySubScenesAreRefusedNotTruncated()
	{
		const auto legacyBlock = []( int valid, int invalid, const QString& marker ) {
			QString rows;
			for( int row = 0; row < valid; ++row )
			{
				rows += QStringLiteral( "<subscene name=\"row%1\" scene=\"0\" tracks=\"0\"/>" ).arg( row );
			}
			for( int row = 0; row < invalid; ++row )
			{
				rows += QStringLiteral( "<subscene name=\"bad\" scene=\"x\" tracks=\"0\"/>" );
			}
			return QStringLiteral( "<session version=\"1\" tracks=\"1\" scenes=\"1\" marker=\"%1\"><scenes/><clips/>"
				"<subscenes>%2</subscenes></session>" ).arg( marker, rows );
		};

		SessionModel model;
		QDomDocument atLimit;
		QVERIFY( atLimit.setContent( legacyBlock( SubScene::MaxCount, 0, QStringLiteral( "at-limit" ) ) ) );
		QVERIFY( model.restoreState( atLimit.documentElement() ) );
		QCOMPARE( model.subScenes().size(), std::size_t( SubScene::MaxCount ) );

		QDomDocument overLimit;
		QVERIFY( overLimit.setContent( legacyBlock( SubScene::MaxCount + 1, 0, QStringLiteral( "over-limit" ) ) ) );
		QVERIFY( !model.restoreState( overLimit.documentElement() ) );
		QVERIFY( model.subScenes().empty() );

		QDomDocument unreadableRows;
		QVERIFY( unreadableRows.setContent( legacyBlock( SubScene::MaxCount, 3, QStringLiteral( "unreadable" ) ) ) );
		QVERIFY( model.restoreState( unreadableRows.documentElement() ) );
		QCOMPARE( model.subScenes().size(), std::size_t( SubScene::MaxCount ) );

		// Through a project file the refused block is kept verbatim, as the malformed dimensions are, not re-saved shorter.
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		Song* song = Engine::getSong();
		song->clearProject();
		const QString source = dir.filePath( QStringLiteral( "legacy-subscenes.mmp" ) );
		QVERIFY( song->saveProjectFile( source ) );
		QString project = readFile( source );
		const int insertion = project.indexOf( QStringLiteral( "</song>" ) );
		QVERIFY( insertion >= 0 );
		project.insert( insertion, legacyBlock( SubScene::MaxCount + 1, 0, QStringLiteral( "excess-subscenes" ) ) );
		QVERIFY( QFile::remove( source ) );
		QFile file( source );
		QVERIFY( file.open( QIODevice::WriteOnly ) );
		QVERIFY( file.write( project.toUtf8() ) == project.toUtf8().size() );
		file.close();

		song->loadProject( source );
		const QString saved = dir.filePath( QStringLiteral( "saved-subscenes.mmp" ) );
		QVERIFY( song->saveProjectFile( saved ) );
		const QString output = readFile( saved );
		QCOMPARE( output.count( QStringLiteral( "excess-subscenes" ) ), 1 );
		QVERIFY( !output.contains( QStringLiteral( "<z:subscenes" ) ) );
	}

	//! A slot references either a pattern or an audio clip, never both
	//! (SPEC-zene-studio A1 mutual exclusivity).
	void slotReferenceIsMutuallyExclusive()
	{
		ClipSlot slot;
		QVERIFY( slot.isEmpty() );
		slot.setPatternReference( 3 );
		QCOMPARE( static_cast<int>( slot.type() ), static_cast<int>( ClipSlot::Type::Midi ) );
		QCOMPARE( slot.patternId(), 3 );
		QVERIFY( slot.audioSource().isEmpty() );

		slot.setAudioReference( QStringLiteral( "loop.wav" ) );
		QCOMPARE( static_cast<int>( slot.type() ), static_cast<int>( ClipSlot::Type::Audio ) );
		QCOMPARE( slot.patternId(), -1 );
		QCOMPARE( slot.audioSource(), QStringLiteral( "loop.wav" ) );

		slot.setPatternReference( 9 );
		QVERIFY( slot.audioSource().isEmpty() );
		QCOMPARE( slot.patternId(), 9 );

		slot.clear();
		QVERIFY( slot.isEmpty() );
		QCOMPARE( slot.patternId(), -1 );
	}

	/*! Every launch quantisation survives a save/load cycle. The conversion from
	 *  the persisted int recognised only None, Bar, TwoBars and FourBars, so the
	 *  sub-bar values (Sixteenth, Eighth, Quarter, Half) and EightBars fell to the
	 *  fallback: a cell set to a 1/16 grid came back as Global, and a GLOBAL
	 *  EightBars came back as Bar.
	 */
	void everyLaunchQuantisationSurvivesAReload()
	{
		const struct { LaunchQuantisation value; int persisted; } values[] = {
			{ LaunchQuantisation::None, 0 },
			{ LaunchQuantisation::Bar, 1 },
			{ LaunchQuantisation::TwoBars, 2 },
			{ LaunchQuantisation::FourBars, 4 },
			{ LaunchQuantisation::Sixteenth, -2 },
			{ LaunchQuantisation::Eighth, -3 },
			{ LaunchQuantisation::Quarter, -4 },
			{ LaunchQuantisation::Half, -5 },
			{ LaunchQuantisation::EightBars, 8 },
		};
		for( const auto& v : values )
		{
			QDomDocument doc;
			doc.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"5\" quantisation=\"%1\"/>" )
				.arg( v.persisted ) );
			ClipSlot slot;
			slot.restoreState( doc.documentElement() );
			QCOMPARE( static_cast<int>( slot.launchQuantisation() ),
				static_cast<int>( v.value ) );
		}

		// Global is a per-clip pointer to the session default, and it is the
		// fallback the missing cases took - so it must still restore as itself.
		QDomDocument global;
		global.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"5\" quantisation=\"-1\"/>" ) );
		ClipSlot inherited;
		inherited.restoreState( global.documentElement() );
		QCOMPARE( static_cast<int>( inherited.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Global ) );

		// A value that is not a grid at all still falls back rather than
		// producing an enum no switch has a case for.
		QDomDocument junk;
		junk.setContent( QStringLiteral( "<clip type=\"1\" pattern=\"5\" quantisation=\"99\"/>" ) );
		ClipSlot unknown;
		unknown.restoreState( junk.documentElement() );
		QCOMPARE( static_cast<int>( unknown.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::Global ) );
	}

	//! The session-wide setting persists through the native <z:scenes> section
	//! and had the same recognised-only-four problem: a global EightBars came
	//! back as Bar. Every grid must survive that save and reload.
	void everyGlobalLaunchQuantisationSurvivesAReload()
	{
		const LaunchQuantisation grids[] = {
			LaunchQuantisation::None,
			LaunchQuantisation::Bar,
			LaunchQuantisation::TwoBars,
			LaunchQuantisation::FourBars,
			LaunchQuantisation::Sixteenth,
			LaunchQuantisation::Eighth,
			LaunchQuantisation::Quarter,
			LaunchQuantisation::Half,
			LaunchQuantisation::EightBars,
		};
		for( const LaunchQuantisation grid : grids )
		{
			SessionModel source;
			source.setTrackCount( 1 );
			source.setSceneCount( 1 );
			source.setGlobalLaunchQuantisation( grid );

			QDomDocument doc;
			QDomElement root = doc.createElement( QStringLiteral( "song" ) );
			doc.appendChild( root );
			QDomElement section = source.saveScenesSection( doc, root );
			QVERIFY( !section.isNull() );

			SessionModel reloaded;
			QVERIFY( reloaded.restoreScenesSection( section ) );
			QCOMPARE( static_cast<int>( reloaded.globalLaunchQuantisation() ),
				static_cast<int>( grid ) );
		}
	}

	//! Growing and shrinking the grid keeps the slots that remain.
	void gridResizePreservesSlots()
	{
		SessionModel model;
		model.setTrackCount( 2 );
		model.setSceneCount( 2 );
		model.slot( 1, 1 ).setPatternReference( 5 );
		model.scene( 1 ).setName( QStringLiteral( "B" ) );

		model.setSceneCount( 4 );
		QCOMPARE( model.slot( 1, 1 ).patternId(), 5 );
		QCOMPARE( model.scene( 1 ).name(), QStringLiteral( "B" ) );

		model.setTrackCount( 3 );
		QCOMPARE( model.slot( 1, 1 ).patternId(), 5 );

		model.setSceneCount( 1 );
		QVERIFY( model.slot( 1, 1 ).isEmpty() );
		QVERIFY( model.scene( 1 ).name().isEmpty() );
		QCOMPARE( model.slot( 1, 0 ).patternId(), -1 );
	}
};

QTEST_GUILESS_MAIN( SessionModelTest )
#include "SessionModelTest.moc"
