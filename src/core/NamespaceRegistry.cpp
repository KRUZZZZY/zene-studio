/*
 * NamespaceRegistry.cpp - the document format's namespace table and resolver
 *                        (ARCH-4 S1a).
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

#include "NamespaceRegistry.h"

namespace lmms
{

namespace
{

const QChar kSeparator = QLatin1Char( ':' );
const QString kVendorPrefix = QStringLiteral( "vendor." );

} // namespace

const QString & NamespaceRegistry::coreNamespace()
{
	static const QString uri = QStringLiteral( "zene:core:1" );
	return uri;
}

const QString & NamespaceRegistry::docNamespace()
{
	static const QString uri = QStringLiteral( "zene:doc:1" );
	return uri;
}

const QString & NamespaceRegistry::guiNamespace()
{
	static const QString uri = QStringLiteral( "zene:gui:1" );
	return uri;
}

QString NamespaceRegistry::vendorNamespace( const QString & domain, int version )
{
	if( domain.isEmpty() || version < 1 ) { return QString(); }
	if( domain.contains( kSeparator ) ) { return QString(); }
	if( domain.contains( QLatin1Char( ' ' ) ) || domain.contains( QLatin1Char( '\t' ) ) )
	{
		return QString();
	}
	return kVendorPrefix + domain + kSeparator + QString::number( version );
}

QStringList NamespaceRegistry::knownNamespaces()
{
	return { coreNamespace(), docNamespace(), guiNamespace() };
}

bool NamespaceRegistry::isKnownNamespace( const QString & uri )
{
	return uri == coreNamespace() || uri == docNamespace() || uri == guiNamespace();
}

bool NamespaceRegistry::isVendorNamespace( const QString & uri )
{
	if( !uri.startsWith( kVendorPrefix ) ) { return false; }
	const int colon = uri.lastIndexOf( kSeparator );
	if( colon <= kVendorPrefix.size() ) { return false; }
	if( uri.mid( kVendorPrefix.size(), colon - kVendorPrefix.size() ).isEmpty() )
	{
		return false;
	}
	bool versionOk = false;
	const int version = uri.mid( colon + 1 ).toInt( &versionOk );
	return versionOk && version >= 1;
}

QStringList NamespaceRegistry::legacyGuiSections()
{
	/* The five GUI-owned sections the writer has always emitted unprefixed
	 * (Song::loadProject's last branch, src/core/Song.cpp:1602-1624). The four
	 * are the nodeName() literals themselves; the first is measured from the
	 * tree - ControllerRackView::nodeName() returns "ControllerRackView"
	 * (include/ControllerRackView.h:57-60), which is how SPEC-ARCH-4's prose
	 * list spells "controllerRack". The literal wins; see the test. */
	static const QStringList sections = {
		QStringLiteral( "ControllerRackView" ),
		QStringLiteral( "pianoroll" ),
		QStringLiteral( "automationeditor" ),
		QStringLiteral( "projectnotes" ),
		QStringLiteral( "timeline" ),
	};
	return sections;
}

const QString & NamespaceRegistry::legacyNamespace( const QString & name )
{
	if( legacyGuiSections().contains( name ) ) { return guiNamespace(); }
	return coreNamespace();
}

NamespaceRegistry::NamespaceRegistry()
{
	// SPEC-ARCH-4 1.3: the default prefix a namespaced document declares is `z`.
	bindPrefix( QStringLiteral( "z" ), coreNamespace() );
}

bool NamespaceRegistry::bindPrefix( const QString & prefix, const QString & uri )
{
	if( prefix.isEmpty() || uri.isEmpty() ) { return false; }
	if( !m_prefixToUri.contains( prefix ) ) { m_prefixOrder.append( prefix ); }
	m_prefixToUri.insert( prefix, uri );
	return true;
}

bool NamespaceRegistry::hasPrefix( const QString & prefix ) const
{
	return m_prefixToUri.contains( prefix );
}

QString NamespaceRegistry::uriForPrefix( const QString & prefix ) const
{
	return m_prefixToUri.value( prefix );
}

QString NamespaceRegistry::prefixForUri( const QString & uri ) const
{
	if( uri.isEmpty() ) { return QString(); }
	for( const QString & prefix : m_prefixOrder )
	{
		if( m_prefixToUri.value( prefix ) == uri ) { return prefix; }
	}
	return QString();
}

NamespaceRegistry::ResolvedName NamespaceRegistry::resolve( const QString & name ) const
{
	ResolvedName resolved;
	const int colon = name.indexOf( kSeparator );
	if( colon < 0 )
	{
		// The compatibility table: a legacy document's names are unprefixed. An
		// empty name is not one of them - it names no local part and no
		// namespace, and nothing may claim it.
		resolved.localName = name;
		resolved.namespaceUri = name.isEmpty() ? QString() : legacyNamespace( name );
		resolved.claimed = !resolved.namespaceUri.isEmpty();
		return resolved;
	}

	resolved.namespaced = true;
	resolved.prefix = name.left( colon );
	resolved.localName = name.mid( colon + 1 );
	resolved.namespaceUri = uriForPrefix( resolved.prefix );
	// An unbound prefix (empty uriForPrefix) is unclaimed, and so is a name with
	// no local part: neither is a name this build can claim to handle.
	resolved.claimed = !resolved.localName.isEmpty()
		&& isKnownNamespace( resolved.namespaceUri );
	return resolved;
}

QString NamespaceRegistry::qualify( const QString & uri, const QString & localName ) const
{
	const QString prefix = prefixForUri( uri );
	if( prefix.isEmpty() || localName.isEmpty() ) { return QString(); }
	return prefix + kSeparator + localName;
}

} // namespace lmms
