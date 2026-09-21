/*
 * ProjectContainer.cpp - the `.mmpz` v2 container. See ProjectContainer.h for
 *                        why it exists and what it deliberately is not.
 *                        SPEC-ARCH-4 5.1 (S2c). ARCH-4 S2c.
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

#include "ProjectContainer.h"

#include "DawProjectInterchange.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace lmms
{

namespace projectcontainer
{

QString skeletonEntryName()
{
	// The one entry every container carries, and the one a reader is allowed
	// to assume. Named after the DAWproject convention this tree already reads
	// for the same job (DawProjectZip.cpp:5-6: "project.xml, metadata.xml"),
	// so a tool that already knows that format recognises this one.
	return QStringLiteral( "project.xml" );
}


namespace
{

/*! True when \a bytes open with a ZIP local file header signature.
 *
 *  The signature is 0x04034B50 and is stored little-endian, so the bytes on
 *  disk are "PK\3\4". This is a decision about the first four bytes only -
 *  everything after them is the reader's to validate, and it validates all of
 *  it (DawProjectZip.cpp bounds-checks every offset and size, and refuses
 *  DEFLATE, encryption, spanning and a bad CRC each by name). */
bool opensWithZipLocalHeader( const QByteArray& bytes )
{
	return bytes.size() >= 4
		&& bytes.at( 0 ) == 'P' && bytes.at( 1 ) == 'K'
		&& bytes.at( 2 ) == '\x03' && bytes.at( 3 ) == '\x04';
}


/*! The uncompressed length qCompress() prefixes its stream with, or 0 when the
 *  bytes do not carry one. Big-endian, and bounded to the same ceiling
 *  tools/mmpz-git/mmpz_git.py:64-67 uses - a declared length is untrusted input
 *  and a 2 GiB one is a claim, not a buffer to allocate. */
quint32 declaredLegacyLength( const QByteArray& bytes )
{
	const auto* raw = reinterpret_cast<const unsigned char*>( bytes.constData() );
	return ( static_cast<quint32>( raw[0] ) << 24 )
		| ( static_cast<quint32>( raw[1] ) << 16 )
		| ( static_cast<quint32>( raw[2] ) << 8 )
		| static_cast<quint32>( raw[3] );
}


/*! True when \a bytes are Qt's qCompress() framing: a 4-byte big-endian
 *  uncompressed length, then a zlib stream.
 *
 *  Recognised from the zlib header rather than by inflating: a zlib stream
 *  begins with CMF (compression method 8 = DEFLATE, window 32K - so 0x78 for
 *  every stream Qt can write) and FLG, and the two bytes as one big-endian
 *  value are a multiple of 31 by the format's own definition. That is two
 *  comparisons and no allocation, which matters because this runs on a payload
 *  that may be a 400 MB project. The XML check cannot collide with it (an XML
 *  document starts with '<', 0x3C) and a ZIP cannot either (it starts with
 *  'P', 0x50), so the order shapeOf() tries them in is not load-bearing. */
bool isLegacyCompressed( const QByteArray& bytes )
{
	if( bytes.size() < 6 ) { return false; }
	if( bytes.at( 4 ) != '\x78' ) { return false; }
	const auto header = static_cast<unsigned int>(
		( static_cast<unsigned char>( bytes.at( 4 ) ) << 8 )
		| static_cast<unsigned char>( bytes.at( 5 ) ) );
	if( header % 31u != 0u ) { return false; }

	const quint32 declared = declaredLegacyLength( bytes );
	return declared > 0 && declared <= 0x7FFFFFFFu;
}


/*! True when \a bytes open with the UTF-8 byte-order mark. */
bool hasUtf8Bom( const QByteArray& bytes )
{
	return bytes.size() >= 3
		&& static_cast<unsigned char>( bytes.at( 0 ) ) == 0xEF
		&& static_cast<unsigned char>( bytes.at( 1 ) ) == 0xBB
		&& static_cast<unsigned char>( bytes.at( 2 ) ) == 0xBF;
}


/*! True when \a c is whitespace an XML prolog may carry before its root. */
bool isXmlSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}


/*! True when \a bytes are XML: an optional UTF-8 BOM, optional whitespace, then
 *  `<`.
 *
 *  Whitespace and the BOM are not hypothetical. A document written by this tree
 *  can be hand-edited, and ARCH-4's own probe measured both (a BOM plus leading
 *  whitespace before the root): a test for a literal leading `<` would call such
 *  a file Unknown and send it down a refusal path for a file that is perfectly
 *  readable. */
bool beginsWithXml( const QByteArray& bytes )
{
	int at = hasUtf8Bom( bytes ) ? 3 : 0;
	while( at < bytes.size() && isXmlSpace( bytes.at( at ) ) ) { ++at; }
	return at < bytes.size() && bytes.at( at ) == '<';
}


/*! True when \a c is one of the characters a section name or an entry name may
 *  carry: the ASCII alphanumerics, plus the '.', ':', '-' and '_' the tree's
 *  own section names use (`z:provenance`, `ladspa-effect`,
 *  `audio_fileprocessor`). */
bool isAllowedNameChar( QChar c )
{
	if( ( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'z' ) )
		|| ( c >= QLatin1Char( 'A' ) && c <= QLatin1Char( 'Z' ) )
		|| ( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) ) )
	{
		return true;
	}
	return QStringLiteral( ".:-_" ).contains( c );
}


/*! The entry namespace every section entry lives in. A section entry name is
 *  `<namespace><position>-<name>`, and the namespace is required rather than
 *  conventional: it is what stops a section entry from being mistaken for the
 *  skeleton, and what makes "which entry is the project" answerable without
 *  guessing. */
