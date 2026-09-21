/*
 * PartialLoadTest.cpp - the reader half of SPEC-ARCH-4 1.4: a project loaded
 *                       with sections named to SKIP, and the read-only policy
 *                       that puts such a session under. ARCH-4 S2b.
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

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "DataFile.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! The document version this build writes, asked of the build rather than
//! spelled here - a literal in a fixture is a second version number to move and
//! the first place a bump would go unnoticed.
QString currentVersion()
{
	DataFile fresh( DataFile::Type::SongProject );
	return fresh.documentElement().attribute( QStringLiteral( "version" ) );
}


//! A song project with the minimal shape Song::loadProject needs, carrying
//! \a songExtras as the last child of <song> - the position an unclaimed section
//! is read from, and therefore the position a partial load leaves one out of.
QString songProject( const QString& songExtras )
{
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"%2"
		"  </song>\n"
		"</lmms-project>\n" ).arg( currentVersion(), songExtras );
}


//! One document in the shape DataFile's own content-element selection needs, so
//! the DataFile-level slots below can say the same thing about a plain `.mmp` and
//! about the qCompress() output a legacy `.mmpz` reaches DataFile as.
QByteArray reducibleProject()
{
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<zene-project version=\"%1\" type=\"song\"><head/><song>"
		"<bigclip><c/></bigclip><keep/></song></zene-project>\n"
		).arg( currentVersion() ).toUtf8();
}


//! \a text written at \a path. Answers false rather than throwing, so a slot
//! asserts the write happened instead of measuring a file that is not there.
bool writeText( const QString& path, const QString& text )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) { return false; }
	return file.write( text.toUtf8() ) >= 0;
}


//! Load a project file into the engine's song from a known-empty state. Clearing
//! and disabling load-on-launch first is what makes every slot here a statement
//! about THIS project.
void loadFresh( const QString& path )
{
	Song* song = Engine::getSong();
	song->clearProject();
	song->setLoadOnLaunch( false );
	song->loadProject( path );
}


//! loadFresh() with the partial-load request (SPEC-ARCH-4 1.4, ARCH-4 S2b): the
//! same known-empty start, so a slot can compare the two answers for ONE file.
void loadFreshSkipping( const QString& path, const QStringList& skipSections )
{
	Song* song = Engine::getSong();
	song->clearProject();
	song->setLoadOnLaunch( false );
	song->loadProject( path, skipSections );
}


//! A readable file's bytes, or an empty string - used only to say a section is
//! NOT in what was written, so an unreadable file must fail that assertion.
QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return QString(); }
	return QString::fromUtf8( file.readAll() );
}

} // namespace


class PartialLoadTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init( true ); }

	void cleanupTestCase() { Engine::destroy(); }

	// -----------------------------------------------------------------------
	// What DataFile does with a skip list (the mechanism)
	// -----------------------------------------------------------------------

	//! The content element name is DERIVED from the root's `type` attribute, not
	//! taken from it, and this is the case that proves the round trip is
	//! load-bearing. A legacy `type="pattern"` document's content element is
	//! `<midiclip>` - DataFile's own compat map says so - so a reader that looked
	//! for a `<pattern>` child would find nothing, remove nothing, and hand the
	//! skipped section to the parser while reporting that it had been skipped.
	//! Both halves are measured, which a name read off the attribute cannot
	//! satisfy: the second half is the one that fails when it is.
	void theContentElementNameIsDerivedFromTheRootType()
	{
		const QStringList skip{ QStringLiteral( "bigclip" ) };

		QStringList removed;
		DataFile songFile( reducibleProject(), skip, &removed );
		QCOMPARE( removed.join( QStringLiteral( "," ) ), QStringLiteral( "bigclip" ) );
		QVERIFY( songFile.content().firstChildElement( QStringLiteral( "bigclip" ) ).isNull() );
		QVERIFY( !songFile.content().firstChildElement( QStringLiteral( "keep" ) ).isNull() );

		// type="pattern" tells the writer `<midiclip>` is the content element.
		removed.clear();
		const QByteArray patternDoc = QStringLiteral(
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"%1\" type=\"pattern\"><head/><midiclip>"
			"<bigclip><c/></bigclip><keep/></midiclip></lmms-project>\n"
			).arg( currentVersion() ).toUtf8();
		DataFile patternFile( patternDoc, skip, &removed );

		QCOMPARE( removed.join( QStringLiteral( "," ) ), QStringLiteral( "bigclip" ) );
		QVERIFY( patternFile.content().firstChildElement( QStringLiteral( "bigclip" ) ).isNull() );
		QVERIFY( !patternFile.content().firstChildElement( QStringLiteral( "keep" ) ).isNull() );
	}

	//! The ordering the wiring turns on, measured. A legacy `.mmpz` reaches
	//! DataFile as the file's bytes whole - qCompress() output - and the byte
	//! scanner cannot follow compressed bytes to an end tag, so the reduction has
	//! to be attempted AGAIN on what qUncompress() returns. Wired the other way
	//! round, a load reports a section as not-loaded after parsing it in full,
	//! which makes the caller believe it holds less than it does.
	void aCompressedProjectIsReducedAfterDecompression()
	{
		const QByteArray plain = reducibleProject();
		const QByteArray compressed = qCompress( plain );
		const QStringList skip{ QStringLiteral( "bigclip" ) };

		// The scanner really does refuse the compressed shape. This is the first
		// half of the measurement and it is what makes the second attempt
		// necessary rather than decorative.
		QStringList onCompressedBytes;
		QCOMPARE( reduceDocumentSections( compressed, QStringLiteral( "song" ), skip,
			&onCompressedBytes ), compressed );
		QCOMPARE( onCompressedBytes.size(), 0 );

		// ...and the load still skips it, because the second attempt reduces what
		// qUncompress() returned.
		QStringList removed;
		DataFile compressedFile( compressed, skip, &removed );
		QCOMPARE( removed.join( QStringLiteral( "," ) ), QStringLiteral( "bigclip" ) );
		QVERIFY( compressedFile.content().firstChildElement( QStringLiteral( "bigclip" ) ).isNull() );
		QVERIFY( !compressedFile.content().firstChildElement( QStringLiteral( "keep" ) ).isNull() );
	}

	//! The report describes the DOCUMENT, not the request, and it is assigned
	//! rather than appended to. A caller that reuses one list across several
	//! loads cannot accumulate, and a name that is not a section of this document
	//! skips nothing and is therefore reported as nothing.
	void theReportDescribesTheDocumentNotTheRequest()
	{
		const QByteArray doc = reducibleProject();
		QStringList removed{ QStringLiteral( "from-an-earlier-document" ) };

		DataFile absent( doc, { QStringLiteral( "notpresent" ) }, &removed );
		QCOMPARE( removed.size(), 0 );
		QVERIFY( !absent.content().firstChildElement( QStringLiteral( "bigclip" ) ).isNull() );

		// A whole load (no skip list) leaves the list empty too.
		removed = QStringList{ QStringLiteral( "stale" ) };
		DataFile whole( doc, QStringList(), &removed );
		QCOMPARE( removed.size(), 0 );
		QVERIFY( !whole.content().firstChildElement( QStringLiteral( "bigclip" ) ).isNull() );

		// ...and a real skip still reports itself.
		QStringList skipped;
		DataFile partial( doc, { QStringLiteral( "bigclip" ) }, &skipped );
		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "bigclip" ) );
	}

	// -----------------------------------------------------------------------
	// What a partial load does to the session (the policy)
	// -----------------------------------------------------------------------

	//! Naming a section means its bytes never reach a parser, and the session
	//! says so. Both halves are asserted against the SAME file loaded whole, so
	//! the difference between the two runs is the skip list and nothing else.
	void aPartialLoadSkipsExactlyTheSectionsItWasGiven()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "partial.mmp" ) );
		QVERIFY( writeText( path, songProject( QStringLiteral(
			"    <newsection><child/></newsection>\n"
			"    <othersection/>\n" ) ) ) );

		// The control: loaded whole, both sections are preserved.
		loadFresh( path );
		QVERIFY( Engine::getSong()->notLoadedSections().isEmpty() );
		QVERIFY( !Engine::getSong()->isPartialLoad() );
		QCOMPARE( Engine::getSong()->unclaimedElements().join( QStringLiteral( "," ) ),
			QStringLiteral( "/song/newsection,/song/othersection" ) );

		// ...and asking for one of them to be left out removes exactly that one.
		// The SURVIVING section is the assertion that matters: a reducer that
		// removed both, or the wrong one, would still report a plausible list.
		loadFreshSkipping( path, { QStringLiteral( "newsection" ) } );
		QCOMPARE( Engine::getSong()->notLoadedSections().join( QStringLiteral( "," ) ),
			QStringLiteral( "newsection" ) );
		QVERIFY( Engine::getSong()->isPartialLoad() );
		QCOMPARE( Engine::getSong()->unclaimedElements().join( QStringLiteral( "," ) ),
			QStringLiteral( "/song/othersection" ) );
	}

	//! A session holding less than its file must not write that loss back. The
	//! refusal names the sections, because "cannot save" without a reason sends
	//! the caller looking at permissions instead of at its own `sections` list -
	//! and it must not have written anything on the way to refusing.
	void aPartialLoadRefusesToSave()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "partial.mmp" ) );
		QVERIFY( writeText( path, songProject( QStringLiteral( "    <newsection/>\n" ) ) ) );

		loadFreshSkipping( path, { QStringLiteral( "newsection" ) } );
		QVERIFY( Engine::getSong()->isPartialLoad() );

		Song* song = Engine::getSong();
		const QString target = dir.filePath( QStringLiteral( "out.mmp" ) );
		QVERIFY2( !song->saveProjectFile( target ),
			"a partial session wrote a document missing the sections it skipped" );
		QVERIFY2( song->saveRefusal().contains( QStringLiteral( "newsection" ) ),
			"the refusal does not name the sections that are not in memory" );
		QVERIFY2( !QFile::exists( target ), "the refused save left a file behind" );

		// Reopened WHOLE, the same document saves: the refusal is a statement
		// about this session's contents, not about the file.
		loadFresh( path );
		QVERIFY( !song->isPartialLoad() );
		QVERIFY2( song->saveProjectFile( target ), "a whole load refused to save" );
		QVERIFY( song->saveRefusal().isEmpty() );
		QVERIFY( QFile::exists( target ) );
	}

	//! A refused load must not leave the still-loaded project marked partial. The
	//! report is carried in a local list and only becomes session state once the
	//! load is ACCEPTED, so a file that cannot be read changes nothing about what
	//! is open - and in particular cannot make the previous, whole project
	//! un-saveable.
	void aRefusedLoadDoesNotMarkTheSessionPartial()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString good = dir.filePath( QStringLiteral( "good.mmp" ) );
		QVERIFY( writeText( good, songProject( QStringLiteral( "    <newsection/>\n" ) ) ) );
		loadFresh( good );
		QVERIFY( !Engine::getSong()->isPartialLoad() );
		QCOMPARE( Engine::getSong()->unclaimedElements().join( QStringLiteral( "," ) ),
			QStringLiteral( "/song/newsection" ) );

		// A file the parser cannot follow to its end at all. It is attempted on
		// the OPEN session deliberately, without clearProject() first: the claim
		// is about what a failed load does to the project that is already loaded,
		// and a helper that starts from a known-empty state would clear that
		// project itself and so measure its own setup instead.
		const QString bad = dir.filePath( QStringLiteral( "bad.mmp" ) );
		QVERIFY( writeText( bad, QStringLiteral( "<lmms-project><song><oops>" ) ) );

		Engine::getSong()->loadProject( bad, { QStringLiteral( "newsection" ) } );
		QVERIFY2( !Engine::getSong()->loadRefusal().isEmpty(), "the broken file was not refused" );
		QVERIFY2( !Engine::getSong()->isPartialLoad(),
			"a refused load marked the session partial" );
		QVERIFY( Engine::getSong()->notLoadedSections().isEmpty() );

		// The OTHER refusal path - a local-plugin path, refused only after the
		// document PARSED. It is the one that can actually falsify the assertion
		// above: an unparseable file leaves the reduction's report EMPTY (nothing
		// was removed from a document that never parsed), so wiring the report
		// into the session before the refusal check would still read as no-op
		// there. Here the skipped section really was removed first, so the report
		// is non-empty and the ordering is observable.
		const QString local = dir.filePath( QStringLiteral( "local.mmp" ) );
		QVERIFY( writeText( local, songProject( QStringLiteral(
			"    <newsection/>\n"
			"    <plugin name=\"local:evil\"/>\n" ) ) ) );

		Engine::getSong()->loadProject( local, { QStringLiteral( "newsection" ) } );
		QVERIFY2( !Engine::getSong()->loadRefusal().isEmpty(),
			"the local-plugin file was not refused" );
		QVERIFY2( !Engine::getSong()->isPartialLoad(),
			"the local-plugin refusal marked the session partial" );
		QVERIFY( Engine::getSong()->notLoadedSections().isEmpty() );

		// The session is the SAME one it was: the section the open project did
		// not claim is still preserved, so neither failed attempt changed it.
		QVERIFY2( Engine::getSong()->unclaimedElements().join( QStringLiteral( "," ) )
				== QStringLiteral( "/song/newsection" ),
			"a refused load discarded the open project's preserved sections" );

		// ...and it is still saveable, still carrying that section.
		const QString target = dir.filePath( QStringLiteral( "still.mmp" ) );
		QVERIFY2( Engine::getSong()->saveProjectFile( target ),
			"the refusal left the still-loaded session un-saveable" );
		const QString written = readText( target );
		QVERIFY2( written.contains( QStringLiteral( "<newsection" ) ),
			"the saved document lost the open project's preserved section" );
	}
};

QTEST_GUILESS_MAIN( PartialLoadTest )
#include "PartialLoadTest.moc"
