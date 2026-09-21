/*
 * DocumentIndex.cpp - the <z:index> a project document carries. ARCH-4 S2a.
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

#include "DocumentIndex.h"

#include "DawProjectInterchange.h"

#include <QDomAttr>
#include <QDomNamedNodeMap>
#include <QPair>
#include <QTextStream>
#include <QVector>

#include <algorithm>

namespace lmms
{

namespace
{

/*! The digest has to be recomputable by a reader in ANY process, so the section
 *  is written in a CANONICAL form rather than in the writer's own rendering.
 *
 *  That is not a nicety, and it is not a preference: it is the correction of a
 *  measured defect. QDomElement::save() emits an element's attributes in the
 *  order of Qt's attribute QHash, and Qt seeds qHash() RANDOMLY, PER PROCESS.
 *  Measured on this tree, 2026-09-21, with stand-alone Qt6 probes and no LMMS
 *  code in them:
 *
 *   - five processes parsing one unchanged file and re-saving one <track>
 *     element produced five different attribute orders;
 *   - a six-attribute element built through setAttribute() and the same element
 *     built by the parser agreed WITH EACH OTHER inside one process and
 *     disagreed with every other process;
 *   - QHashSeed::setDeterministicGlobalSeed() pinned the order across
 *     processes, which is what identifies the seed as the variable -
 *     QT_HASH_SEED is ignored by Qt6, and the seed it reports really is
 *     per-process random.
 *
 *  So save() is a function of the PROCESS, not of the document: two processes
 *  saving the same project write byte-different files carrying the same
 *  content. A digest taken over that rendering is not a digest - the reader
 *  that recomputes it gets a different value - and it is what made
 *  tests/src/core/DocumentIndexTest.cpp's integration slot fail on roughly one
 *  run in fifteen, on <trackcontainer>, while the file's own bytes and the
 *  writer's recorded digest agreed on every run.
 *
 *  The canonical form deletes the only source of process dependence: attributes
 *  are emitted sorted by name. Everything else is the shape the project writer
 *  uses - indent 2 per level, the node at one level, an element with no
 *  significant child self-closed - so the digest is still "this section as a
 *  document", it is just a document that every process agrees on. */
bool isInsignificantText( const QDomNode & node )
{
	if( !node.isText() && !node.isCDATASection() ) { return false; }
	return node.nodeValue().trimmed().isEmpty();
}

/*! Whether \a element has a child the digest must describe. Whitespace-only
 *  text does not count: a parser-built DOM carries the document's indentation
 *  as text nodes and a setter-built one does not, and the writer regenerates
 *  indentation anyway, so counting it would put the two paths back at odds. */
bool hasSignificantChildren( const QDomElement & element )
{
	for( QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling() )
	{
		if( node.isElement() ) { return true; }
		if( ( node.isText() || node.isCDATASection() ) && !isInsignificantText( node ) )
		{
			return true;
		}
	}
	return false;
}

QString escapeText( const QString & text )
{
	QString out = text;
	out.replace( QLatin1Char( '&' ), QLatin1String( "&amp;" ) );
	out.replace( QLatin1Char( '<' ), QLatin1String( "&lt;" ) );
	out.replace( QLatin1Char( '>' ), QLatin1String( "&gt;" ) );
	return out;
}

QString escapeAttribute( const QString & value )
{
	QString out = escapeText( value );
	out.replace( QLatin1Char( '"' ), QLatin1String( "&quot;" ) );
	out.replace( QLatin1Char( '\n' ), QLatin1String( "&#xa;" ) );
	out.replace( QLatin1Char( '\r' ), QLatin1String( "&#xd;" ) );
	out.replace( QLatin1Char( '\t' ), QLatin1String( "&#x9;" ) );
	return out;
}

void writeCanonical( QTextStream & stream, const QDomElement & element, int level );

/*! The children, each on its own indented line. Comments and processing
 *  instructions do not participate: the digest addresses the content a reader
 *  would load, and neither is loaded. Character data participates TRIMMED, which
 *  is not tidiness but the same rule as whitespace-only text: a parser-built DOM
 *  carries a run's surrounding indentation inside the text node and a
 *  setter-built one does not, so the two paths would otherwise disagree about
 *  the same content. Whitespace INSIDE the text is content and survives. */
void writeCanonicalChildren( QTextStream & stream, const QDomElement & element, int level )
{
	for( QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling() )
	{
		if( node.isElement() )
		{
			writeCanonical( stream, node.toElement(), level );
		}
		else if( ( node.isText() || node.isCDATASection() ) && !isInsignificantText( node ) )
		{
			stream << QString( level * 2, QLatin1Char( ' ' ) )
				<< escapeText( node.nodeValue().trimmed() ) << QLatin1Char( '\n' );
		}
	}
}

void writeCanonical( QTextStream & stream, const QDomElement & element, int level )
{
	const QString indent( level * 2, QLatin1Char( ' ' ) );
	stream << indent << QLatin1Char( '<' ) << element.nodeName();

	QVector<QPair<QString, QString>> attributes;
	const QDomNamedNodeMap map = element.attributes();
	attributes.reserve( map.length() );
	for( int i = 0; i < map.length(); ++i )
	{
		const QDomAttr attribute = map.item( i ).toAttr();
		attributes.append( qMakePair( attribute.name(), attribute.value() ) );
	}
	std::sort( attributes.begin(), attributes.end(),
		[]( const QPair<QString, QString> & a, const QPair<QString, QString> & b )
		{
			return a.first < b.first;
		} );
	for( const QPair<QString, QString> & attribute : attributes )
	{
		stream << QLatin1Char( ' ' ) << attribute.first << QLatin1String( "=\"" )
			<< escapeAttribute( attribute.second ) << QLatin1Char( '"' );
	}

	if( !hasSignificantChildren( element ) )
	{
		stream << QLatin1String( "/>\n" );
		return;
	}

	stream << QLatin1String( ">\n" );
	writeCanonicalChildren( stream, element, level + 1 );
	stream << indent << QLatin1String( "</" ) << element.nodeName() << QLatin1String( ">\n" );
}

