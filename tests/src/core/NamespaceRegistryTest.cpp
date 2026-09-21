/*
 * NamespaceRegistryTest.cpp - ARCH-4 S1a: the document format's namespace table
 *                             and the resolver that reads it.
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
 *
 */

#include "NamespaceRegistry.h"

#include <QtTest>

#include <QFile>
#include <QSet>
#include <QString>
#include <QStringList>

namespace
{

/*! The `nodeName()` literal a GUI section header returns.
 *
 * Extracted rather than restated: the compatibility table's five entries are
 * facts about OTHER files, and a test that repeats them only proves the test and
 * the table were typed twice. This reads the real override - it works for both
 * the `{ return "x"; }` and the `-> QString override { return "x"; }` forms,
 * because it looks for the first `return "` after the `nodeName()` mention.
 */
QString nodeNameLiteralIn( const QString & path, bool * ok )
{
	*ok = false;
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) { return QString(); }
	const QString text = QString::fromUtf8( file.readAll() );

	const int at = text.indexOf( QStringLiteral( "nodeName()" ) );
	if( at < 0 ) { return QString(); }
	const int ret = text.indexOf( QStringLiteral( "return \"" ), at );
	if( ret < 0 ) { return QString(); }
	const int begin = ret + 8; // strlen( "return \"" )
	const int end = text.indexOf( QLatin1Char( '"' ), begin );
	if( end < 0 ) { return QString(); }

	*ok = true;
	return text.mid( begin, end - begin );
}

} // namespace

class NamespaceRegistryTest : public QObject
{
	Q_OBJECT

private slots:
	/*! The format's default binding, and the three namespaces it declares. */
	void theDefaultBindingIsTheFormatsOwn()
	{
		lmms::NamespaceRegistry registry;
		QVERIFY( registry.hasPrefix( QStringLiteral( "z" ) ) );
		QCOMPARE( registry.uriForPrefix( QStringLiteral( "z" ) ),
			lmms::NamespaceRegistry::coreNamespace() );
		QCOMPARE( lmms::NamespaceRegistry::coreNamespace(), QStringLiteral( "zene:core:1" ) );
		QCOMPARE( lmms::NamespaceRegistry::docNamespace(), QStringLiteral( "zene:doc:1" ) );
		QCOMPARE( lmms::NamespaceRegistry::guiNamespace(), QStringLiteral( "zene:gui:1" ) );
	}

	/*! A legacy document's names carry no prefix and must keep resolving - to
	 *  `zene:core:1`, per SPEC-ARCH-4 1.3's compatibility table. */
	void legacyNamesResolveThroughTheCompatibilityTable()
	{
		const QStringList core = {
			QStringLiteral( "trackcontainer" ),
			QStringLiteral( "track" ),
			QStringLiteral( "clip" ),
			QStringLiteral( "note" ),
			QStringLiteral( "controllers" ),
			QStringLiteral( "scales" ),
			QStringLiteral( "session" ),
			QStringLiteral( "somethingNobodyClaimsYet" ),
		};
		lmms::NamespaceRegistry registry;
		for( const QString & name : core )
		{
			const auto resolved = registry.resolve( name );
			QVERIFY2( resolved.claimed, qPrintable( name ) );
			QVERIFY2( !resolved.namespaced, qPrintable( name ) );
			QCOMPARE( resolved.prefix, QString() );
			QCOMPARE( resolved.localName, name );
			QCOMPARE( resolved.namespaceUri, lmms::NamespaceRegistry::coreNamespace() );
		}
	}

	/*! The five GUI-owned sections are the exception in that table: they are
	 *  unprefixed in every document the writer has ever produced, and they belong
	 *  to `zene:gui:1`, not core (SPEC-ARCH-4 1.3). */
	void guiSectionsResolveToTheGuiNamespace()
	{
		lmms::NamespaceRegistry registry;
		const QStringList sections = lmms::NamespaceRegistry::legacyGuiSections();
		QCOMPARE( sections.size(), 5 );
		for( const QString & name : sections )
		{
			const auto resolved = registry.resolve( name );
			QVERIFY2( resolved.claimed, qPrintable( name ) );
			QCOMPARE( resolved.namespaceUri, lmms::NamespaceRegistry::guiNamespace() );
			QCOMPARE( resolved.localName, name );
		}
	}

