/*
 * DocumentIndexTest.cpp - the document index, and the one condition under which
 *                          a project may carry one. ARCH-4 S2a.
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

#include <QCryptographicHash>
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

const int kDocumentVersion = 31;

//! The document version this build writes, asked of the build rather than
//! spelled here - a literal in the fixture would be a second version number to
//! move and the first place a bump would go unnoticed.
QString currentVersion()
{
	DataFile fresh( DataFile::Type::SongProject );
	return fresh.documentElement().attribute( QStringLiteral( "version" ) );
}


//! A `<song>` holding \a children, as the content element of a document. A
//! fixture that is not well-formed yields a null element, which fails the slot
//! that asked for it rather than quietly matching an empty index.
QDomElement contentWith( QDomDocument& document, const QString& children )
{
	if( !document.setContent( QStringLiteral( "<song>%1</song>" ).arg( children ) ) )
	{
		qWarning() << "the fixture is not well-formed XML";
		return QDomElement();
	}
	return document.documentElement();
}


//! The names of a document's sections, in order - what a failure should print.
QString namesOf( const DocumentIndex& index )
{
	QStringList names;
	for( const DocumentSection& section : index.sections ) { names.append( section.name ); }
	return names.join( QStringLiteral( "," ) );
}


//! How the project writer serialises one element.
QString serialise( const QDomElement& element )
{
	QString out;
	QTextStream stream( &out );
	element.save( stream, 2 );
	stream.flush();
	return out;
}


QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return QString(); }
	return QString::fromUtf8( file.readAll() );
}


//! A song project with the minimal shape Song::loadProject needs, carrying
//! \a songExtras as the last child of <song> - the position an unclaimed section
//! is read from.
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


} // namespace


class DocumentIndexTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init( true ); }

	void cleanupTestCase() { Engine::destroy(); }

	// -----------------------------------------------------------------------
	// The index itself (SPEC-ARCH-4 1.4)
	// -----------------------------------------------------------------------

	//! A section is a child of the CONTENT element, in document order, named by
	//! its own tag name - the granularity the spec's motivating case needs
	//! ("the tracks and the tempo map" are both children of <song>).
	void sectionsAreTheContentChildrenInDocumentOrder()
	{
		QDomDocument document;
		const QDomElement content = contentWith( document, QStringLiteral(
			"<trackcontainer/>\n<controllers/>\n<tempo-map/>" ) );

		const DocumentIndex index = documentIndex( content, kDocumentVersion );

		QCOMPARE( namesOf( index ), QStringLiteral( "trackcontainer,controllers,tempo-map" ) );
		QCOMPARE( index.sections.at( 0 ).entry, QStringLiteral( "trackcontainer.xml" ) );
	}

	//! A section that carries its own version says so; one that does not
	//! inherits the document's - the rule the <session> block already follows
	//! (src/core/SessionModel.cpp:245, read at :300).
	void aSectionVersionAttributeBeatsTheDocumentVersion()
	{
		QDomDocument document;
		const QDomElement content = contentWith( document,
			QStringLiteral( "<session version=\"1\"/><trackcontainer/>" ) );

		const DocumentIndex index = documentIndex( content, kDocumentVersion );

		QCOMPARE( index.sections.at( 0 ).version, 1 );
		QCOMPARE( index.sections.at( 1 ).version, kDocumentVersion );
	}

	//! head, the index itself and the element the sketch reserves for preserved
	//! foreign content are never sections - and a real section beside them still
	//! is, so the three rules cannot be satisfied by indexing nothing.
	void anElementThatIsNeverASectionIsNotIndexed()
	{
		QDomDocument document;
		const QDomElement content = contentWith( document, QStringLiteral(
			"<head/>\n<z:index/>\n<z:unclaimed/>\n<trackcontainer/>" ) );

		const DocumentIndex index = documentIndex( content, kDocumentVersion );

		QCOMPARE( namesOf( index ), QStringLiteral( "trackcontainer" ) );
	}

	//! The digest is the digest of the section as the root of its own save, in
	//! CANONICAL form. It is pinned by LITERALS computed outside this build, so
	//! the definition cannot drift to a re-rendering that happens to agree with
	//! itself.
	//!
	//! The first literal is the sha256 of exactly these 67 bytes, and the shape of
	//! them is the measurement that matters here - QDomElement::save(stream, n)
	//! takes `n` as the indent width PER LEVEL and puts the node it is called on
	//! at one level, so the element sits at column 2 here and at column 4 in a
	//! document whose content element is at column 2:
	//!
	//!     "  <trackcontainer x=\"1\">\n    <track name=\"t\"/>\n  </trackcontainer>\n"
	void theDigestIsOfTheSectionAsTheWriterSerialisesIt()
	{
		QDomDocument document;
		const QDomElement content = contentWith( document,
			QStringLiteral( "<trackcontainer x=\"1\"><track name=\"t\"/></trackcontainer>" ) );

		const QDomElement section = content.firstChildElement( QStringLiteral( "trackcontainer" ) );
		// `sha256:` and 64 hex characters - the width is part of the contract a
		// reader indexes, so it is asserted rather than assumed.
		QCOMPARE( sectionDigest( section ).size(), QStringLiteral( "sha256:" ).size() + 64 );

		QCOMPARE( sectionDigest( section ), QStringLiteral(
			"sha256:0783106e0a95b9bf0d1e60f49d5b1f03a4e52dd6feffbe46edbcd667ccbd82bb" ) );

		// ...and for an element this simple the canonical rendering and the
		// writer's rendering are the same bytes, so the digest is also the digest
		// of what a saved file carries for it. The assertions after this one are
		// the reason the definition cannot just be "whatever save() produces":
		// save() emits attributes in Qt's attribute QHash order, and Qt seeds
		// qHash() RANDOMLY PER PROCESS - five processes parsing one unchanged file
		// re-saved the same element five different ways (measured 2026-09-21, with
		// stand-alone Qt6 probes; QHashSeed::setDeterministicGlobalSeed() pins the
		// order, which is what identifies the seed as the variable).
		QCOMPARE( sectionDigest( section ), QStringLiteral( "sha256:" ) + QString::fromLatin1(
			QCryptographicHash::hash( serialise( section ).toUtf8(),
				QCryptographicHash::Sha256 ).toHex() ) );

		// The second literal is the sha256 of these 73 bytes. It pins the rule the
		// per-process seed forced: attributes are emitted SORTED BY NAME. The
		// fixture writes `y` before `x` and the canonical form writes `x` before
		// `y`, so the digest can only equal the literal if the order is the
		// digest's own and not the DOM's:
		//
		//     "  <trackcontainer x=\"1\" y=\"2\">\n    <track name=\"t\"/>\n  </trackcontainer>\n"
		QDomDocument reordered;
		const QDomElement reorderedContent = contentWith( reordered,
			QStringLiteral( "<trackcontainer y=\"2\" x=\"1\">"
				"<track name=\"t\"/></trackcontainer>" ) );
		const QDomElement reorderedSection =
			reorderedContent.firstChildElement( QStringLiteral( "trackcontainer" ) );
		QCOMPARE( sectionDigest( reorderedSection ), QStringLiteral(
			"sha256:ea85de3716668dbd0a5f4b56246565e55a0c64da5f81ff593d6357d3d44d4812" ) );

		// ...and the same content with the attributes entered the other way round
		// digests identically, which is the property the canonical form exists
		// for: a reader in another process gets this value too.
		QDomDocument straight;
		const QDomElement straightContent = contentWith( straight,
			QStringLiteral( "<trackcontainer x=\"1\" y=\"2\">"
				"<track name=\"t\"/></trackcontainer>" ) );
		QCOMPARE( sectionDigest( straightContent.firstChildElement(
			QStringLiteral( "trackcontainer" ) ) ), sectionDigest( reorderedSection ) );

		// The third literal is the sha256 of these 113 bytes, and it pins
		// everything else the canonical form decides at once: nesting and
		// self-closing, attribute escaping, and character data - which is digested
		// TRIMMED, so that a parser-built DOM (which carries a run's surrounding
		// indentation inside the text node) and a setter-built one agree. The two
		// fixtures below hold the same content, one written on one line and one
		// written indented, and must digest identically:
		//
		//     "  <section a=\"1\" b=\"&amp;&lt;&quot;\">\n    <x y=\"1\">\n"
		//     "      <z/>\n    </x>\n    hello &amp; &lt;bye&gt;\n  </section>\n"
		QDomDocument flat;
		const QDomElement flatContent = contentWith( flat, QStringLiteral(
			"<section a=\"1\" b=\"&amp;&lt;&quot;\"><x y=\"1\"><z/></x>"
			"hello &amp; &lt;bye&gt;</section>" ) );
		const QDomElement flatSection =
			flatContent.firstChildElement( QStringLiteral( "section" ) );
		QCOMPARE( sectionDigest( flatSection ), QStringLiteral(
			"sha256:c8f4588f4629eeb4993ba9093c884f4ea8d0269c9711a0faaefd63ef0e4d39ae" ) );

		QDomDocument indented;
		const QDomElement indentedContent = contentWith( indented, QStringLiteral(
			"<section a=\"1\" b=\"&amp;&lt;&quot;\">\n"
			"  <x y=\"1\">\n    <z/>\n  </x>\n"
			"  hello &amp; &lt;bye&gt;\n"
			"</section>" ) );
		QCOMPARE( sectionDigest( indentedContent.firstChildElement(
			QStringLiteral( "section" ) ) ), sectionDigest( flatSection ) );

		// ...and it does NOT depend on where the section sits. That is the
		// property the index is bought for: a section filed into its own file
		// keeps its digest, so a container's entry can be verified against the
		// index of the document it came from.
		QDomDocument deep;
		QVERIFY( deep.setContent( QStringLiteral(
			"<zene-project><head/><song><wrapper><trackcontainer x=\"1\">"
			"<track name=\"t\"/></trackcontainer></wrapper></song></zene-project>" ) ) );
		const QDomElement nested = deep.documentElement()
			.firstChildElement( QStringLiteral( "song" ) )
			.firstChildElement( QStringLiteral( "wrapper" ) )
			.firstChildElement( QStringLiteral( "trackcontainer" ) );
		QVERIFY2( !nested.isNull(), "the nested fixture is not well-formed" );
		QCOMPARE( sectionDigest( nested ), sectionDigest( section ) );

		// One attribute, two levels down: the digest of the whole section.
		const DocumentSection before = documentIndex( content, kDocumentVersion ).sections.at( 0 );
		section.firstChildElement( QStringLiteral( "track" ) )
			.setAttribute( QStringLiteral( "name" ), QStringLiteral( "u" ) );
		const DocumentSection after = documentIndex( content, kDocumentVersion ).sections.at( 0 );

		QVERIFY2( before.digest != after.digest,
			"a change inside the section did not move its digest" );
		QCOMPARE( after.digest, sectionDigest( section ) );
	}

	//! Write, then read back: every field survives, the namespace is declared on
	//! the root, and the index sits directly after <head> - the position the
	//! format sketch gives it, and one a reader scanning for it can rely on.
	void writeThenParseRoundTripsEveryField()
	{
		QDomDocument document;
		QDomElement root = document.createElement( QStringLiteral( "zene-project" ) );
		document.appendChild( root );
		root.appendChild( document.createElement( QStringLiteral( "head" ) ) );
		root.appendChild( document.createElement( QStringLiteral( "song" ) ) );

		const DocumentIndex written = documentIndex( root.firstChildElement(
			QStringLiteral( "song" ) ), kDocumentVersion );
		QVERIFY( written.isEmpty() );

		// A document whose content element has a section does round trip.
		root.firstChildElement( QStringLiteral( "song" ) ).appendChild(
			document.createElement( QStringLiteral( "trackcontainer" ) ) );
		const DocumentIndex source = documentIndex( root.firstChildElement(
			QStringLiteral( "song" ) ), kDocumentVersion );
		QCOMPARE( source.sections.size(), 1 );

		QVERIFY( writeDocumentIndex( source, document, root ) );
		QCOMPARE( root.attribute( documentIndexNamespaceAttribute() ),
			documentIndexNamespaceUri() );

		const QDomElement head = root.firstChildElement( QStringLiteral( "head" ) );
		QCOMPARE( head.nextSiblingElement().nodeName(), documentIndexNodeName() );

		const DocumentIndex parsed = parseDocumentIndex( root );
		QCOMPARE( parsed.sections.size(), 1 );
		QCOMPARE( parsed.sections.at( 0 ).name, source.sections.at( 0 ).name );
		QCOMPARE( parsed.sections.at( 0 ).entry, source.sections.at( 0 ).entry );
		QCOMPARE( parsed.sections.at( 0 ).version, source.sections.at( 0 ).version );
		QCOMPARE( parsed.sections.at( 0 ).digest, source.sections.at( 0 ).digest );

		// The index is not a section of the document it indexes.
		QCOMPARE( namesOf( documentIndex( root.firstChildElement( QStringLiteral( "song" ) ),
			kDocumentVersion ) ), QStringLiteral( "trackcontainer" ) );
	}

	//! An empty index changes nothing: not the bytes, and not the root's
	//! attributes. This is the additive rule's half that lives in the writer.
	void anEmptyIndexIsNotWrittenAndTheDocumentIsUnchanged()
	{
		QDomDocument document;
		QVERIFY( document.setContent( QStringLiteral(
			"<zene-project version=\"31\"><head/><song/></zene-project>" ) ) );
		QDomElement root = document.documentElement();

		const QString before = serialise( root );
		QVERIFY( !writeDocumentIndex( DocumentIndex(), document, root ) );
		QCOMPARE( serialise( root ), before );
		QVERIFY2( !root.hasAttribute( documentIndexNamespaceAttribute() ),
			"an empty index still declared the namespace" );
		QVERIFY( parseDocumentIndex( root ).isEmpty() );
	}

	//! A `<z:section>` with no name is skipped: it names nothing, so admitting it
	//! would turn a damaged index into a load that silently omits a section.
	void anEntryThatCannotBeNamedIsSkipped()
	{
		QDomDocument document;
		QVERIFY( document.setContent( QStringLiteral(
			"<zene-project version=\"31\"><head/>"
			"<z:index><z:section entry=\"a.xml\" digest=\"sha256:x\"/>"
			"<z:section name=\"song\" entry=\"song.xml\" v=\"31\" digest=\"sha256:y\"/>"
			"</z:index><song/></zene-project>" ) ) );

		const DocumentIndex index = parseDocumentIndex( document.documentElement() );

		QCOMPARE( index.sections.size(), 1 );
		QCOMPARE( index.sections.at( 0 ).name, QStringLiteral( "song" ) );
		QVERIFY2( index.section( QStringLiteral( "song" ) ) != nullptr, "lookup by name failed" );
		QVERIFY( index.section( QStringLiteral( "absent" ) ) == nullptr );
	}

	// -----------------------------------------------------------------------
	// The gate: which projects may carry one (SPEC-ARCH-4 5.2 risk 2)
	// -----------------------------------------------------------------------

	//! The owed proof, and the additive rule's own test. A project carrying a
	//! section this build does not claim gets an index whose digests verify
	//! against the bytes the file actually carries; the SAME project with that
	//! section removed re-saves with no index and no namespace - so a project
	//! that uses none of this keeps the bytes it always had.
	void anUnclaimedSectionEarnsAnIndexAndNoUnclaimedSectionDoesNot()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString withExtra = dir.filePath( QStringLiteral( "extra.mmp" ) );
		QFile in( withExtra );
		QVERIFY( in.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
		QVERIFY( in.write( songProject( QStringLiteral( "    <newsection a=\"1\">\n"
			"      <inner b=\"2\"/>\n    </newsection>\n" ) ).toUtf8() ) >= 0 );
		in.close();

		loadFresh( withExtra );
		QCOMPARE( Engine::getSong()->unclaimedElements(),
			QStringList{ QStringLiteral( "/song/newsection" ) } );

		const QString savedPath = dir.filePath( QStringLiteral( "extra-out.mmp" ) );
		QVERIFY2( Engine::getSong()->saveProjectFile( savedPath ), "the project did not save" );
		const QString saved = readText( savedPath );
		QVERIFY2( saved.contains( QStringLiteral( "<z:index>" ) ),
			"a project with preserved content re-saved without an index" );

		QDomDocument document;
		QVERIFY( document.setContent( saved ) );
		const QDomElement root = document.documentElement();
		QCOMPARE( root.attribute( documentIndexNamespaceAttribute() ),
			documentIndexNamespaceUri() );

		const DocumentIndex index = parseDocumentIndex( root );

		// More than the fixture wrote, MEASURED: saveProjectFile writes the
		// mixer, the controller states, the scales and the keymaps on every
		// save regardless of what was loaded, so a document's <song> carries
		// those sections even when its source file did not. The old section is
		// last - the position S1 re-emits preserved content at, after
		// everything this build writes - which is the document-order contract
		// the index follows rather than sorts.
		QCOMPARE( namesOf( index ), QStringLiteral(
			"trackcontainer,mixer,controllers,scales,keymaps,newsection" ) );

		// Verifying the index against a re-render of each section it names. This
		// is the assertion that made the digest go canonical: it recomputes the
		// digest with sectionDigest() over a section the SAVED FILE was parsed
		// into, and before the canonical form that recomputation disagreed with
		// the recorded digest on roughly one run in fifteen, because
		// QDomElement::save() orders attributes by a per-process-random QHash
		// seed. It is now a reader's own integrity check, which is what it was
		// written to be.
		//
		// Note it is still NOT "hash the section's substring out of the saved
		// file": sectionDigest() renders the section as the root of its own save
		// (and in canonical form), and the saved file carries it one level deeper
		// in the writer's own rendering. See DocumentIndex.h.
		const QDomElement song = root.firstChildElement( QStringLiteral( "song" ) );
		for( const DocumentSection& section : index.sections )
		{
			const QDomElement element = song.firstChildElement( section.name );
			QVERIFY2( !element.isNull(), qPrintable( section.name + QStringLiteral(
				" is indexed but not in the document" ) ) );
			QCOMPARE( section.digest, sectionDigest( element ) );
		}

		// ...and the same project without it carries no index at all.
		const QString clean = dir.filePath( QStringLiteral( "clean.mmp" ) );
		QFile cleanFile( clean );
		QVERIFY( cleanFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
		QVERIFY( cleanFile.write( songProject( QString() ).toUtf8() ) >= 0 );
		cleanFile.close();

		loadFresh( clean );
		QVERIFY( Engine::getSong()->unclaimedElements().isEmpty() );

		const QString cleanSavedPath = dir.filePath( QStringLiteral( "clean-out.mmp" ) );
		QVERIFY2( Engine::getSong()->saveProjectFile( cleanSavedPath ), "the project did not save" );
		const QString cleanSaved = readText( cleanSavedPath );
		QVERIFY2( !cleanSaved.contains( QStringLiteral( "<z:index" ) ),
			"a project this build understands completely gained an index" );
		QVERIFY2( !cleanSaved.contains( documentIndexNamespaceAttribute() ),
			"a project this build understands completely gained the z namespace" );
	}
};

QTEST_GUILESS_MAIN( DocumentIndexTest )
#include "DocumentIndexTest.moc"
