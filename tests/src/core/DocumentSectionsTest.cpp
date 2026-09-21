/*
 * DocumentSectionsTest.cpp - reduceDocumentSections(): the byte-exact section
 *                            reducer a partial load reads through. ARCH-4 S2b.
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
#include <QString>
#include <QStringList>

#include "DocumentIndex.h"

using namespace lmms;

namespace
{

//! The content element every fixture below reduces inside of.
const char* const kContent = "song";

/*! A document whose <song> holds, in order: a nested-only section, a big one, a
 *  same-named element one level DEEPER (which must never count as a section), a
 *  prefixed one, and one that is the last child - the end-tag boundary.
 *
 *  The `xmlns:z` declaration is on the root so the prefixed child is a legal
 *  document whichever way a parser is asked to treat namespaces. */
QByteArray fixture()
{
	return QByteArray(
		"<?xml version=\"1.0\"?>\n"
		"<zene-project version=\"31\" type=\"song\" xmlns:z=\"urn:zene:core:1\">\n"
		"  <head bpm=\"124\"/>\n"
		"  <song>\n"
		"    <trackcontainer c=\"1\">\n"
		"      <track name=\"a\"/>\n"
		"    </trackcontainer>\n"
		"    <bigclip>\n"
		"      <clip id=\"1\"/><clip id=\"2\"/>\n"
		"    </bigclip>\n"
		"    <wrapper><bigclip><clip id=\"nested\"/></bigclip></wrapper>\n"
		"    <z:provenance seq=\"1\"/>\n"
		"    <lastskip x=\"9\"><inner/></lastskip>\n"
		"  </song>\n"
		"</zene-project>\n" );
}


/*! The bytes one element occupies, from its `<` to the end of its end tag,
 *  located by tag NAME so the expectation is built from the fixture's own text
 *  rather than transcribed beside it. Nesting of the SAME name does not need to
 *  be handled here: the only repeated name is `bigclip`, whose outer occurrence
 *  is the first one and whose first `</bigclip>` is therefore its own. */
QByteArray sliceOf( const QByteArray& data, const QString& name )
{
	const QByteArray open = QByteArray( "<" ) + name.toUtf8();
	int begin = data.indexOf( open );
	if( begin < 0 ) { return QByteArray(); }
	const int gt = data.indexOf( '>', begin );
	if( gt < 0 ) { return QByteArray(); }
	if( data.at( gt - 1 ) == '/' ) { return data.mid( begin, gt - begin + 1 ); }

	const QByteArray close = QByteArray( "</" ) + name.toUtf8() + ">";
	const int end = data.indexOf( close, gt );
	if( end < 0 ) { return QByteArray(); }
	return data.mid( begin, end + close.size() - begin );
}


QDomDocument parse( const QByteArray& bytes )
{
	QDomDocument document;
	QString error;
	int line = -1;
	int column = -1;
	document.setContent( bytes, &error, &line, &column );
	return document;
}


//! How many DIRECT children of \a parent carry \a name - the count that says
//! whether a section is still there, as opposed to a recursive tag count that a
//! nested same-named element would also satisfy.
int directChildrenNamed( const QDomElement& parent, const QString& name )
{
	int count = 0;
	for( QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement() )
	{
		if( e.tagName() == name ) { ++count; }
	}
	return count;
}


//! The content element of \a document, or a null element when it is absent.
QDomElement contentOf( const QDomDocument& document, const QString& contentName )
{
	return document.documentElement().firstChildElement( contentName );
}

} // namespace


class DocumentSectionsTest : public QObject
{
	Q_OBJECT

private slots:
	// ---------------------------------------------------------------- identity

