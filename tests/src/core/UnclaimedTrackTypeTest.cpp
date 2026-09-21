/*
 * UnclaimedTrackTypeTest.cpp - the <trackcontainer> children no build here can
 *                             construct: a <track> whose `type` has no class,
 *                             and an element that is not a track at all.
 *                             Neither is dropped and neither is coerced.
 *                             SPEC-ARCH-4 1.6.2. ARCH-4 S1c.
 *                             The last slot is the LOAD REPORT's half
 *                             (SPEC-ARCH-4 1.6.4, ARCH-4 S1d): the same paths,
 *                             answered by project.open instead of re-derived.
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
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
#include "Song.h"
#include "TrackContainer.h"
#include "UnclaimedElements.h"

using namespace lmms;

namespace
{

bool writeText( const QString& path, const QString& text )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return false;
	}
	return file.write( text.toUtf8() ) >= 0;
}


QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QString();
	}
	return QString::fromUtf8( file.readAll() );
}


//! A project file the test just wrote, as a document. Assertions are made on
//! THIS and not on substrings of the text, because QDom does not promise an
//! attribute ORDER across a parse: a substring test on `<track type="9"` would
//! pin the serialiser's ordering rather than the preservation.
QDomDocument parse( const QString& text )
{
	QDomDocument document;
	document.setContent( text, false );
	return document;
}


//! How the project writer serialises one element - the canonical form two saves
//! of the same document are compared in (SPEC-ARCH-4 S0's correction: the draft
//! asked for byte identity, which a re-indented re-emitted block cannot give).
QString serialise( const QDomElement& element )
{
	QString out;
	QTextStream stream( &out );
	element.save( stream, 2 );
	stream.flush();
	return out;
}


//! The <trackcontainer> of a saved project: the file's own <song> child.
QDomElement containerOf( const QDomDocument& document )
{
	return document.documentElement().firstChildElement( QStringLiteral( "song" ) )
		.firstChildElement( QStringLiteral( "trackcontainer" ) );
}


//! The child element of \a parent whose \a attribute equals \a value, or a null
//! element. By attribute, so the lookup does not depend on position.
QDomElement childWith( const QDomElement& parent, const QString& attribute, const QString& value )
{
	for( QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement() )
	{
		if( e.attribute( attribute ) == value ) { return e; }
	}
	return QDomElement();
}


//! A failure message that shows what the report actually named, so a failed slot
//! says what it got instead of only what it wanted.
QString listed( const QStringList& names )
{
	QString out = QStringLiteral( "[" );
	for( const QString& name : names )
	{
		if( out.size() > 1 ) { out += QStringLiteral( ", " ); }
		out += name;
	}
	return out + QStringLiteral( "]" );
}


//! A song project whose <trackcontainer> holds one ordinary instrument track -
//! a row this build CAN construct, and therefore the control for every slot
//! here - followed by exactly \a containerChildren. \a containerChildren is
//! where the elements under test are written.
QString projectWithContainerChildren( const QString& containerChildren )
{
	DataFile fresh( DataFile::Type::SongProject );
	const QString version = fresh.documentElement().attribute( QStringLiteral( "version" ) );
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"known\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"%2"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n" ).arg( version, containerChildren );
}


//! Load \a project into the engine's song from a known-empty state, and hand it
//! back. Clearing and disabling load-on-launch first is what makes every slot
//! here a statement about THIS project rather than about the default one.
Song* loadFresh( const QString& project )
{
	Song* song = Engine::getSong();
	song->clearProject();
	song->setLoadOnLaunch( false );
	song->loadProject( project );
	return song;
}


//! How many tracks the song holds. The refused rows are deliberately NOT among
//! them: what this slice changes is what the load KEEPS, not what it models.
int trackCount( const Song* song )
{
	return static_cast<int>( song->tracks().size() );
}

} // namespace


class UnclaimedTrackTypeTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init( true );
		// The registry only dispatches a handler once the application says the
		// model is fully up - until then every command gets the typed 'busy'
		// refusal with the reason code `engine_starting` (ControlSession.cpp).
		// The last slot drives project.open through ControlRegistry, so this
		// test sets that flag itself, exactly as ControlRegistryTest does.
		ControlRegistry::setReady( true );
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady( false );
		Engine::destroy();
	}

	//! The slice's owed proof (SPEC-ARCH-4 1.6.2): a `<track>` whose `type` this
	//! build has no class for survives a load -> save round trip, is reported,
	//! and is not counted as a track. Before this slice `Track::create()`
	//! returned nullptr for it, `TrackContainer::loadSettings()` discarded that
	//! nullptr, and the row was gone from the file the next save wrote.
	void anUnknownTrackTypeIsPreservedNotDropped()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "future-type.mmp" ) );
		QVERIFY( writeText( project, projectWithContainerChildren(
			"      <track type=\"9\" name=\"future\">\n"
			"        <futuretrack gain=\"2\"/>\n"
			"      </track>\n" ) ) );

		Song* song = loadFresh( project );

		// The factory refused it, so the container holds the one track it could
		// build. Counted separately from "did it survive": a build that kept the
		// row by fabricating a track would pass a survival check and fail this.
		QCOMPARE( trackCount( song ), 1 );

		// The load says so: the path is the child index the re-emitted element
		// will occupy (one track written, so this one is child 1).
		const QStringList reported = song->unclaimedElements();
		QVERIFY2( reported.contains( QStringLiteral( "/song/trackcontainer/track[1]" ) ),
			qPrintable( QStringLiteral( "the unknown track type was not reported: " )
				+ listed( reported ) ) );

		// The container's own list, which is what the save re-emits.
		QCOMPARE( static_cast<int>( song->unclaimedChildren().size() ), 1 );

		const QString firstSave = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( firstSave ) );
		const QDomElement firstContainer = containerOf( parse( readText( firstSave ) ) );
		QVERIFY2( !firstContainer.isNull(), "the saved project has no <trackcontainer>" );

		// Exactly once: preserving must not accumulate a copy per open, and must
		// not write the row twice beside the track the writer emits itself.
		QCOMPARE( firstContainer.elementsByTagName( QStringLiteral( "track" ) ).count(), 2 );

		const QDomElement refused =
			childWith( firstContainer, QStringLiteral( "name" ), QStringLiteral( "future" ) );
		QVERIFY2( !refused.isNull(),
			"the unknown track type was dropped by a load -> save round trip" );
		QCOMPARE( refused.tagName(), QStringLiteral( "track" ) );
		QCOMPARE( refused.attribute( QStringLiteral( "type" ) ), QStringLiteral( "9" ) );

		// Verbatim means the element AND its own subtree.
		const QDomElement payload = refused.firstChildElement();
		QVERIFY2( !payload.isNull(), "the unknown track's own child was not carried with it" );
		QCOMPARE( payload.tagName(), QStringLiteral( "futuretrack" ) );
		QCOMPARE( payload.attribute( QStringLiteral( "gain" ) ), QStringLiteral( "2" ) );

		// ...and it is stable: opening and saving the result again changes
		// neither the count, nor the report, nor the block itself.
		song->loadProject( firstSave );
		QCOMPARE( trackCount( song ), 1 );
		QCOMPARE( song->unclaimedElements(), reported );

		const QString secondSave = dir.filePath( QStringLiteral( "round2.mmp" ) );
		QVERIFY( song->saveProjectFile( secondSave ) );
		const QDomElement secondContainer = containerOf( parse( readText( secondSave ) ) );
		QCOMPARE( secondContainer.elementsByTagName( QStringLiteral( "track" ) ).count(), 2 );
		const QDomElement again =
			childWith( secondContainer, QStringLiteral( "name" ), QStringLiteral( "future" ) );
		QVERIFY( !again.isNull() );
		QCOMPARE( serialise( again ), serialise( refused ) );
	}

	//! The sibling hole, and the reason the claim test is not simply "did the
	//! factory refuse it": `Track::create()` reads `type` with
	//! QVariant::toInt(), so an element with NO `type` attribute reads as 0 - an
	//! Instrument track. Without the name test, any element a newer writer put
	//! inside <trackcontainer> would be silently fabricated into an instrument
	//! track and its contents read as clips, which is the trap
	//! SPEC-stable-ids.md 3.1 records for <track>'s own children.
	//!
	//! This slot is ALSO the witness for the loader hang this slice found, and
	//! that is why it is written this way. `Song::loadProject`'s own pre-count
	//! walk advanced its cursor INSIDE its `nodeName() == "track"` branch, so
	//! the first <trackcontainer> child that was not a <track> made the walk
	//! loop for ever: this fixture, loaded before the one-line fix, spun at 100%
	//! CPU and never reached any assertion. The slot cannot assert "it did not
	//! hang"; it completes, and that completion is the witness.
	void anElementThatIsNotATrackIsNotCoercedIntoOne()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "not-a-track.mmp" ) );
		QVERIFY( writeText( project, projectWithContainerChildren(
			"      <lanes kind=\"bus\">\n"
			"        <lane index=\"0\"/>\n"
			"      </lanes>\n" ) ) );

		Song* song = loadFresh( project );

		QCOMPARE( trackCount( song ), 1 );

		const QStringList reported = song->unclaimedElements();
		QVERIFY2( reported.contains( QStringLiteral( "/song/trackcontainer/lanes[1]" ) ),
			qPrintable( QStringLiteral( "the non-track element was not reported: " )
				+ listed( reported ) ) );

		const QString save = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( save ) );
		const QDomElement container = containerOf( parse( readText( save ) ) );

		// It did not become a track...
		QCOMPARE( container.elementsByTagName( QStringLiteral( "track" ) ).count(), 1 );

		// ...and it is still itself, once, with its own subtree.
		QCOMPARE( container.elementsByTagName( QStringLiteral( "lanes" ) ).count(), 1 );
		const QDomElement lanes = container.firstChildElement( QStringLiteral( "lanes" ) );
		QVERIFY2( !lanes.isNull(),
			"the non-track element was dropped by a load -> save round trip" );
		QCOMPARE( lanes.attribute( QStringLiteral( "kind" ) ), QStringLiteral( "bus" ) );
		const QDomElement lane = lanes.firstChildElement( QStringLiteral( "lane" ) );
		QVERIFY2( !lane.isNull(), "the non-track element's own child was not carried with it" );
		QCOMPARE( lane.attribute( QStringLiteral( "index" ) ), QStringLiteral( "0" ) );
	}

	//! The negative control: the predicate is "this build could not construct a
	//! track from it", not "the type number is unusual". A FOLDER track (type 7)
	//! has a class here and must be claimed and counted - and an ordinary
	//! project of known types must produce an EMPTY report, or preservation and
	//! the report would both be noise.
	void aTrackTypeThisBuildCanBuildIsClaimedAndNotReported()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		// Folder is the highest enumerator with a class (include/Track.h); the
		// comment there records that its number is appended, never inserted, so
		// 7 is the value an older build drops - the case this slice is about,
		// and the case this build must still take.
		const QString project = dir.filePath( QStringLiteral( "known-types.mmp" ) );
		QVERIFY( writeText( project, projectWithContainerChildren(
			"      <track type=\"7\" name=\"folder\">\n"
			"        <trackfolder/>\n"
			"      </track>\n" ) ) );

		Song* song = loadFresh( project );

		QCOMPARE( trackCount( song ), 2 );
		QVERIFY2( song->unclaimedChildren().isEmpty(),
			qPrintable( QStringLiteral( "a track this build can build was kept as unclaimed: " )
				+ listed( song->unclaimedElements() ) ) );
		QVERIFY2( song->unclaimedElements().isEmpty(),
			qPrintable( QStringLiteral( "a project of known types reported unclaimed elements: " )
				+ listed( song->unclaimedElements() ) ) );

		const QString save = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( save ) );
		const QDomElement container = containerOf( parse( readText( save ) ) );
		QCOMPARE( container.elementsByTagName( QStringLiteral( "track" ) ).count(), 2 );
		QVERIFY( !childWith( container, QStringLiteral( "name" ), QStringLiteral( "folder" ) ).isNull() );
	}

	//! SPEC-ARCH-4 1.6.4 (ARCH-4 S1d): the load REPORT. What the loader kept is
	//! answered by `project.open` beside `ids_assigned`, so a caller learns what
	//! a load preserved instead of discovering it on the next save. The paths
	//! asserted here are the SAME ones the two slots above assert on the saved
	//! DOM: the report composes them, it does not invent them.
	void projectOpenReportsWhatTheLoadPreserved()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		// Both refusals S1c added, in one document, in document order: a <track>
		// whose `type` has no class here, then an element that is not a track.
		const QString project = dir.filePath( QStringLiteral( "report.mmp" ) );
		QVERIFY( writeText( project, projectWithContainerChildren(
			"      <track type=\"9\" name=\"future\">\n"
			"        <futuretrack gain=\"2\"/>\n"
			"      </track>\n"
			"      <lanes kind=\"bus\">\n"
			"        <lane index=\"0\"/>\n"
			"      </lanes>\n" ) ) );

		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult opened = registry->invoke( QStringLiteral( "project.open" ),
			QJsonObject{{ QStringLiteral( "path" ), project }} );
		QVERIFY2( opened.ok, qPrintable( opened.errorMessage ) );

		const QJsonArray unclaimed =
			opened.result.value( QStringLiteral( "unclaimed" ) ).toArray();
		// The count is the array's own length rather than a second estimate, in
		// the same `count`/`<name>_count` shape as the load errors beside it.
		QCOMPARE( opened.result.value( QStringLiteral( "unclaimed_count" ) ).toInt(),
			unclaimed.size() );
		QCOMPARE( unclaimed.size(), 2 );

		// DOCUMENT order, and that is the assertion: sorting these the way the
		// error list beside them is sorted would put `lanes` first. The index in
		// a path is the child number the re-emitted element occupies, so it is
		// the track count (1: only the known track was built) plus the refused
		// element's own position among the refusals.
		QCOMPARE( unclaimed.at( 0 ).toString(),
			QStringLiteral( "/song/trackcontainer/track[1]" ) );
		QCOMPARE( unclaimed.at( 1 ).toString(),
			QStringLiteral( "/song/trackcontainer/lanes[2]" ) );

		// The report describes THIS load: a project of known types answers with
		// an empty list, so a report that was always non-empty would be saying
		// nothing about the load at all.
		const QString known = dir.filePath( QStringLiteral( "known.mmp" ) );
		QVERIFY( writeText( known, projectWithContainerChildren(
			"      <track type=\"7\" name=\"folder\">\n"
			"        <trackfolder/>\n"
			"      </track>\n" ) ) );
		const ControlResult clean = registry->invoke( QStringLiteral( "project.open" ),
			QJsonObject{{ QStringLiteral( "path" ), known }} );
		QVERIFY2( clean.ok, qPrintable( clean.errorMessage ) );

		const QJsonArray cleanUnclaimed =
			clean.result.value( QStringLiteral( "unclaimed" ) ).toArray();
		QStringList cleanNames;
		for( const QJsonValue& name : cleanUnclaimed ) { cleanNames.append( name.toString() ); }
		QVERIFY2( cleanUnclaimed.isEmpty(),
			qPrintable( QStringLiteral( "a project of known types reported unclaimed elements: " )
				+ listed( cleanNames ) ) );
		QCOMPARE( clean.result.value( QStringLiteral( "unclaimed_count" ) ).toInt(), 0 );
	}
};

QTEST_GUILESS_MAIN( UnclaimedTrackTypeTest )
#include "UnclaimedTrackTypeTest.moc"