/*! The element that is never a section: every reader loads it, so no reader can
 *  skip it, and digesting it would claim otherwise. It is a child of the ROOT
 *  element while sections are children of the content element, so the walk over
 *  a content element cannot meet one - the test is here so that a caller who
 *  passes the root by mistake indexes the content element as a section rather
 *  than indexing the head as one. */
QString headNodeName()
{
	return QStringLiteral( "head" );
}

/*! The element the format sketch reserves for preserved foreign content. It is
 *  addressed by document PATH (SPEC-ARCH-4 1.6.1), not by section name, so it is
 *  not a section. No slice writes one yet; the rule is stated here because the
 *  element that would be indexed wrongly is exactly the one this file exists to
 *  describe, and a rule discovered after the fact is a rule that was not a rule. */
QString unclaimedNodeName()
{
	return QStringLiteral( "z:unclaimed" );
}

} // namespace

QString documentIndexNodeName()
{
	return QStringLiteral( "z:index" );
}

QString documentIndexSectionNodeName()
{
	return QStringLiteral( "z:section" );
}

QString documentIndexNamespaceAttribute()
{
	return QStringLiteral( "xmlns:z" );
}

QString documentIndexNamespaceUri()
{
	return QStringLiteral( "urn:zene:core:1" );
}

const DocumentSection* DocumentIndex::section( const QString & name ) const
{
	for( const DocumentSection & candidate : sections )
	{
		if( candidate.name == name ) { return &candidate; }
	}
	return nullptr;
}

QString sectionDigest( const QDomElement & section )
{
	QString xml;
	{
		QTextStream stream( &xml );
		// Level 1, not 0: the node sits at one level, which is what makes the
		// value independent of where in a document the section sits.
		writeCanonical( stream, section, 1 );
		stream.flush();
	}
	// The interchange module's digest, reused rather than written a second
	// time: a document's section digest and a DAWproject entry digest are the
	// same statement about the same bytes, and two spellings of it could drift
	// apart without either being wrong on its own.
	return QStringLiteral( "sha256:" ) + interchange::dawProjectSha256( xml.toUtf8() );
}

DocumentIndex documentIndex( const QDomElement & content, int documentVersion )
{
	DocumentIndex index;

	for( QDomNode node = content.firstChild(); !node.isNull(); node = node.nextSibling() )
	{
		if( !node.isElement() ) { continue; }

		const QDomElement element = node.toElement();
		const QString name = element.nodeName();

		if( name == documentIndexNodeName() ) { continue; }
		if( name == headNodeName() ) { continue; }
		if( name == unclaimedNodeName() ) { continue; }

		DocumentSection entry;
		entry.name = name;
		entry.entry = name + QStringLiteral( ".xml" );
		entry.version = element.attribute( QStringLiteral( "version" ),
			QString::number( documentVersion ) ).toInt();
		entry.digest = sectionDigest( element );
		index.sections.append( entry );
	}

	return index;
}

bool writeDocumentIndex( const DocumentIndex & index, QDomDocument & document, QDomElement & root )
{
	// An empty index is NOT written: the document would still change its bytes,
	// and it would have gained an index that indexes nothing.
	if( index.isEmpty() ) { return false; }

	root.setAttribute( documentIndexNamespaceAttribute(), documentIndexNamespaceUri() );

	QDomElement element = document.createElement( documentIndexNodeName() );
	for( const DocumentSection & section : index.sections )
	{
		QDomElement entry = document.createElement( documentIndexSectionNodeName() );
		entry.setAttribute( QStringLiteral( "name" ), section.name );
		entry.setAttribute( QStringLiteral( "entry" ), section.entry );
		entry.setAttribute( QStringLiteral( "v" ), QString::number( section.version ) );
		entry.setAttribute( QStringLiteral( "digest" ), section.digest );
		element.appendChild( entry );
	}

	// Directly after <head>, the position SPEC-ARCH-4 1.1's format sketch gives
	// it. A document with no <head> cannot be read at all (Song::loadProject
	// refuses one), so the fallback is for a non-song payload rather than for a
	// project.
	const QDomElement head = root.firstChildElement( headNodeName() );
	if( head.isNull() )
	{
		root.insertBefore( element, root.firstChild() );
	}
	else
	{
		root.insertAfter( element, head );
	}

	return true;
}

DocumentIndex parseDocumentIndex( const QDomElement & root )
{
	DocumentIndex index;

	const QDomElement element = root.firstChildElement( documentIndexNodeName() );
	if( element.isNull() ) { return index; }

	for( QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling() )
	{
		if( !node.isElement() ) { continue; }

		const QDomElement entry = node.toElement();
		if( entry.nodeName() != documentIndexSectionNodeName() ) { continue; }

		DocumentSection section;
		section.name = entry.attribute( QStringLiteral( "name" ) );

		// A section that cannot be named cannot be loaded by name, so admitting
		// it would turn a damaged index into a load that silently omits the
		// section it named nothing about.
		if( section.name.isEmpty() ) { continue; }

		section.entry = entry.attribute( QStringLiteral( "entry" ) );
		section.version = entry.attribute( QStringLiteral( "v" ),
			QStringLiteral( "0" ) ).toInt();
		section.digest = entry.attribute( QStringLiteral( "digest" ) );
		index.sections.append( section );
	}

	return index;
}

} // namespace lmms
