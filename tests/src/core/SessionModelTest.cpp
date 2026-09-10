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


//! The <session> ... </session> substring of a project file, for byte-exact
//! comparison across a save/load/save cycle (session blocks never nest).
QString sessionBlock( const QString& project )
{
	const int start = project.indexOf( QStringLiteral( "<session " ) );
	const int end = project.indexOf( QStringLiteral( "</session>" ) );
	if( start < 0 || end < 0 )
	{
		return QString();
	}
	return project.mid( start, end + 10 - start );
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

	//! The deliverable: a saved project round-trips a <session> block with
	//! every clip-slot and scene field intact through a real .mmp file.
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

		// QDom serialises attributes in hash order, so verify the block
		// through the DOM rather than by substring.
		QDomDocument parsed;
		QVERIFY( parsed.setContent( firstText.toUtf8() ) );
		const QDomElement savedSession = parsed.documentElement()
			.firstChildElement( QStringLiteral( "song" ) )
			.firstChildElement( QStringLiteral( "session" ) );
		QVERIFY( !savedSession.isNull() );
		QCOMPARE( savedSession.attribute( QStringLiteral( "version" ) ).toInt(),
			SessionModel::CurrentVersion );
		QCOMPARE( savedSession.attribute( QStringLiteral( "tracks" ) ).toInt(), 2 );
		QCOMPARE( savedSession.attribute( QStringLiteral( "scenes" ) ).toInt(), 2 );
		QCOMPARE( savedSession.attribute( QStringLiteral( "launchquantisation" ) ).toInt(),
			static_cast<int>( LaunchQuantisation::FourBars ) );
		QCOMPARE( savedSession.firstChildElement( QStringLiteral( "clips" ) )
			.elementsByTagName( QStringLiteral( "clip" ) ).length(), 2 );
		QVERIFY( firstText.contains( QStringLiteral( "pattern=\"11\"" ) ) );
		QVERIFY( firstText.contains( QStringLiteral( "src=\"samples/snare.wav\"" ) ) );
		const QString firstSession = sessionBlock( firstText );
		QVERIFY( !firstSession.isEmpty() );

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

		// Save the loaded project again: the <session> block is byte-identical.
		const QString second = dir.filePath( QStringLiteral( "second.mmp" ) );
		QVERIFY( song->saveProjectFile( second ) );
		QCOMPARE( sessionBlock( readFile( second ) ), firstSession );

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
