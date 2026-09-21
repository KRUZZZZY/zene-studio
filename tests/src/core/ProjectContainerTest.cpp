/*
 * ProjectContainerTest.cpp - the `.mmpz` v2 container: shape detection by
 *                            content, and the entry-naming rules that stop a
 *                            section being silently lost. ARCH-4 S2c part 1.
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

#include <utility>
#include <vector>

#include "DawProjectInterchange.h"
#include "ProjectContainer.h"

using namespace lmms;

namespace
{

//! The skeleton every fixture container carries.
const char* const kSkeleton = "<?xml version=\"1.0\"?>\n"
	"<zene-project version=\"31\" type=\"song\"/>\n";

//! A document as this tree writes one - the plain-XML shape.
QByteArray plainXml()
{
	return QByteArray( "<?xml version=\"1.0\"?>\n"
		"<zene-project version=\"31\" type=\"song\">\n"
		"  <head bpm=\"124\"/>\n"
		"</zene-project>\n" );
}

//! Bytes that are none of the three shapes: no ZIP signature, no zlib header,
//! no leading '<'.
QByteArray opaque()
{
	return QByteArray( "\x00\x11\x22\x33\x44\x55\x66\x77", 8 );
}

bool writeBytes( const QString& path, const QByteArray& bytes )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) { return false; }
	const qint64 written = file.write( bytes );
	file.close();
	return written == bytes.size();
}

QByteArray readBytes( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) ) { return QByteArray(); }
	return file.readAll();
}

} // namespace


class ProjectContainerTest : public QObject
{
	Q_OBJECT

private slots:

	/*! The three shapes that ship, plus the two a caller must refuse. Each is
	 *  decided by CONTENT: no input here is named, and none of the assertions
	 *  reads a file name. */
	void theShapeIsDecidedByContentAndNotByTheFileName()
	{
		using projectcontainer::Shape;
		using projectcontainer::shapeOf;

		// Compared as ints: a bare enum class is not a type QTest can print,
		// and a comparison it cannot report is not worth having.
		QCOMPARE( int( shapeOf( QByteArray() ) ), int( Shape::Empty ) );
		QCOMPARE( int( shapeOf( plainXml() ) ), int( Shape::PlainXml ) );
		QCOMPARE( int( shapeOf( qCompress( plainXml() ) ) ), int( Shape::LegacyCompressed ) );
		QCOMPARE( int( shapeOf( opaque() ) ), int( Shape::Unknown ) );

		// A v1 blob is NOT a container. This is the whole of "v1 stays
		// readable": the new reader must not claim it, so the old path is the
		// one that runs.
		QVERIFY( !projectcontainer::isContainer( qCompress( plainXml() ) ) );
		QVERIFY( !projectcontainer::isContainer( plainXml() ) );
		QVERIFY( !projectcontainer::isContainer( opaque() ) );
	}

	/*! Whitespace before the root, and a UTF-8 BOM, are both real - ARCH-4's
	 *  own probe measured them. A test for a literal leading '<' would call
	 *  such a file Unknown and refuse a perfectly readable project. */
	void xmlIsRecognisedAfterABomAndLeadingWhitespace()
	{
		using projectcontainer::Shape;

		QCOMPARE( int( projectcontainer::shapeOf( QByteArray( "\n\t  " ) + plainXml() ) ),
			int( Shape::PlainXml ) );
		QCOMPARE( int( projectcontainer::shapeOf(
			QByteArray( "\xEF\xBB\xBF" ) + plainXml() ) ), int( Shape::PlainXml ) );
		QCOMPARE( int( projectcontainer::shapeOf(
			QByteArray( "\xEF\xBB\xBF\r\n  " ) + plainXml() ) ), int( Shape::PlainXml ) );

		// ...and a BOM with nothing after it is not XML, it is Unknown: the
		// whitespace skip must not walk off the end.
		QCOMPARE( int( projectcontainer::shapeOf( QByteArray( "\xEF\xBB\xBF   " ) ) ),
			int( Shape::Unknown ) );
	}

	/*! The container is the bytes, not the name: the same container is
	 *  recognised whatever the file is called, and a v1 `.mmpz` is not
	 *  recognised however it is named. SPEC-ARCH-4 risk 5 (":589-591"). */
	void theContainerIsTheBytesAndNotTheName()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const std::vector<std::pair<QString, QByteArray>> sections{
			{ projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) ),
				QByteArray( "<track id=\"0\"/>" ) } };
		const QString oddlyNamed = dir.filePath( QStringLiteral( "song.mmp" ) );
		QVERIFY( projectcontainer::writeContainer( oddlyNamed, QByteArray( kSkeleton ),
			sections, nullptr ) );
		QVERIFY( projectcontainer::isContainer( readBytes( oddlyNamed ) ) );

		const QString misnamedBlob = dir.filePath( QStringLiteral( "song.mmpz" ) );
		QVERIFY( writeBytes( misnamedBlob, qCompress( plainXml() ) ) );
		QVERIFY( !projectcontainer::isContainer( readBytes( misnamedBlob ) ) );
		QCOMPARE( int( projectcontainer::shapeOf( readBytes( misnamedBlob ) ) ),
			int( projectcontainer::Shape::LegacyCompressed ) );
	}

	/*! The position is part of the entry name, and it is not decoration: two
	 *  `<track>` sections are two entries, and a caller that asks for a name
	 *  that cannot be an entry gets nothing back rather than an escaped
	 *  spelling the index could not map. */
	void aSectionEntryNameCarriesItsPositionAndRefusesTheUnusable()
	{
		QCOMPARE( projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) ),
			QStringLiteral( "sections/0000-track" ) );
		QCOMPARE( projectcontainer::sectionEntryName( 7, QStringLiteral( "bigclip" ) ),
			QStringLiteral( "sections/0007-bigclip" ) );
		// The names the tree's own sections use.
		QCOMPARE( projectcontainer::sectionEntryName( 1, QStringLiteral( "z:provenance" ) ),
			QStringLiteral( "sections/0001-z:provenance" ) );
		QCOMPARE( projectcontainer::sectionEntryName( 2, QStringLiteral( "audio_fileprocessor" ) ),
			QStringLiteral( "sections/0002-audio_fileprocessor" ) );

		QVERIFY( projectcontainer::sectionEntryName( -1, QStringLiteral( "track" ) ).isEmpty() );
		QVERIFY( projectcontainer::sectionEntryName( 0, QString() ).isEmpty() );
		QVERIFY( projectcontainer::sectionEntryName( 0, QStringLiteral( "a/b" ) ).isEmpty() );
		QVERIFY( projectcontainer::sectionEntryName( 0, QStringLiteral( "a b" ) ).isEmpty() );
		QVERIFY( projectcontainer::sectionEntryName( 0, QStringLiteral( "a<b>" ) ).isEmpty() );
	}

	void aContainerRoundTripsItsSkeletonAndItsSections()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "song.mmpz" ) );

		const QString track = projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) );
		const QString clip = projectcontainer::sectionEntryName( 1, QStringLiteral( "bigclip" ) );
		const std::vector<std::pair<QString, QByteArray>> sections{
			{ track, QByteArray( "<track id=\"0\"/>" ) },
			{ clip, QByteArray( "<bigclip><clip id=\"1\"/></bigclip>" ) } };

		QString error;
		QVERIFY2( projectcontainer::writeContainer( path, QByteArray( kSkeleton ), sections,
			&error ), qPrintable( error ) );

		QCOMPARE( int( projectcontainer::shapeOf( readBytes( path ) ) ),
			int( projectcontainer::Shape::ZipContainer ) );

		std::vector<std::pair<QString, QByteArray>> read;
		QVERIFY2( projectcontainer::readContainer( path, QStringList(), &read, &error ),
			qPrintable( error ) );

		QCOMPARE( int( read.size() ), 3 );
		QCOMPARE( read.at( 0 ).first, projectcontainer::skeletonEntryName() );
		QCOMPARE( read.at( 0 ).second, QByteArray( kSkeleton ) );
		QCOMPARE( read.at( 1 ).first, track );
		QCOMPARE( read.at( 1 ).second, sections.at( 0 ).second );
		QCOMPARE( read.at( 2 ).first, clip );
		QCOMPARE( read.at( 2 ).second, sections.at( 1 ).second );
	}

	/*! A subset read keeps the skeleton - a reader cannot interpret sections
	 *  without it - and returns the kept entries in the container's own order.
	 *
	 *  The LIMIT is asserted too, so the header's honest claim cannot drift
	 *  into an overclaim: this saves PARSING, and the entries it did not ask
	 *  for were still read and copied by the reader underneath. */
	void aSubsetReadKeepsTheSkeletonAndReturnsTheKeptInOrder()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "song.mmpz" ) );

		const QString track = projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) );
		const QString clip = projectcontainer::sectionEntryName( 1, QStringLiteral( "bigclip" ) );
		const QString last = projectcontainer::sectionEntryName( 2, QStringLiteral( "automation" ) );
		const std::vector<std::pair<QString, QByteArray>> sections{
			{ track, QByteArray( "<track/>" ) },
			{ clip, QByteArray( "<bigclip/>" ) },
			{ last, QByteArray( "<automation/>" ) } };

		QString error;
		QVERIFY2( projectcontainer::writeContainer( path, QByteArray( kSkeleton ), sections,
			&error ), qPrintable( error ) );

		std::vector<std::pair<QString, QByteArray>> kept;
		QVERIFY2( projectcontainer::readContainer( path, QStringList{ track, last }, &kept,
			&error ), qPrintable( error ) );

		QCOMPARE( int( kept.size() ), 3 );
		QCOMPARE( kept.at( 0 ).first, projectcontainer::skeletonEntryName() );
		QCOMPARE( kept.at( 1 ).first, track );
		QCOMPARE( kept.at( 2 ).first, last );
	}

	/*! An entry name that repeats is a section silently lost - one body for
	 *  two index rows - so it is refused, and the refusal leaves NO FILE
	 *  BEHIND rather than a half-written project. */
	void collidingSectionNamesAreRefusedRatherThanMerged()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "song.mmpz" ) );

		const QString name = projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) );
		const std::vector<std::pair<QString, QByteArray>> sections{
			{ name, QByteArray( "<track id=\"0\"/>" ) },
			{ name, QByteArray( "<track id=\"1\"/>" ) } };

		QString error;
		QVERIFY( !projectcontainer::writeContainer( path, QByteArray( kSkeleton ), sections,
			&error ) );
		QVERIFY( error.contains( QStringLiteral( "share the container entry name" ) ) );
		QVERIFY( !QFile::exists( path ) );
	}

	/*! A container with no sections gains nothing over the v1 blob and would
	 *  grow every project that has nothing to skip - the opposite of the
	 *  additive rule. Refused, so writing v1 stays a decision. */
	void aContainerWithNoSectionsIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "song.mmpz" ) );

		QString error;
		QVERIFY( !projectcontainer::writeContainer( path, QByteArray( kSkeleton ),
			std::vector<std::pair<QString, QByteArray>>(), &error ) );
		QVERIFY( error.contains( QStringLiteral( "no sections" ) ) );
		QVERIFY( !QFile::exists( path ) );

		// ...and a container with no skeleton is refused too: there would be
		// nothing to name the sections from.
		QString error2;
		const std::vector<std::pair<QString, QByteArray>> one{
			{ projectcontainer::sectionEntryName( 0, QStringLiteral( "track" ) ),
				QByteArray( "<track/>" ) } };
		QVERIFY( !projectcontainer::writeContainer( path, QByteArray(), one, &error2 ) );
		QVERIFY( error2.contains( QStringLiteral( "no document skeleton" ) ) );
		QVERIFY( !QFile::exists( path ) );
	}

	/*! A ZIP without our skeleton entry is not a project. Guessing which entry
	 *  is "the document" is how a DAWproject archive - or any ZIP a user
	 *  picked - is misread, so it is refused by name. */
	void aZipWithoutTheSkeletonIsRefusedByName()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		const QString path = dir.filePath( QStringLiteral( "foreign.mmpz" ) );

		// A real DAWproject archive carries project.xml - so the fixture must
		// NOT, or it would be a container this module should accept, and the
		// slot would be asserting its own fixture back.
		const std::vector<interchange::DawProjectEntry> entries{
			{ QStringLiteral( "metadata.xml" ), QByteArray( "<x/>" ) } };
		const QString other = dir.filePath( QStringLiteral( "other.zip" ) );
		QString error;
		QVERIFY2( interchange::dawProjectZipWrite( other, entries, &error ), qPrintable( error ) );
		QVERIFY( writeBytes( path, readBytes( other ) ) );

		std::vector<std::pair<QString, QByteArray>> read;
		QString readError;
		QVERIFY( !projectcontainer::readContainer( path, QStringList(), &read, &readError ) );
		QVERIFY( readError.contains( projectcontainer::skeletonEntryName() ) );
		QVERIFY( read.empty() );
	}

	/*! No destination is a programming error, and it is refused rather than
	 *  dereferenced. */
	void aNullDestinationIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );
		QString error;
		QVERIFY( !projectcontainer::readContainer(
			dir.filePath( QStringLiteral( "song.mmpz" ) ), QStringList(), nullptr, &error ) );
		QVERIFY( !error.isEmpty() );
	}
};

QTEST_GUILESS_MAIN( ProjectContainerTest )
#include "ProjectContainerTest.moc"