	//! With nothing to skip the answer must be the SAME BYTES, not an equal
	//! document: this is the property the additive rule is built on, and a
	//! re-serialiser would fail it while still "loading the same thing".
	void identityWithoutASkipList()
	{
		const QByteArray data = fixture();
		QStringList skipped;

		QCOMPARE( reduceDocumentSections( data, QString::fromLatin1( kContent ), {}, &skipped ), data );
		QCOMPARE( skipped.size(), 0 );

		// An empty payload, an empty content name and a name that is not in the
		// document are all the same answer.
		QCOMPARE( reduceDocumentSections( QByteArray(), QString::fromLatin1( kContent ),
			{ QStringLiteral( "bigclip" ) } ), QByteArray() );
		QCOMPARE( reduceDocumentSections( data, QString(), { QStringLiteral( "bigclip" ) } ), data );
		QCOMPARE( reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "notpresent" ) } ), data );
		QCOMPARE( reduceDocumentSections( data, QStringLiteral( "nosuchcontent" ),
			{ QStringLiteral( "bigclip" ) } ), data );
	}

	//! A malformed document is returned EXACTLY as it was. A half-reduced
	//! document is the one outcome that must never happen: it looks
	//! well-formed and is missing something arbitrary.
	void malformedInputIsReturnedUnchanged()
	{
		const QByteArray broken(
			"<zene-project><song><bigclip><oops></song></zene-project>" );
		QStringList skipped;
		QCOMPARE( reduceDocumentSections( broken, QString::fromLatin1( kContent ),
			{ QStringLiteral( "bigclip" ) }, &skipped ), broken );

		// The scan DOES reach the named section's start tag here - it fails later,
		// on the truncated document. So this is the case that says a refusal must
		// not report a removal it never made: `skipped` would otherwise be
		// non-empty beside bytes that still carry the section, and the caller's
		// one test for "did anything go?" would answer yes.
		QCOMPARE( skipped.size(), 0 );
	}

	//! The report is ASSIGNED, not appended to. A caller that reuses one list
	//! across calls - the natural thing for a reader loading several documents -
	//! must not accumulate the previous document's sections.
	void theReportIsAssignedNotAppendedTo()
	{
		const QByteArray data = fixture();
		QStringList skipped { QStringLiteral( "from-an-earlier-document" ) };

		reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "lastskip" ) }, &skipped );

		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "lastskip" ) );

		// A refusal removes nothing, so it must not disturb what is already there
		// either.
		QCOMPARE( reduceDocumentSections( data, QStringLiteral( "nosuchcontent" ),
			{ QStringLiteral( "bigclip" ) }, &skipped ), data );
		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "lastskip" ) );
	}

	// ------------------------------------------------------------- the removals

	//! Every section boundary the reducer has to get right, in one document.
	void removesExactlyTheNamedSections()
	{
		const QByteArray data = fixture();
		QStringList skipped;

		const QByteArray reduced = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "bigclip" ), QStringLiteral( "lastskip" ),
				QStringLiteral( "z:provenance" ) }, &skipped );

		// Document order, not the order they were asked for.
		QCOMPARE( skipped.join( QStringLiteral( "," ) ),
			QStringLiteral( "bigclip,z:provenance,lastskip" ) );

		// BYTE-EXACT: the expected answer is the input with precisely those three
		// ranges deleted - built by deletion, so the assertion is against the
		// input's own bytes rather than against a re-rendering of them.
		QByteArray expected = data;
		expected.replace( sliceOf( data, QStringLiteral( "lastskip" ) ), QByteArray() );
		expected.replace( sliceOf( data, QStringLiteral( "z:provenance" ) ), QByteArray() );
		expected.replace( sliceOf( data, QStringLiteral( "bigclip" ) ), QByteArray() );
		QCOMPARE( reduced, expected );

		// The result is still a document, and the nested same-named element and
		// everything else survived.
		const QDomDocument document = parse( reduced );
		QCOMPARE( directChildrenNamed( contentOf( document, QStringLiteral( "song" ) ),
			QStringLiteral( "bigclip" ) ), 0 );
		QVERIFY( reduced.contains( "<clip id=\"nested\"/>" ) );
		QVERIFY( reduced.contains( "<trackcontainer c=\"1\">" ) );
		QVERIFY( reduced.contains( "<head bpm=\"124\"/>" ) );
		QVERIFY( !reduced.contains( "<inner/>" ) );
	}

	//! A self-closing child of the content element, and the LAST one - the two
	//! boundaries where an off-by-one would corrupt bytes rather than merely
	//! remove the wrong thing.
	void removesASelfClosingAndALastChild()
	{
		const QByteArray data = fixture();
		QStringList skipped;

		const QByteArray reduced = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "z:provenance" ) }, &skipped );
		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "z:provenance" ) );
		QVERIFY2( !reduced.contains( "z:provenance" ), "the self-closing section survived" );
		QVERIFY2( reduced.contains( "</song>" ), "removing a self-closing child ate the content end tag" );
		QVERIFY2( reduced.contains( "<lastskip x=\"9\">" ), "a sibling was removed too" );

		skipped.clear();
		const QByteArray last = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "lastskip" ) }, &skipped );
		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "lastskip" ) );
		QVERIFY2( !last.contains( "<lastskip" ), "the last-child section survived" );
		QVERIFY2( last.contains( "</song>\n</zene-project>" ),
			"removing the last child ate the content element's end tag" );
	}

	//! Only DIRECT children of the content element are sections, and a
	//! same-named element one level deeper must survive untouched - otherwise a
	//! skip list would delete content the index never named.
	void onlyDirectChildrenOfTheContentAreSections()
	{
		const QByteArray data = fixture();
		const QByteArray reduced = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "bigclip" ) } );

		const QDomDocument document = parse( reduced );
		QCOMPARE( directChildrenNamed( contentOf( document, QStringLiteral( "song" ) ),
			QStringLiteral( "bigclip" ) ), 0 );
		QVERIFY2( reduced.contains( "<clip id=\"nested\"/>" ),
			"a same-named element one level deeper was removed as if it were a section" );

		// The nested one is still a DIRECT child of its own parent, which is what
		// makes the pair of counts meaningful rather than a tag-count coincidence.
		const QDomElement wrapper =
			contentOf( document, QStringLiteral( "song" ) ).firstChildElement( QStringLiteral( "wrapper" ) );
		QVERIFY2( !wrapper.isNull(), "the wrapper element did not survive the reduction" );
		QCOMPARE( directChildrenNamed( wrapper, QStringLiteral( "bigclip" ) ), 1 );

		// ...and a name that occurs ONLY deeper is not a section at all, so
		// naming it changes nothing.
		QCOMPARE( reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "clip" ) } ), data );
	}

	//! A prefixed section is addressed by its prefixed name, because the reducer
	//! scans with namespace processing OFF - the same setting the project's own
	//! reader uses. With it ON the name would resolve and nothing would match.
	void prefixedSectionsAreAddressedByTheirPrefixedName()
	{
		const QByteArray data = fixture();

		// The resolved name does not match...
		QCOMPARE( reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "provenance" ) } ), data );

		// ...the literal one does.
		const QByteArray reduced = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "z:provenance" ) } );
		QVERIFY( !reduced.contains( "z:provenance" ) );
		QVERIFY( reduced.contains( "xmlns:z=\"urn:zene:core:1\"" ) );
	}

	//! Colliding names are all-or-nothing: the index addresses a section by its
	//! own tag name, so two children sharing one name cannot be told apart and
	//! BOTH go. Pinned so the limit cannot change silently.
	void collidingNamesAreAllOrNothing()
	{
		const QByteArray data(
			"<p>\n<song>\n<a x=\"1\"/>\n<a x=\"2\"/>\n<b/>\n</song>\n</p>\n" );
		QStringList skipped;
		const QByteArray reduced = reduceDocumentSections( data,
			QString::fromLatin1( kContent ), { QStringLiteral( "a" ) }, &skipped );

		QCOMPARE( skipped.join( QStringLiteral( "," ) ), QStringLiteral( "a,a" ) );
		QVERIFY( !reduced.contains( "x=\"1\"" ) );
		QVERIFY( !reduced.contains( "x=\"2\"" ) );
		QVERIFY( reduced.contains( "<b/>" ) );
	}

	//! What the mechanism is FOR, stated as an assertion: the unselected
	//! section's content is not merely dropped afterwards - it never reaches a
	//! parser, so the reduced parse holds none of its objects. This is the
	//! unit-scale form of the spec's "prove the rest was not parsed".
	void theUnselectedSectionIsAbsentFromTheParse()
	{
		const QByteArray data = fixture();

		// The full document carries the section's clip objects...
		QCOMPARE( int( parse( data ).elementsByTagName( QStringLiteral( "clip" ) ).count() ), 3 );

		// ...and the reduced one carries only the nested survivor.
		const QByteArray reduced = reduceDocumentSections( data, QString::fromLatin1( kContent ),
			{ QStringLiteral( "bigclip" ), QStringLiteral( "lastskip" ) } );
		QCOMPARE( int( parse( reduced ).elementsByTagName( QStringLiteral( "clip" ) ).count() ), 1 );
		QCOMPARE( int( parse( reduced ).elementsByTagName( QStringLiteral( "inner" ) ).count() ), 0 );
	}
};

QTEST_GUILESS_MAIN( DocumentSectionsTest )
#include "DocumentSectionsTest.moc"