	/*! The drift check: the compatibility table's five entries must BE the five
	 *  literals the GUI section classes return from nodeName(). If a nodeName()
	 *  changes, or a section is added, this fails and the table must be re-measured
	 *  - which is the negative control that stops the table quietly becoming
	 *  fiction. (The spec's prose spells the controller rack `controllerRack`;
	 *  the tree returns `ControllerRackView` - the tree wins, and this proves it.) */
	void theCompatibilityTableMatchesTheTreesOwnNodeNames()
	{
		const QString root = QStringLiteral( ZENE_ARCH4_SOURCE_DIR );
		QVERIFY2( !root.isEmpty(), "the test needs its source root" );

		// One copy, then its iterators: two calls would hand one range two
		// different temporaries.
		const QStringList sections = lmms::NamespaceRegistry::legacyGuiSections();
		QSet<QString> expected;
		for( const QString & name : sections ) { expected.insert( name ); }

		const QStringList headers = {
			QStringLiteral( "/include/ControllerRackView.h" ),
			QStringLiteral( "/include/PianoRoll.h" ),
			QStringLiteral( "/include/AutomationEditor.h" ),
			QStringLiteral( "/include/ProjectNotes.h" ),
			QStringLiteral( "/include/Timeline.h" ),
		};

		QSet<QString> measured;
		for( const QString & name : headers )
		{
			bool ok = false;
			const QString literal = nodeNameLiteralIn( root + name, &ok );
			QVERIFY2( ok, qPrintable( name ) );
			measured.insert( literal );
		}

		QCOMPARE( measured, expected );
	}

	/*! An unprefixed name that nobody has a handler for still resolves to core:
	 *  claiming a namespace is not the same as claiming the name, and 1.6's
	 *  preservation layer is what catches the latter. */
	void anUnknownLegacyNameIsStillCoreNotAnError()
	{
		lmms::NamespaceRegistry registry;
		const auto resolved = registry.resolve( QStringLiteral( "notASectionYet" ) );
		QVERIFY( resolved.claimed );
		QCOMPARE( resolved.namespaceUri, lmms::NamespaceRegistry::coreNamespace() );
	}

	/*! A bound prefix resolves; the local name is what follows the separator. */
	void aBoundPrefixResolvesToItsNamespace()
	{
		lmms::NamespaceRegistry registry;
		const auto resolved = registry.resolve( QStringLiteral( "z:track" ) );
		QVERIFY( resolved.namespaced );
		QVERIFY( resolved.claimed );
		QCOMPARE( resolved.prefix, QStringLiteral( "z" ) );
		QCOMPARE( resolved.localName, QStringLiteral( "track" ) );
		QCOMPARE( resolved.namespaceUri, lmms::NamespaceRegistry::coreNamespace() );
	}

	/*! The resolver never invents a namespace: an unbound prefix is unclaimed,
	 *  with no namespace at all - not a default, not core. */
	void anUnboundPrefixIsUnclaimedAndNamesNoNamespace()
	{
		lmms::NamespaceRegistry registry;
		const auto resolved = registry.resolve( QStringLiteral( "zz:thing" ) );
		QVERIFY( resolved.namespaced );
		QVERIFY( !resolved.claimed );
		QCOMPARE( resolved.prefix, QStringLiteral( "zz" ) );
		QCOMPARE( resolved.localName, QStringLiteral( "thing" ) );
		QCOMPARE( resolved.namespaceUri, QString() );
	}

