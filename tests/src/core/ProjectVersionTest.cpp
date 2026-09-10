/*
 * ProjectVersionTest.cpp
 *
 * Copyright (c) 2015 Lukas W <lukaswhl/at/gmail.com>
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

#include "ProjectVersion.h"

#include <QtTest>

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_SESSION_VIEW

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QTextStream>

#include "DataFile.h"
#include "SessionModel.h"

namespace
{

QString fileToString( lmms::DataFile& dataFile )
{
	QString out;
	QTextStream stream( &out );
	dataFile.write( stream );
	stream.flush();
	return out;
}


QString nodeToString( const QDomElement& node )
{
	QString out;
	QTextStream stream( &out );
	node.save( stream, 2 );
	stream.flush();
	return out;
}

} // namespace

#endif // LMMS_HAVE_SESSION_VIEW


class ProjectVersionTest : public QObject
{
	Q_OBJECT
private slots:
	void ProjectVersionComparisonTests()
	{
		using namespace lmms;

		QVERIFY(ProjectVersion("1.1.0", ProjectVersion::CompareType::Minor) > "1.0.3");
		QVERIFY(ProjectVersion("1.1.0", ProjectVersion::CompareType::Major) < "2.1.0");
		QVERIFY(ProjectVersion("1.1.0", ProjectVersion::CompareType::Release) > "0.2.1");
		QVERIFY(ProjectVersion("1.1.4", ProjectVersion::CompareType::Release) < "1.1.10");
		QVERIFY(ProjectVersion("1.1.0", ProjectVersion::CompareType::Minor) == "1.1.5");
		QVERIFY( ! ( ProjectVersion("3.1.0", ProjectVersion::CompareType::Minor) < "2.2.5" ) );
		QVERIFY( ! ( ProjectVersion("2.5.0", ProjectVersion::CompareType::Release) < "2.2.5" ) );
		//A pre-release version has lower precedence than a normal version
		QVERIFY(ProjectVersion("1.1.0") > "1.1.0-alpha");
		//But higher precedence than the previous version
		QVERIFY(ProjectVersion("1.1.0-alpha") > "1.0.0");
		//Identifiers with letters or hyphens are compare lexically in ASCII sort order
		QVERIFY(ProjectVersion("1.1.0-alpha") < "1.1.0-beta");
		QVERIFY(ProjectVersion("1.2.0-rc1") < "1.2.0-rc2");
		//Build metadata MUST be ignored when determining version precedence
		QVERIFY(ProjectVersion("1.2.2") == "1.2.2+metadata");
		QVERIFY(ProjectVersion("1.0.0-alpha") < "1.0.0-alpha.1");
		QVERIFY(ProjectVersion("1.0.0-alpha.1") < "1.0.0-alpha.beta");
		QVERIFY(ProjectVersion("1.0.0-alpha.beta") < "1.0.0-beta");
		QVERIFY(ProjectVersion("1.0.0-beta.2") < "1.0.0-beta.11");
		//Test workaround for old, nonstandard version numbers
		QVERIFY(ProjectVersion("1.2.2.42") == "1.2.3-42");
		QVERIFY(ProjectVersion("1.2.2.42") > "1.2.2.21");
		//Ensure that newer versions of the same format aren't upgraded
		//in order to discourage use of incorrect versioning
		QVERIFY(ProjectVersion("1.2.3.42") == "1.2.3");
		//CompareVersion "All" should compare every identifier
		QVERIFY(
			ProjectVersion("1.0.0-a.b.c.d.e.f.g.h.i.j.k.l", ProjectVersion::CompareType::All)
			< "1.0.0-a.b.c.d.e.f.g.h.i.j.k.m"
		);
		//Prerelease identifiers may contain hyphens
		QVERIFY(ProjectVersion("1.0.0-Alpha-1.2") > "1.0.0-Alpha-1.1");
		//We shouldn't crash on invalid versions
		QVERIFY(ProjectVersion("1-invalid") == "1.0.0-invalid");
		QVERIFY(ProjectVersion("") == "0.0.0");
		//Numeric identifiers are smaller than non-numeric identiiers
		QVERIFY(ProjectVersion("1.0.0-alpha") > "1.0.0-1");
		//An identifier of the form "-x" is non-numeric, not negative
		QVERIFY(ProjectVersion("1.0.0-alpha.-1") > "1.0.0-alpha.1");
	}

#ifdef LMMS_HAVE_SESSION_VIEW

	//! (task #594) A <session> block round-trips every ClipSlot and Scene
	//! field without loss, and re-serialising the loaded model is
	//! byte-identical.
	void sessionBlockRoundTrip()
	{
		using namespace lmms;

		SessionModel model;
		model.setTrackCount( 2 );
		model.setSceneCount( 3 );
		model.setGlobalLaunchQuantisation( LaunchQuantisation::TwoBars );

		model.scene( 0 ).setName( "Intro" );
		model.scene( 1 ).setName( "Chorus" );
		model.scene( 1 ).setTempoEnabled( true );
		model.scene( 1 ).setTempo( 128.5 );
		model.scene( 1 ).setTimeSigEnabled( true );
		model.scene( 1 ).setTimeSigNumerator( 3 );
		model.scene( 1 ).setTimeSigDenominator( 8 );

		// MIDI slot: every field at a non-default value.
		ClipSlot& midi = model.slot( 0, 0 );
		midi.setPatternReference( 7 );
		midi.setName( "Bass" );
		midi.setLaunchMode( LaunchMode::Toggle );
		midi.setLaunchQuantisation( LaunchQuantisation::FourBars );
		midi.setLegato( true );
		midi.setLoopStart( 96 );
		midi.setLoopLength( 384 );
		midi.setGainDb( -6.5f );
		midi.setTranspose( -3 );
		midi.setDetune( 12 );
		midi.setRamMode( true );

		FollowAction next;
		next.type = FollowAction::Type::Next;
		next.chance = 0.75;
		next.linked = false;
		next.timeBars = 2.5;
		next.jumpTo = 1;
		midi.addFollowAction( next );

		FollowAction stop;
		stop.type = FollowAction::Type::Stop;
		stop.chance = 0.25;
		midi.addFollowAction( stop );

		// Audio slot: a different reference kind and launch mode.
		ClipSlot& audio = model.slot( 1, 2 );
		audio.setAudioReference( QStringLiteral( "samples/kick.wav" ) );
		audio.setLaunchMode( LaunchMode::Gate );
		audio.setLaunchQuantisation( LaunchQuantisation::None );
		audio.setLoopLength( 192 );

		QDomDocument doc;
		QDomElement root = doc.createElement( QStringLiteral( "song" ) );
		doc.appendChild( root );
		const QDomElement session = model.saveState( doc, root );
		QVERIFY( !session.isNull() );
		QCOMPARE( session.tagName(), QStringLiteral( "session" ) );
		QCOMPARE( session.attribute( QStringLiteral( "version" ) ).toInt(), SessionModel::CurrentVersion );
		QCOMPARE( session.attribute( QStringLiteral( "tracks" ) ).toInt(), 2 );
		QCOMPARE( session.attribute( QStringLiteral( "scenes" ) ).toInt(), 3 );
		QCOMPARE( session.attribute( QStringLiteral( "launchquantisation" ) ).toInt(),
			static_cast<int>( LaunchQuantisation::TwoBars ) );

		// Sparse grid: only the two filled slots are written.
		const QDomElement clips = session.firstChildElement( QStringLiteral( "clips" ) );
		QCOMPARE( clips.elementsByTagName( QStringLiteral( "clip" ) ).length(), 2 );

		const QDomElement midiElement = clips.firstChildElement( QStringLiteral( "clip" ) );
		QCOMPARE( midiElement.attribute( QStringLiteral( "track" ) ).toInt(), 0 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "scene" ) ).toInt(), 0 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "type" ) ).toInt(),
			static_cast<int>( ClipSlot::Type::Midi ) );
		QCOMPARE( midiElement.attribute( QStringLiteral( "pattern" ) ).toInt(), 7 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "name" ) ), QStringLiteral( "Bass" ) );
		QCOMPARE( midiElement.attribute( QStringLiteral( "launchmode" ) ).toInt(),
			static_cast<int>( LaunchMode::Toggle ) );
		QCOMPARE( midiElement.attribute( QStringLiteral( "quantisation" ) ).toInt(),
			static_cast<int>( LaunchQuantisation::FourBars ) );
		QCOMPARE( midiElement.attribute( QStringLiteral( "legato" ) ).toInt(), 1 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "loopstart" ) ).toInt(), 96 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "looplength" ) ).toInt(), 384 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "gain" ) ).toDouble(), -6.5 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "transpose" ) ).toInt(), -3 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "detune" ) ).toInt(), 12 );
		QCOMPARE( midiElement.attribute( QStringLiteral( "ram" ) ).toInt(), 1 );
		QCOMPARE( midiElement.firstChildElement( QStringLiteral( "followactions" ) )
			.elementsByTagName( QStringLiteral( "followaction" ) ).length(), 2 );

		const QString first = nodeToString( session );

		// Load into a fresh model and compare every field.
		SessionModel loaded;
		QVERIFY( loaded.restoreState( session ) );
		QVERIFY( loaded.hadSessionBlock() );
		QCOMPARE( loaded.trackCount(), 2 );
		QCOMPARE( loaded.sceneCount(), 3 );
		QCOMPARE( static_cast<int>( loaded.globalLaunchQuantisation() ),
			static_cast<int>( LaunchQuantisation::TwoBars ) );

		QCOMPARE( loaded.scene( 0 ).name(), QStringLiteral( "Intro" ) );
		QCOMPARE( loaded.scene( 1 ).name(), QStringLiteral( "Chorus" ) );
		QVERIFY( loaded.scene( 1 ).tempoEnabled() );
		QVERIFY( loaded.scene( 1 ).tempo() == 128.5 );
		QVERIFY( loaded.scene( 1 ).timeSigEnabled() );
		QCOMPARE( loaded.scene( 1 ).timeSigNumerator(), 3 );
		QCOMPARE( loaded.scene( 1 ).timeSigDenominator(), 8 );
		QVERIFY( !loaded.scene( 2 ).isModified() );

		const ClipSlot& loadedMidi = loaded.slot( 0, 0 );
		QCOMPARE( static_cast<int>( loadedMidi.type() ), static_cast<int>( ClipSlot::Type::Midi ) );
		QCOMPARE( loadedMidi.patternId(), 7 );
		QCOMPARE( loadedMidi.name(), QStringLiteral( "Bass" ) );
		QCOMPARE( static_cast<int>( loadedMidi.launchMode() ), static_cast<int>( LaunchMode::Toggle ) );
		QCOMPARE( static_cast<int>( loadedMidi.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::FourBars ) );
		QCOMPARE( loadedMidi.legato(), true );
		QCOMPARE( loadedMidi.loopStart(), 96 );
		QCOMPARE( loadedMidi.loopLength(), 384 );
		QVERIFY( loadedMidi.gainDb() == -6.5f );
		QCOMPARE( loadedMidi.transpose(), -3 );
		QCOMPARE( loadedMidi.detune(), 12 );
		QCOMPARE( loadedMidi.ramMode(), true );
		QCOMPARE( loadedMidi.followActions().size(), std::size_t( 2 ) );
		QVERIFY( loadedMidi.followActions()[0] == next );
		QVERIFY( loadedMidi.followActions()[1] == stop );

		const ClipSlot& loadedAudio = loaded.slot( 1, 2 );
		QCOMPARE( static_cast<int>( loadedAudio.type() ), static_cast<int>( ClipSlot::Type::Audio ) );
		QCOMPARE( loadedAudio.audioSource(), QStringLiteral( "samples/kick.wav" ) );
		QCOMPARE( loadedAudio.patternId(), -1 );
		QCOMPARE( static_cast<int>( loadedAudio.launchMode() ), static_cast<int>( LaunchMode::Gate ) );
		QCOMPARE( static_cast<int>( loadedAudio.launchQuantisation() ),
			static_cast<int>( LaunchQuantisation::None ) );
		QCOMPARE( loadedAudio.legato(), false );
		QCOMPARE( loadedAudio.loopLength(), 192 );
		QVERIFY( loaded.slot( 1, 0 ).isEmpty() );

		// Re-save the loaded model: identical bytes.
		QDomDocument doc2;
		QDomElement root2 = doc2.createElement( QStringLiteral( "song" ) );
		doc2.appendChild( root2 );
		const QDomElement session2 = loaded.saveState( doc2, root2 );
		QCOMPARE( nodeToString( session2 ), first );

		// An untouched model writes nothing.
		QVERIFY( SessionModel().isEmpty() );
		QVERIFY( !SessionModel().shouldPersist() );
		QVERIFY( model.shouldPersist() );
	}

	//! (task #594) A project saved before the session view existed carries no
	//! <session> block; loading and re-saving it is byte-identical.
	void projectWithoutSessionRoundTripsUnchanged()
	{
		using namespace lmms;

		DataFile fresh( DataFile::Type::SongProject );
		const QString version = fresh.documentElement().attribute( QStringLiteral( "version" ) );
		QVERIFY( !version.isEmpty() );

		const QString project = QString(
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\">\n"
			"  <head/>\n"
			"  <song>\n"
			"    <trackcontainer type=\"song\">\n"
			"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
			"        <instrumenttrack/>\n"
			"        <pattern type=\"0\" name=\"p\" muted=\"0\" steps=\"16\" pos=\"0\" len=\"192\" frozen=\"0\">\n"
			"          <midiclip steps=\"16\" name=\"c\" muted=\"0\" pos=\"0\" len=\"192\" type=\"0\">\n"
			"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\"/>\n"
			"          </midiclip>\n"
			"        </pattern>\n"
			"      </track>\n"
			"    </trackcontainer>\n"
			"  </song>\n"
			"</lmms-project>\n" ).arg( version );

		DataFile old( project.toUtf8() );
		QVERIFY( old.type() == DataFile::Type::SongProject );
		QVERIFY( !old.content().isNull() );

		const QString first = fileToString( old );

		// No session state is invented for a pre-session project.
		QVERIFY( !first.contains( QStringLiteral( "session" ) ) );
		QVERIFY( old.content().firstChildElement( QStringLiteral( "session" ) ).isNull() );

		// Byte-identical re-save.
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( fileToString( roundTrip ), first );

		// The loader leaves the model empty and produces no block.
		SessionModel model;
		QVERIFY( !model.hadSessionBlock() );
		QVERIFY( model.isEmpty() );
		QVERIFY( !model.shouldPersist() );
	}

	//! (task #594) A <session> block written by a newer build is ignored, not
	//! guessed at, and preserved verbatim so re-saving cannot drop it.
	void unknownFutureSessionVersionIsIgnoredAndPreserved()
	{
		using namespace lmms;

		DataFile fresh( DataFile::Type::SongProject );
		const QString version = fresh.documentElement().attribute( QStringLiteral( "version" ) );

		const QString project = QString(
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\">\n"
			"  <head/>\n"
			"  <song>\n"
			"    <trackcontainer type=\"song\"/>\n"
			"    <session version=\"99\" tracks=\"1\" scenes=\"1\">\n"
			"      <clips><clip track=\"0\" scene=\"0\" type=\"1\" pattern=\"4\"/></clips>\n"
			"    </session>\n"
			"  </song>\n"
			"</lmms-project>\n" ).arg( version );

		DataFile data( project.toUtf8() );
		const QDomElement sessionElement = data.content().firstChildElement( QStringLiteral( "session" ) );
		QVERIFY( !sessionElement.isNull() );
		QCOMPARE( sessionElement.attribute( QStringLiteral( "version" ) ).toInt(), 99 );

		SessionModel model;
		QVERIFY( !model.restoreState( sessionElement ) );
		QVERIFY( model.hadSessionBlock() );
		QVERIFY( model.shouldPersist() );
		QVERIFY( !model.preservedUnknownXml().isEmpty() );
		// Nothing was interpreted.
		QCOMPARE( model.trackCount(), 0 );
		QCOMPARE( model.sceneCount(), 0 );
		QVERIFY( model.slot( 0, 0 ).isEmpty() );

		// The opaque block survives a save through this build.
		QDomDocument doc;
		QDomElement root = doc.createElement( QStringLiteral( "song" ) );
		doc.appendChild( root );
		const QDomElement saved = model.saveState( doc, root );
		QCOMPARE( saved.attribute( QStringLiteral( "version" ) ).toInt(), 99 );
		QCOMPARE( saved.attribute( QStringLiteral( "tracks" ) ).toInt(), 1 );
		const QDomElement savedClip = saved.firstChildElement( QStringLiteral( "clips" ) )
			.firstChildElement( QStringLiteral( "clip" ) );
		QVERIFY( !savedClip.isNull() );
		QCOMPARE( savedClip.attribute( QStringLiteral( "pattern" ) ).toInt(), 4 );

		// A block from this build's own version is parsed, not preserved.
		QDomDocument doc2;
		QDomElement root2 = doc2.createElement( QStringLiteral( "song" ) );
		doc2.appendChild( root2 );
		SessionModel own;
		own.setTrackCount( 1 );
		own.setSceneCount( 1 );
		own.slot( 0, 0 ).setPatternReference( 4 );
		const QDomElement ownElement = own.saveState( doc2, root2 );
		SessionModel reparsed;
		QVERIFY( reparsed.restoreState( ownElement ) );
		QVERIFY( reparsed.preservedUnknownXml().isEmpty() );
		QCOMPARE( reparsed.slot( 0, 0 ).patternId(), 4 );
	}

#endif // LMMS_HAVE_SESSION_VIEW
};

QTEST_GUILESS_MAIN(ProjectVersionTest)
#include "ProjectVersionTest.moc"
