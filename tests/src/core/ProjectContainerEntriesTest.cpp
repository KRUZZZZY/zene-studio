/*
 * ProjectContainerEntriesTest.cpp - the `.mmpz` v2 deriver and reassembler:
 *                                   document -> entries -> document, with the
 *                                   raw byte-identity round trip this pair
 *                                   promises and the refusals that keep the
 *                                   promise true. ARCH-4 S2c part 2.
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
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <utility>
#include <vector>

#include "ProjectContainer.h"

using namespace lmms;

namespace
{

const char* const kContent = "song";

//! The index rows for \a names exactly as documentIndex() records them -
//! `entry` as `name + ".xml"`, positionless. The deriver must NOT key on it;
//! every fixture below writes it so a deriver that did would be caught.
QString indexRows( const QStringList& names )
{
	QString rows;
	for( int i = 0; i < names.size(); ++i )
	{
		rows += QStringLiteral( "    <z:section name=\"%1\" entry=\"%1.xml\" v=\"31\" "
			"digest=\"sha256:x%2\"/>\n" ).arg( names.at( i ) ).arg( i );
	}
	return rows;
}

//! A document carrying an index over \a names, with \a songChildren as the
//! content element's children (each line already indented, each ending in a
//! newline) - the shape this tree writes: root, head, index, then <song>.
QByteArray documentWith( const QStringList& names, const QString& songChildren )
{
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"31\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head bpm=\"124\"/>\n"
		"  <z:index xmlns:z=\"urn:zene:core:1\">\n%1"
		"  </z:index>\n"
		"  <song>\n%2"
		"  </song>\n"
		"</lmms-project>\n" ).arg( indexRows( names ), songChildren ).toUtf8();
}

//! The two-section document most slots use: a multi-line section, then a
//! self-closing one, so both end-boundaries are exercised.
QByteArray twoSectionDocument()
{
	return documentWith( QStringList{ QStringLiteral( "trackcontainer" ),
			QStringLiteral( "tempo-map" ) },
		QStringLiteral(
			"    <trackcontainer type=\"song\">\n"
			"      <track name=\"a\"/>\n"
			"    </trackcontainer>\n"
			"    <tempo-map v=\"1\"/>\n" ) );
}

//! Bytes the destination holds before a refusal must leave them exactly so.
std::vector<std::pair<QString, QByteArray>> sentinelEntries()
{
	return { { QStringLiteral( "sentinel.xml" ), QByteArray( "untouched" ) } };
}

QByteArray readBytes( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return QByteArray(); }
	return file.readAll();
}

} // namespace


class ProjectContainerEntriesTest : public QObject
{
	Q_OBJECT

private slots:

	/*! The split itself: the skeleton first, then one pair per index row in
	 *  index order, keyed by sectionEntryName() - NOT by the index's recorded
	 *  `entry` attribute, which every fixture writes as `name + ".xml"` so a
	 *  deriver that keyed on it would produce `trackcontainer.xml` here and be
	 *  caught. The section entries carry the element AND the whitespace that
	 *  followed it - the mechanism the byte-identity round trip rests on - so
	 *  the literals below pin that trailing whitespace explicitly. */
	void aDocumentDerivesIntoASkeletonThenSectionsInIndexOrder()
	{
		std::vector<std::pair<QString, QByteArray>> derived;
		QString error;
		QVERIFY2( projectcontainer::deriveContainerEntries( twoSectionDocument(),
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );

		QCOMPARE( int( derived.size() ), 3 );
		QCOMPARE( derived.at( 0 ).first, projectcontainer::skeletonEntryName() );
		QCOMPARE( derived.at( 1 ).first,
			QStringLiteral( "sections/0000-trackcontainer" ) );
		QCOMPARE( derived.at( 2 ).first, QStringLiteral( "sections/0001-tempo-map" ) );

		const QByteArray skeleton = derived.at( 0 ).second;
		QVERIFY( skeleton.contains( "<head" ) );
		QVERIFY( skeleton.contains( "<z:index" ) );
		QVERIFY( !skeleton.contains( "<trackcontainer" ) );
		QVERIFY( !skeleton.contains( "<tempo-map" ) );

		QCOMPARE( derived.at( 1 ).second, QByteArray(
			"<trackcontainer type=\"song\">\n"
			"      <track name=\"a\"/>\n"
			"    </trackcontainer>\n    " ) );
		QCOMPARE( derived.at( 2 ).second, QByteArray( "<tempo-map v=\"1\"/>\n  " ) );
	}

	/*! THE round trip, and the guarantee it proves: RAW byte identity - the
	 *  rebuilt document equals the original byte for byte, not merely in
	 *  canonical form. Chosen deliberately (see deriveContainerEntries in
	 *  ProjectContainer.h): the spec corrected its own "byte identity" proofs
	 *  to canonical identity because re-serialisation through Qt's save() is
	 *  per-process random (SPEC-ARCH-4 :544-557), and this path never
	 *  re-serialises, so the raw comparison cannot flake for that reason. Two
	 *  fixtures: the indented one, where every separator lives in an entry; and
	 *  a compact one with NO whitespace between sections, where the splice must
	 *  invent nothing. */
	void theDerivedEntriesReassembleIntoTheOriginalBytesRaw()
	{
		const QByteArray original = twoSectionDocument();
		std::vector<std::pair<QString, QByteArray>> derived;
		QString error;
		QVERIFY2( projectcontainer::deriveContainerEntries( original,
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );

		QByteArray rebuilt;
		QVERIFY2( projectcontainer::reassembleContainerDocument( derived,
			QString::fromLatin1( kContent ), &rebuilt, &error ), qPrintable( error ) );
		QCOMPARE( rebuilt, original );

		const QByteArray compact =
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"31\" type=\"song\"><head/>"
			"<z:index xmlns:z=\"urn:zene:core:1\">"
			"<z:section name=\"a\" entry=\"a.xml\" v=\"31\" digest=\"sha256:x0\"/>"
			"<z:section name=\"b\" entry=\"b.xml\" v=\"31\" digest=\"sha256:x1\"/>"
			"</z:index><song><a/><b/></song></lmms-project>\n";
		std::vector<std::pair<QString, QByteArray>> compactDerived;
		QVERIFY2( projectcontainer::deriveContainerEntries( compact,
			QString::fromLatin1( kContent ), &compactDerived, &error ), qPrintable( error ) );
		QCOMPARE( compactDerived.at( 1 ).second, QByteArray( "<a/>" ) );

		QByteArray compactRebuilt;
		QVERIFY2( projectcontainer::reassembleContainerDocument( compactDerived,
			QString::fromLatin1( kContent ), &compactRebuilt, &error ), qPrintable( error ) );
		QCOMPARE( compactRebuilt, compact );
	}

	/*! A document with no `<z:index>` declares no section, so there is nothing
	 *  to derive - refused with the destination left exactly as passed, not
	 *  cleared: refusal must look like "nothing happened", never like an empty
	 *  success. */
	void derivingRefusesADocumentWithNoIndex()
	{
		QByteArray noIndex =
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"31\" type=\"song\">\n"
			"  <head bpm=\"124\"/>\n"
			"  <song>\n"
			"    <trackcontainer/>\n"
			"  </song>\n"
			"</lmms-project>\n";

		auto entries = sentinelEntries();
		QString error;
		QVERIFY( !projectcontainer::deriveContainerEntries( noIndex,
			QString::fromLatin1( kContent ), &entries, &error ) );
		QVERIFY( error.contains( QStringLiteral( "no <z:index>" ) ) );
		QCOMPARE( int( entries.size() ), 1 );
		QCOMPARE( entries.at( 0 ).first, QStringLiteral( "sentinel.xml" ) );
		QCOMPARE( entries.at( 0 ).second, QByteArray( "untouched" ) );
	}

	/*! A `<z:section>` with no name: parseDocumentIndex() skips such a row by
	 *  contract, which is right for a reader selecting sections by name and
	 *  WRONG for a deriver, because a dropped row silently shifts every later
	 *  row's position - and the position is the container's key. The declared
	 *  row count against the surviving row count is what turns that shift into
	 *  a refusal. */
	void derivingRefusesANamelessIndexRow()
	{
		QByteArray nameless =
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"31\" type=\"song\">\n"
			"  <head/>\n"
			"  <z:index xmlns:z=\"urn:zene:core:1\">\n"
			"    <z:section entry=\"x.xml\" digest=\"sha256:x\"/>\n"
			"    <z:section name=\"trackcontainer\" entry=\"trackcontainer.xml\" "
			"v=\"31\" digest=\"sha256:y\"/>\n"
			"  </z:index>\n"
			"  <song>\n"
			"    <trackcontainer/>\n"
			"  </song>\n"
			"</lmms-project>\n";

		auto entries = sentinelEntries();
		QString error;
		QVERIFY( !projectcontainer::deriveContainerEntries( nameless,
			QString::fromLatin1( kContent ), &entries, &error ) );
		QVERIFY( error.contains( QStringLiteral( "with no name" ) ) );
		QCOMPARE( int( entries.size() ), 1 );
	}

	/*! A section name sectionEntryName() cannot file - here a non-ASCII element
	 *  name, which XML allows and this container's entry alphabet does not.
	 *  Refused, not escaped: an entry name the container cannot round-trip is a
	 *  name the container would lose. */
	void derivingRefusesASectionNameTheContainerCannotFile()
	{
		// UTF-8 bytes for `<café/>` - an XML-legal element name.
		QByteArray unfilable =
			"<?xml version=\"1.0\"?>\n"
			"<lmms-project version=\"31\" type=\"song\">\n"
			"  <head/>\n"
			"  <z:index xmlns:z=\"urn:zene:core:1\">\n"
			"    <z:section name=\"caf\xC3\xA9\" entry=\"x.xml\" v=\"31\" "
			"digest=\"sha256:x\"/>\n"
			"  </z:index>\n"
			"  <song>\n"
			"    <caf\xC3\xA9/>\n"
			"  </song>\n"
			"</lmms-project>\n";

		auto entries = sentinelEntries();
		QString error;
		QVERIFY( !projectcontainer::deriveContainerEntries( unfilable,
			QString::fromLatin1( kContent ), &entries, &error ) );
		QVERIFY( error.contains( QStringLiteral( "cannot be filed" ) ) );
		QCOMPARE( int( entries.size() ), 1 );
	}

	/*! A child of the content element the index does NOT name: the row count
	 *  and the order still match (the scan only looks for indexed names), so
	 *  this is caught by the interior check - after the cut, something other
	 *  than whitespace sits where the splice assumes gaps only. Splicing there
	 *  would move the sections past the unindexed child, so the refusal is what
	 *  keeps byte identity true rather than probable. */
	void derivingRefusesAChildTheIndexDoesNotName()
	{
		const QByteArray unindexed = documentWith(
			QStringList{ QStringLiteral( "trackcontainer" ), QStringLiteral( "tempo-map" ) },
			QStringLiteral(
				"    <trackcontainer/>\n"
				"    <the-index-does-not-know-me a=\"1\"/>\n"
				"    <tempo-map v=\"1\"/>\n" ) );

		auto entries = sentinelEntries();
		QString error;
		QVERIFY( !projectcontainer::deriveContainerEntries( unindexed,
			QString::fromLatin1( kContent ), &entries, &error ) );
		QVERIFY( error.contains( QStringLiteral( "not whitespace-only" ) ) );
		QCOMPARE( int( entries.size() ), 1 );
	}

	/*! No skeleton, no document: a pair list without the skeleton entry is not
	 *  a v2 project, refused by name rather than guessed at - and the caller's
	 *  destination keeps its bytes. */
	void reassemblingRefusesEntriesWithoutTheSkeleton()
	{
		QString error;
		std::vector<std::pair<QString, QByteArray>> derived;
		QVERIFY2( projectcontainer::deriveContainerEntries( twoSectionDocument(),
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );
		derived.erase( derived.begin() );

		auto document = QByteArray( "untouched" );
		QVERIFY( !projectcontainer::reassembleContainerDocument( derived,
			QString::fromLatin1( kContent ), &document, &error ) );
		QVERIFY( error.contains( projectcontainer::skeletonEntryName() ) );
		QCOMPARE( document, QByteArray( "untouched" ) );
	}

	/*! Two entries sharing a name is one body for two index rows - a section
	 *  silently lost. Refused rather than de-duplicated, the write side's own
	 *  rule, and the destination stays untouched. */
	void reassemblingRefusesCollidingEntries()
	{
		QString error;
		std::vector<std::pair<QString, QByteArray>> derived;
		QVERIFY2( projectcontainer::deriveContainerEntries( twoSectionDocument(),
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );
		const auto duplicate = derived.at( 1 );
		derived.push_back( duplicate );

		auto document = QByteArray( "untouched" );
		QVERIFY( !projectcontainer::reassembleContainerDocument( derived,
			QString::fromLatin1( kContent ), &document, &error ) );
		QVERIFY( error.contains( QStringLiteral( "share the container entry name" ) ) );
		QCOMPARE( document, QByteArray( "untouched" ) );
	}

	/*! The pair ORDER is free - the position lives inside each entry's name, so
	 *  a reversed list reassembles to the identical bytes - but the POSITIONS
	 *  themselves are pinned to the index: names carrying each other's positions
	 *  are refused, because position is what maps an entry back to an index row
	 *  and a wrong position files a section where another belongs. */
	void entryOrderIsFreeButPositionsMustMatchTheIndex()
	{
		QString error;
		std::vector<std::pair<QString, QByteArray>> derived;
		QVERIFY2( projectcontainer::deriveContainerEntries( twoSectionDocument(),
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );

		auto reversed = derived;
		std::reverse( reversed.begin(), reversed.end() );
		QByteArray rebuilt;
		QVERIFY2( projectcontainer::reassembleContainerDocument( reversed,
			QString::fromLatin1( kContent ), &rebuilt, &error ), qPrintable( error ) );
		QCOMPARE( rebuilt, twoSectionDocument() );

		// The same two names with their positions swapped between them.
		auto shifted = derived;
		shifted.at( 1 ).first = QStringLiteral( "sections/0000-tempo-map" );
		shifted.at( 2 ).first = QStringLiteral( "sections/0001-trackcontainer" );
		auto document = QByteArray( "untouched" );
		QVERIFY( !projectcontainer::reassembleContainerDocument( shifted,
			QString::fromLatin1( kContent ), &document, &error ) );
		QVERIFY( error.contains( QStringLiteral( "the index names the entry" ) ) );
		QCOMPARE( document, QByteArray( "untouched" ) );
	}

	/*! The integration pair, end to end through the REAL writer and reader:
	 *  derive, write the container, read it back (central-directory order),
	 *  reassemble - and the document equals the original RAW. This is the slot
	 *  that says the whole `.mmpz` v2 shape round-trips a document, not just
	 *  the pair in isolation. */
	void theRoundTripSurvivesTheRealContainerPair()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "song.mmpz" ) );

		const QByteArray original = twoSectionDocument();
		QString error;
		std::vector<std::pair<QString, QByteArray>> derived;
		QVERIFY2( projectcontainer::deriveContainerEntries( original,
			QString::fromLatin1( kContent ), &derived, &error ), qPrintable( error ) );

		const std::vector<std::pair<QString, QByteArray>> sections(
			derived.begin() + 1, derived.end() );
		QVERIFY2( projectcontainer::writeContainer( path, derived.at( 0 ).second,
			sections, &error ), qPrintable( error ) );
		QVERIFY( projectcontainer::isContainer( readBytes( path ) ) );

		std::vector<std::pair<QString, QByteArray>> read;
		QVERIFY2( projectcontainer::readContainer( path, QStringList(), &read, &error ),
			qPrintable( error ) );
		QCOMPARE( int( read.size() ), int( derived.size() ) );
		for( int i = 0; i < int( derived.size() ); ++i )
		{
			QCOMPARE( read.at( i ).first, derived.at( i ).first );
		}

		QByteArray rebuilt;
		QVERIFY2( projectcontainer::reassembleContainerDocument( read,
			QString::fromLatin1( kContent ), &rebuilt, &error ), qPrintable( error ) );
		QCOMPARE( rebuilt, original );
	}
};

QTEST_GUILESS_MAIN( ProjectContainerEntriesTest )
#include "ProjectContainerEntriesTest.moc"