	/*! A name with no local part is not claimable, however well bound its prefix. */
	void anEmptyLocalNameIsNeverClaimed()
	{
		lmms::NamespaceRegistry registry;
		QVERIFY( !registry.resolve( QStringLiteral( "z:" ) ).claimed );
		QVERIFY( !registry.resolve( QStringLiteral( ":" ) ).claimed );
		QVERIFY( !registry.resolve( QString() ).claimed );
	}

	/*! A vendor namespace is well-formed and recognised, and still unclaimed by
	 *  this build - no handler here implements a third party's schema. */
	void aVendorNamespaceIsWellFormedAndUnclaimed()
	{
		const QString uri = lmms::NamespaceRegistry::vendorNamespace( QStringLiteral( "example.com" ), 2 );
		QCOMPARE( uri, QStringLiteral( "vendor.example.com:2" ) );
		QVERIFY( lmms::NamespaceRegistry::isVendorNamespace( uri ) );
		QVERIFY( !lmms::NamespaceRegistry::isKnownNamespace( uri ) );

		lmms::NamespaceRegistry registry;
		QVERIFY( registry.bindPrefix( QStringLiteral( "v" ), uri ) );
		const auto resolved = registry.resolve( QStringLiteral( "v:thing" ) );
		QVERIFY( resolved.namespaced );
		QVERIFY( !resolved.claimed );
		QCOMPARE( resolved.namespaceUri, uri );
		QCOMPARE( resolved.localName, QStringLiteral( "thing" ) );
	}

	/*! A *future* first-party namespace - `zene:core:2` - is a well-formed name in
	 *  a namespace this build does not implement. Unclaimed, not guessed at. */
	void aFutureCoreVersionIsUnclaimed()
	{
		lmms::NamespaceRegistry registry;
		QVERIFY( registry.bindPrefix( QStringLiteral( "q" ), QStringLiteral( "zene:core:2" ) ) );
		const auto resolved = registry.resolve( QStringLiteral( "q:track" ) );
		QVERIFY( !resolved.claimed );
		QCOMPARE( resolved.namespaceUri, QStringLiteral( "zene:core:2" ) );
		QVERIFY( !lmms::NamespaceRegistry::isVendorNamespace( resolved.namespaceUri ) );
	}

	/*! qualify() is the resolver's other direction, and it invents no prefix
	 *  either: a namespace with no binding is unspellable, and the caller must
	 *  declare one first (which is what a document's `xmlns:` does). */
	void qualifyUsesABoundPrefixAndInventsNone()
	{
		lmms::NamespaceRegistry registry;
		QCOMPARE( registry.qualify( lmms::NamespaceRegistry::coreNamespace(), QStringLiteral( "track" ) ),
			QStringLiteral( "z:track" ) );
		QCOMPARE( registry.qualify( lmms::NamespaceRegistry::guiNamespace(), QStringLiteral( "pianoroll" ) ),
			QString() );
		QCOMPARE( registry.qualify( QStringLiteral( "zene:core:2" ), QStringLiteral( "track" ) ), QString() );
		QCOMPARE( registry.qualify( lmms::NamespaceRegistry::coreNamespace(), QString() ), QString() );

		QVERIFY( registry.bindPrefix( QStringLiteral( "zg" ), lmms::NamespaceRegistry::guiNamespace() ) );
		QCOMPARE( registry.qualify( lmms::NamespaceRegistry::guiNamespace(), QStringLiteral( "pianoroll" ) ),
			QStringLiteral( "zg:pianoroll" ) );
		// and the round trip closes
		const auto back = registry.resolve( QStringLiteral( "zg:pianoroll" ) );
		QVERIFY( back.claimed );
		QCOMPARE( back.namespaceUri, lmms::NamespaceRegistry::guiNamespace() );
	}

