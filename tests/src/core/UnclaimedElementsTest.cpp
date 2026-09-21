/*
 * UnclaimedElementsTest.cpp - the elements a loader did not claim: the
 *                             capture/re-emit mechanism, and the two load walks
 *                             that use it (SPEC-ARCH-4 1.6.1 / 1.6.3). ARCH-4 S1b.
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
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>

#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MidiClip.h"
#include "Song.h"
#include "Track.h"
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


//! How the project writer serialises one element - what the test means by
//! "kept verbatim", expressed as the string that reaches the file.
QString serialise( const QDomElement& element )
{
	QString out;
	QTextStream stream( &out );
	element.save( stream, 2 );
	stream.flush();
	return out;
}


//! The named child of a document's root element, or a null element.
QDomElement child( const QDomDocument& document, const QString& name )
{
	return document.documentElement().firstChildElement( name );
}


//! A song project with the minimal shape a project needs to load. \a trackExtras
//! lands inside its single track and \a songExtras inside <song> after the track
//! container - the two positions an unclaimed element is read from.
QString projectWith( const QString& trackExtras, const QString& songExtras )
{
	DataFile fresh( DataFile::Type::SongProject );
	const QString version = fresh.documentElement().attribute( QStringLiteral( "version" ) );
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"%2"
		"      </track>\n"
		"    </trackcontainer>\n"
		"%3"
		"  </song>\n"
		"</lmms-project>\n" ).arg( version, trackExtras, songExtras );
}


//! The `<name ...>...</name>` substring of a project file, for comparing one save
//! against the next.
QString sectionText( const QString& project, const QString& name )
{
	const int start = project.indexOf( QStringLiteral( "<" ) + name );
	const int end = project.indexOf( QStringLiteral( "</" ) + name + QStringLiteral( ">" ) );
	if( start < 0 || end < 0 ) { return QString(); }
	return project.mid( start, end - start + name.size() + 3 );
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


//! How many clips the song's first track holds - what a load walk made of the
//! clip elements inside one <track>. Guarded because Track::getClip() CREATES a
//! clip when asked for one that does not exist.
int clipCountOfFirstTrack()
{
	const auto& tracks = Engine::getSong()->tracks();
	return tracks.empty() ? 0 : tracks.front()->numOfClips();
}


//! The first clip of the song's first track, or nullptr when it has none.
Clip* firstClipOfFirstTrack()
{
	const auto& tracks = Engine::getSong()->tracks();
	if( tracks.empty() || tracks.front()->numOfClips() < 1 ) { return nullptr; }
	return tracks.front()->getClip( 0 );
}

} // namespace


class UnclaimedElementsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init( true ); }

	void cleanupTestCase() { Engine::destroy(); }

	// -----------------------------------------------------------------------
	// The mechanism (SPEC-ARCH-4 1.6.1)
	// -----------------------------------------------------------------------

	//! "Kept verbatim": what is re-emitted is the serialisation of the element
	//! that was read - its attributes and its whole subtree.
	void aCapturedElementIsReemittedVerbatim()
	{
		QDomDocument source;
		QVERIFY( source.setContent( QStringLiteral(
			"<song><unknownSection foo=\"1\"><inner bar=\"2\"/></unknownSection></song>" ) ) );

		QVector<UnclaimedElement> captured;
		captureUnclaimed( child( source, QStringLiteral( "unknownSection" ) ),
			QStringLiteral( "unknownSection" ), captured );

		QCOMPARE( captured.size(), 1 );
		QCOMPARE( captured.at( 0 ).key, QStringLiteral( "unknownSection" ) );
		QCOMPARE( captured.at( 0 ).xml,
			serialise( child( source, QStringLiteral( "unknownSection" ) ) ) );

		QDomDocument target;
		QDomElement root = target.createElement( QStringLiteral( "song" ) );
		target.appendChild( root );
		QVERIFY( reemitUnclaimed( captured, target, root ) );

		const QDomElement reemitted = child( target, QStringLiteral( "unknownSection" ) );
		QVERIFY2( !reemitted.isNull(), "the captured section was not re-emitted" );
		QCOMPARE( reemitted.attribute( QStringLiteral( "foo" ) ), QStringLiteral( "1" ) );
		QCOMPARE( reemitted.firstChildElement( QStringLiteral( "inner" ) )
			.attribute( QStringLiteral( "bar" ) ), QStringLiteral( "2" ) );
	}

	//! Two elements sharing a name are BOTH kept, in document order: a capture
	//! appends and is never keyed by name, so nothing can be overwritten.
	void captureKeepsBothElementsThatShareAName()
	{
		QDomDocument source;
		QVERIFY( source.setContent( QStringLiteral(
			"<song><lane n=\"1\"/><lane n=\"2\"/></song>" ) ) );

		QVector<UnclaimedElement> captured;
		for( QDomNode node = source.documentElement().firstChild();
			!node.isNull(); node = node.nextSibling() )
		{
			if( node.isElement() )
			{
				captureUnclaimed( node.toElement(), node.nodeName(), captured );
			}
		}
		QCOMPARE( captured.size(), 2 );

		QDomDocument target;
		QDomElement root = target.createElement( QStringLiteral( "song" ) );
		target.appendChild( root );
		QVERIFY( reemitUnclaimed( captured, target, root ) );

		const QDomNodeList lanes = root.elementsByTagName( QStringLiteral( "lane" ) );
		QCOMPARE( lanes.count(), 2 );
		QCOMPARE( lanes.at( 0 ).toElement().attribute( QStringLiteral( "n" ) ),
			QStringLiteral( "1" ) );
		QCOMPARE( lanes.at( 1 ).toElement().attribute( QStringLiteral( "n" ) ),
			QStringLiteral( "2" ) );
	}

	//! The two no-ops: a null element captures nothing, and an empty list
	//! re-emits nothing (which is what lets a caller treat "false" as "there is
	//! no preserved block to write").
	void aNullElementCapturesNothingAndAnEmptyListReemitsNothing()
	{
		QVector<UnclaimedElement> captured;
		captureUnclaimed( QDomElement(), QStringLiteral( "missing" ), captured );
		QCOMPARE( captured.size(), 0 );

		QDomDocument target;
		QDomElement root = target.createElement( QStringLiteral( "song" ) );
		target.appendChild( root );
		QVERIFY( !reemitUnclaimed( captured, target, root ) );
		QCOMPARE( root.childNodes().count(), 0 );
	}

	//! A malformed entry is skipped, the sound entries still come through, and
	//! nothing crashes: captureUnclaimed() cannot produce such an entry, but a
	//! list corrupted after capture must not cost the whole project its save.
	void aMalformedEntryIsSkippedAndTheRestStillReemits()
	{
		QVector<UnclaimedElement> entries;
		entries.append( UnclaimedElement{ QStringLiteral( "good" ),
			QStringLiteral( "<good a=\"1\"/>" ) } );
		entries.append( UnclaimedElement{ QStringLiteral( "truncated" ),
			QStringLiteral( "<truncated a=\"1\"" ) } );
		entries.append( UnclaimedElement{ QStringLiteral( "alsoGood" ),
			QStringLiteral( "<alsoGood b=\"2\"/>" ) } );

		QDomDocument target;
		QDomElement root = target.createElement( QStringLiteral( "song" ) );
		target.appendChild( root );
		QVERIFY( reemitUnclaimed( entries, target, root ) );

		QCOMPARE( root.childNodes().count(), 2 );
		QVERIFY2( !child( target, QStringLiteral( "good" ) ).isNull(),
			"a sound entry was lost because another entry was malformed" );
		QVERIFY2( !child( target, QStringLiteral( "alsoGood" ) ).isNull(),
			"an entry after the malformed one was lost with it" );
		QVERIFY( child( target, QStringLiteral( "truncated" ) ).isNull() );
	}

	// -----------------------------------------------------------------------
	// The two walks (SPEC-ARCH-4 1.6.1 / 1.6.3)
	// -----------------------------------------------------------------------

	//! The owed proof: a fixture with an unknown SECTION and an unknown TRACK
	//! CHILD loads, re-saves, and both survive at their own paths - and the load
	//! report names them. Before this slice the section was dropped outright and
	//! the child was coerced into a phantom Clip (1.6.3), so a load -> save
	//! through this build destroyed both.
	void anUnknownSectionAndAnUnknownChildSurviveARoundTrip()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "unknown.mmp" ) );
		QVERIFY( writeText( project, projectWith(
			"        <futureChild mode=\"mystery\"/>\n",
			"    <futureSection invented=\"yes\"><child/></futureSection>\n" ) ) );

		Song* song = loadFresh( project );

		const QStringList reported = song->unclaimedElements();
		QVERIFY2( reported.contains( QStringLiteral( "/song/futureSection" ) ),
			qPrintable( QStringLiteral( "the unknown section was not reported: " )
				+ listed( reported ) ) );
		QVERIFY2( reported.contains( QStringLiteral( "/song/trackcontainer/track[0]/futureChild" ) ),
			qPrintable( QStringLiteral( "the unknown track child was not reported: " )
				+ listed( reported ) ) );

		const QString firstSave = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( firstSave ) );
		const QString firstText = readText( firstSave );

		QVERIFY2( firstText.contains( QStringLiteral( "invented=\"yes\"" ) ),
			"the unknown section was dropped by a load -> save round trip" );
		QVERIFY2( firstText.contains( QStringLiteral( "<child/>" ) ),
			"the unknown section's own children were not carried with it" );
		QVERIFY2( firstText.contains( QStringLiteral( "mode=\"mystery\"" ) ),
			"the unknown track child was dropped by a load -> save round trip" );

		// Exactly once: preserving must not accumulate a copy per open.
		QCOMPARE( firstText.count( QStringLiteral( "<futureSection" ) ), 1 );
		QCOMPARE( firstText.count( QStringLiteral( "<futureChild" ) ), 1 );

		// ...and it is stable: opening and saving the result again changes
		// neither block.
		song->loadProject( firstSave );
		const QString secondSave = dir.filePath( QStringLiteral( "round2.mmp" ) );
		QVERIFY( song->saveProjectFile( secondSave ) );
		const QString secondText = readText( secondSave );
		QCOMPARE( secondText.count( QStringLiteral( "<futureSection" ) ), 1 );
		QCOMPARE( secondText.count( QStringLiteral( "<futureChild" ) ), 1 );
		QCOMPARE( sectionText( secondText, QStringLiteral( "futureSection" ) ),
			sectionText( firstText, QStringLiteral( "futureSection" ) ) );
	}

	//! The negative control: the predicate is "no reader claimed it", not "every
	//! name". The section and the child this build DOES read must not be
	//! reported, or preservation and the report would both be noise.
	void knownSectionsAndChildrenAreNotReported()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		// <instrumenttrack/> is this track type's own child (Track::loadTrack
		// routes it to loadTrackSpecificSettings) and <controllers/> is a
		// section the walk restores - one of each, both claimed.
		const QString project = dir.filePath( QStringLiteral( "known.mmp" ) );
		QVERIFY( writeText( project, projectWith( "", "    <controllers/>\n" ) ) );

		Song* song = loadFresh( project );

		QVERIFY2( song->unclaimedElements().isEmpty(),
			qPrintable( QStringLiteral( "a claimed element was reported as unclaimed: " )
				+ listed( song->unclaimedElements() ) ) );

		// RESET ON ABSENCE, the rule every other project-scoped element in this
		// walk follows: the next project must not inherit the last one's report.
		song->loadProject( project );
		QVERIFY( song->unclaimedElements().isEmpty() );
	}

	//! A build with no GUI has no reader for the five window-state sections the
	//! writer puts in <song>, so the rule that keeps a <session> block keeps
	//! them: the gap is this build's capability, not the document's, and a load
	//! -> save here must not be what deletes a user's window state. With a GUI
	//! the five are claimed and never enter the report, so the premise is
	//! asserted rather than assumed.
	void theWindowSectionsAGuiLessBuildCannotReadArePreservedNotDropped()
	{
		QVERIFY2( gui::getGUI() == nullptr,
			"this slot pins what a build with NO GUI does, and this harness gave it one" );

		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		// The element name is the tree's own literal for the piano-roll window
		// state (tests/emptyproject.mmp:269; the same five names
		// NamespaceRegistryTest checks against the headers).
		const QString project = dir.filePath( QStringLiteral( "with-gui.mmp" ) );
		QVERIFY( writeText( project, projectWith( "",
			"    <pianoroll width=\"840\" x=\"-11\" y=\"0\" maximized=\"0\""
			" height=\"480\" visible=\"0\" minimized=\"0\"/>\n" ) ) );

		Song* song = loadFresh( project );

		QVERIFY2( song->unclaimedElements().contains( QStringLiteral( "/song/pianoroll" ) ),
			qPrintable( QStringLiteral( "a GUI-less build did not report the window state it "
				"cannot read: " ) + listed( song->unclaimedElements() ) ) );

		const QString save = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( save ) );
		const QString text = readText( save );
		QVERIFY2( text.contains( QStringLiteral( "visible=\"0\"" ) ),
			"opening and saving in a GUI-less build dropped the piano-roll window state" );
		QCOMPARE( text.count( QStringLiteral( "<pianoroll" ) ), 1 );
	}

	// -----------------------------------------------------------------------
	// The same branch, the other half of its job (1.6.3)
	// -----------------------------------------------------------------------

	//! The regression this slice shipped and then caught, pinned so it cannot
	//! come back: the branch that must now PRESERVE an unknown child is also the
	//! CLIP LOADER, so a clip element has to keep becoming a real Clip. Measured
	//! before this slot existed: every clip element was captured instead, no clip
	//! was created anywhere, TrackContainer::isEmpty() answered true for every
	//! project, and 25 test binaries went red - while this file stayed green,
	//! because its fixtures had no clip in them.
	void aClipElementIsStillLoadedAsARealClip()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "clip.mmp" ) );
		QVERIFY( writeText( project, projectWith(
			"        <midiclip steps=\"16\" name=\"c\" muted=\"0\" pos=\"0\" len=\"192\""
			" type=\"0\">\n"
			"          <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\""
			" type=\"0\"/>\n"
			"        </midiclip>\n",
			"" ) ) );

		Song* song = loadFresh( project );

		QVERIFY2( song->unclaimedElements().isEmpty(),
			qPrintable( QStringLiteral( "a clip element was preserved instead of loaded: " )
				+ listed( song->unclaimedElements() ) ) );
		QCOMPARE( clipCountOfFirstTrack(), 1 );

		// ...and the clip's OWN data came with the element, not just the element:
		// this is what separates "loaded as a clip" from "kept as a blob".
		auto* clip = dynamic_cast<MidiClip*>( firstClipOfFirstTrack() );
		QVERIFY2( clip != nullptr, "the <midiclip> element did not become a MidiClip" );
		QCOMPARE( static_cast<int>( clip->notes().size() ), 1 );
		QCOMPARE( clip->notes().at( 0 )->key(), 60 );
		QCOMPARE( clip->length().getTicks(), static_cast<tick_t>( 192 ) );
	}

	//! The four spellings DataFile::upgrade_bbTcoRename renames the clip elements
	//! FROM are still clip elements. A document can declare a current `version`
	//! and still carry a pre-rename tag - the tree's own legacy-shaped fixtures do
	//! exactly that, and one of them is what a <midiclip> is nested inside - so
	//! the walk has to keep loading them: nothing that loaded before this slice
	//! may stop loading because of it.
	void thePreRenameClipSpellingsStillLoadAsClips()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "prerename.mmp" ) );
		QVERIFY( writeText( project, projectWith(
			"        <pattern pos=\"0\" len=\"192\"/>\n"
			"        <sampletco pos=\"0\" len=\"192\"/>\n"
			"        <bbtco pos=\"0\" len=\"192\"/>\n"
			"        <automationpattern pos=\"0\" len=\"192\"/>\n",
			"" ) ) );

		Song* song = loadFresh( project );

		QVERIFY2( song->unclaimedElements().isEmpty(),
			qPrintable( QStringLiteral( "a pre-rename clip element was preserved instead "
				"of loaded: " ) + listed( song->unclaimedElements() ) ) );
		QCOMPARE( clipCountOfFirstTrack(), 4 );
	}

	//! 1.6.3's own direction, which the round-trip slot above does NOT pin: a
	//! child the format has never heard of is kept verbatim AND is not coerced.
	//! A build that preserved it while still making a clip of it would pass a
	//! survives-the-round-trip check and fill the arrangement with phantom clips.
	void anUnknownChildIsPreservedAndIsNeverCoercedIntoAClip()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "unknown-child.mmp" ) );
		QVERIFY( writeText( project, projectWith(
			"        <futureChild mode=\"mystery\"/>\n", "" ) ) );

		Song* song = loadFresh( project );

		QVERIFY2( song->unclaimedElements().contains(
			QStringLiteral( "/song/trackcontainer/track[0]/futureChild" ) ),
			qPrintable( QStringLiteral( "the unknown child was not reported: " )
				+ listed( song->unclaimedElements() ) ) );
		QCOMPARE( clipCountOfFirstTrack(), 0 );
	}
};

QTEST_GUILESS_MAIN( UnclaimedElementsTest )
#include "UnclaimedElementsTest.moc"