QString sectionNamespace()
{
	return QStringLiteral( "sections/" );
}


/*! True when \a name is usable, non-empty, and built only from
 *  isAllowedNameChar() characters.
 *
 *  \a allowSeparator admits the single '/' that ends the `sections/`
 *  namespace. The two callers need the two different answers - a SECTION's own
 *  name never carries a separator, while the ENTRY name built from it always
 *  does - and giving them one flag rather than two near-identical predicates is
 *  what keeps a change to the rule from having to be made twice. */
bool isUsableName( const QString& name, bool allowSeparator )
{
	if( name.isEmpty() ) { return false; }
	for( const QChar c : name )
	{
		if( c == QLatin1Char( '/' ) )
		{
			if( !allowSeparator ) { return false; }
			continue;
		}
		if( !isAllowedNameChar( c ) ) { return false; }
	}
	return true;
}


void setError( QString* error, const QString& message )
{
	if( error ) { *error = message; }
}

} // namespace


Shape shapeOf( const QByteArray& bytes )
{
	if( bytes.isEmpty() ) { return Shape::Empty; }
	if( opensWithZipLocalHeader( bytes ) ) { return Shape::ZipContainer; }
	if( isLegacyCompressed( bytes ) ) { return Shape::LegacyCompressed; }
	if( beginsWithXml( bytes ) ) { return Shape::PlainXml; }
	return Shape::Unknown;
}


bool isContainer( const QByteArray& bytes )
{
	return shapeOf( bytes ) == Shape::ZipContainer;
}


QString sectionEntryName( int index, const QString& sectionName )
{
	if( index < 0 ) { return QString(); }
	if( !isUsableName( sectionName, false ) ) { return QString(); }

	// The position comes first so that a container's entries sort in the
	// document's own order even when a foreign tool lists them, and so that
	// two sections with the SAME name stay two entries.
	return sectionNamespace()
		+ QStringLiteral( "%1-%2" ).arg( index, 4, 10, QLatin1Char( '0' ) )
			.arg( sectionName );
}


bool writeContainer( const QString& path, const QByteArray& skeleton,
	const std::vector<std::pair<QString, QByteArray>>& sections, QString* error )
{
	if( path.isEmpty() )
	{
		setError( error, QStringLiteral( "the container path is empty" ) );
		return false;
	}
	if( skeleton.isEmpty() )
	{
		setError( error, QStringLiteral(
			"the container has no document skeleton, so there would be nothing "
			"to name its sections from" ) );
		return false;
	}
	if( sections.empty() )
	{
		// Not a validation detail. A container with no sections carries
		// nothing a v1 blob does not, and writing one would grow every project
		// that has no skippable section - the opposite of the additive rule
		// SPEC-ARCH-4 risk 2 (:570-575) calls a release invariant. A caller
		// with nothing to skip writes v1, and this refusal is what makes that
		// a decision instead of an accident.
		setError( error, QStringLiteral(
			"the container has no sections, so a v1 file is the honest shape: "
			"refusing to write a container that gains nothing" ) );
		return false;
	}

	std::vector<interchange::DawProjectEntry> entries;
	entries.push_back( { skeletonEntryName(), skeleton } );

	QStringList taken;
	taken.append( skeletonEntryName() );
	for( const std::pair<QString, QByteArray>& section : sections )
	{
		if( !section.first.startsWith( sectionNamespace() )
			|| !isUsableName( section.first, true ) )
		{
			setError( error, QStringLiteral( "'%1' is not usable as a section's "
				"container entry name: a section entry must be named "
				"'%2<position>-<section name>' by sectionEntryName()" )
				.arg( section.first, sectionNamespace() ) );
			return false;
		}
		if( taken.contains( section.first ) )
		{
			// Two sections sharing an entry name is a section silently lost -
			// the container would hold one body for two index rows. Refused
			// rather than de-duplicated, because a caller that produced
			// colliding names has a bug this must not hide.
			setError( error, QStringLiteral( "two sections share the container entry "
				"name '%1', which would lose one of them" ).arg( section.first ) );
			return false;
		}
		taken.append( section.first );
		entries.push_back( section );
	}

	QString zipError;
	if( !interchange::dawProjectZipWrite( path, entries, &zipError ) )
	{
		setError( error, zipError );
		return false;
	}
	return true;
}


bool readContainer( const QString& path, const QStringList& keepEntries,
	std::vector<std::pair<QString, QByteArray>>* entries, QString* error )
{
	if( entries == nullptr )
	{
		setError( error, QStringLiteral( "no destination for the container's entries" ) );
		return false;
	}

	const QString skeleton = skeletonEntryName();
	std::vector<interchange::DawProjectEntry> all;
	if( !interchange::dawProjectZipRead( path, &all, error ) ) { return false; }

	const bool keepEverything = keepEntries.isEmpty();
	bool sawSkeleton = false;
	entries->clear();
	for( const interchange::DawProjectEntry& entry : all )
	{
		if( entry.first == skeleton ) { sawSkeleton = true; }
		if( keepEverything || entry.first == skeleton || keepEntries.contains( entry.first ) )
		{
			entries->push_back( entry );
		}
	}

	if( !sawSkeleton )
	{
		// A ZIP without our skeleton is not a project. Guessing which entry is
		// "the document" is how a DAWproject archive - or any other ZIP a user
		// picked - gets misread as a project, so it is refused by name.
		entries->clear();
		setError( error, QStringLiteral( "%1 is a ZIP container but carries no '%2' "
			"entry, so it is not a v2 project" ).arg( path, skeleton ) );
		return false;
	}
	return true;
}

} // namespace projectcontainer

} // namespace lmms