	/*! prefixForUri() has ONE answer per namespace, in binding order, and none for
	 *  an unbound one - a table with two answers is a table that makes qualify()
	 *  non-deterministic. */
	void prefixForUriIsDeterministicAndSingleValued()
	{
		lmms::NamespaceRegistry registry;
		QCOMPARE( registry.prefixForUri( lmms::NamespaceRegistry::coreNamespace() ), QStringLiteral( "z" ) );
		QCOMPARE( registry.prefixForUri( lmms::NamespaceRegistry::docNamespace() ), QString() );

		QVERIFY( registry.bindPrefix( QStringLiteral( "d" ), lmms::NamespaceRegistry::docNamespace() ) );
		QVERIFY( registry.bindPrefix( QStringLiteral( "d2" ), lmms::NamespaceRegistry::docNamespace() ) );
		QCOMPARE( registry.prefixForUri( lmms::NamespaceRegistry::docNamespace() ), QStringLiteral( "d" ) );
		QVERIFY( registry.hasPrefix( QStringLiteral( "d2" ) ) );
		QCOMPARE( registry.uriForPrefix( QStringLiteral( "d2" ) ),
			lmms::NamespaceRegistry::docNamespace() );

		// rebinding a prefix replaces it rather than duplicating the entry
		QVERIFY( registry.bindPrefix( QStringLiteral( "d" ), lmms::NamespaceRegistry::guiNamespace() ) );
		QCOMPARE( registry.prefixForUri( lmms::NamespaceRegistry::guiNamespace() ), QStringLiteral( "d" ) );
		QCOMPARE( registry.resolve( QStringLiteral( "d:pianoroll" ) ).namespaceUri,
			lmms::NamespaceRegistry::guiNamespace() );
	}

	/*! Binding nothing is not binding: an empty prefix or namespace is refused. */
	void bindPrefixRefusesEmptyOperands()
	{
		lmms::NamespaceRegistry registry;
		QVERIFY( !registry.bindPrefix( QString(), lmms::NamespaceRegistry::coreNamespace() ) );
		QVERIFY( !registry.bindPrefix( QStringLiteral( "x" ), QString() ) );
		QVERIFY( !registry.hasPrefix( QStringLiteral( "x" ) ) );
		QVERIFY( !registry.hasPrefix( QString() ) );
	}

	/*! A malformed vendor namespace is not a namespace. */
	void vendorNamespaceRefusesMalformedInput()
	{
		using lmms::NamespaceRegistry;
		QCOMPARE( NamespaceRegistry::vendorNamespace( QString(), 1 ), QString() );
		QCOMPARE( NamespaceRegistry::vendorNamespace( QStringLiteral( "a:b" ), 1 ), QString() );
		QCOMPARE( NamespaceRegistry::vendorNamespace( QStringLiteral( "a b" ), 1 ), QString() );
		QCOMPARE( NamespaceRegistry::vendorNamespace( QStringLiteral( "a\tb" ), 1 ), QString() );
		QCOMPARE( NamespaceRegistry::vendorNamespace( QStringLiteral( "example.com" ), 0 ), QString() );
		QCOMPARE( NamespaceRegistry::vendorNamespace( QStringLiteral( "example.com" ), -1 ), QString() );

		QVERIFY( !NamespaceRegistry::isVendorNamespace( QStringLiteral( "vendor.:1" ) ) );
		QVERIFY( !NamespaceRegistry::isVendorNamespace( QStringLiteral( "vendor.example.com:" ) ) );
		QVERIFY( !NamespaceRegistry::isVendorNamespace( QStringLiteral( "vendor.example.com:x" ) ) );
		QVERIFY( !NamespaceRegistry::isVendorNamespace( QStringLiteral( "vendor.example.com:0" ) ) );
		QVERIFY( !NamespaceRegistry::isVendorNamespace( QStringLiteral( "example.com:1" ) ) );
		QVERIFY( !NamespaceRegistry::isVendorNamespace( QString() ) );
		QVERIFY( NamespaceRegistry::isVendorNamespace( QStringLiteral( "vendor.example.com:1" ) ) );
	}
};

QTEST_GUILESS_MAIN(NamespaceRegistryTest)
#include "NamespaceRegistryTest.moc"
