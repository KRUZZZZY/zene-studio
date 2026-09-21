/*
 * NamespaceRegistry.h - the document format's namespace table and the resolver
 *                      that turns a document name into (namespace, local name).
 *                      ARCH-4 S1a.
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

#ifndef LMMS_NAMESPACE_REGISTRY_H
#define LMMS_NAMESPACE_REGISTRY_H

#include "lmms_export.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace lmms
{

/*! The document model's schema namespaces, and the one table that resolves a
 *  name in a document to the namespace it belongs to.
 *
 * Requirement 2 of the document model ("extensible by namespaced schemas",
 * design/specs/SPEC-ARCH-4-DOCUMENT-MODEL-DRAFT.md 1.3) says every element and
 * attribute name carries a namespace, declared on the root the way XML declares
 * one (`xmlns:z="zene:core:1"`), and that the loader resolves `prefix:local` to
 * a handler through ONE table. This class is that table.
 *
 * Three measured facts shape it:
 *
 *  - The namespaces are URI-shaped and already versioned: `zene:core:1` (the
 *    sections and objects that exist today), `zene:doc:1` (index, unclaimed,
 *    provenance, derived markers), `zene:gui:1` (the GUI-owned sections
 *    already in the file) and `vendor.<domain>:<version>` for anything a third
 *    party writes (1.3).
 *  - Legacy documents are unprefixed and all 38 fixtures must keep loading, so
 *    an unprefixed name resolves through the compatibility table below - to
 *    `zene:core:1` by default, and to `zene:gui:1` for the five GUI-owned
 *    sections the writer has always emitted unprefixed (the last branch of
 *    `Song::loadProject`'s walk, `src/core/Song.cpp:1602-1624`). (1.3)
 *  - A name whose prefix is not bound, or whose namespace this build does not
 *    know, is NOT an error: it is *unclaimed*, and 1.6's preservation layer is
 *    what keeps it ("Unclaimed namespaces fall through to 1.6's preservation
 *    layer rather than to a warning", 1.3). resolve() reports that condition;
 *    it never guesses a namespace for an unknown one.
 *
 * A vendor namespace is unclaimed *by construction*: this build implements no
 * handler for any third party's. It is still a well-formed namespace, and
 * isVendorNamespace() recognises its shape so a report can name the vendor -
 * which is why constructing one here is supported even though resolving into
 * one always answers `claimed == false`.
 *
 * S1a is deliberately additive. The writer keeps emitting legacy names ("Additive:
 * legacy names resolve through the compatibility table; nothing new is written",
 * the S1 slice row), so no load or save path changes in this unit; this class is
 * what the preservation, `type` and load-report units consult. Nothing here is
 * allowed to make a document that loads today fail to load.
 */
class LMMS_EXPORT NamespaceRegistry
{
public:
	/*! One name from a document, resolved.
	 *
	 * `claimed` is the only field a caller should branch on when deciding
	 * whether *this build* handles the name. `namespaceUri` is empty precisely
	 * when the name's namespace could not be determined at all - an unbound
	 * prefix - and is non-empty-but-unclaimed for a namespace this build does
	 * not know (a vendor's, or a future `zene:core:2`).
	 */
	struct ResolvedName
	{
		QString prefix;       //!< as written; empty when the name was unprefixed
		QString namespaceUri; //!< empty when the prefix is not bound
		QString localName;
		bool namespaced = false;
		bool claimed = false;

		bool operator==( const ResolvedName & other ) const
		{
			return namespaced == other.namespaced
				&& claimed == other.claimed
				&& prefix == other.prefix
				&& namespaceUri == other.namespaceUri
				&& localName == other.localName;
		}
	};

	/*! A registry with the format's default binding: `z` -> `zene:core:1`.
	 *  A document's own `xmlns:` declarations are bound on top of it (bindPrefix).
	 */
	NamespaceRegistry();

	static const QString & coreNamespace(); //!< "zene:core:1"
	static const QString & docNamespace();  //!< "zene:doc:1"
	static const QString & guiNamespace();  //!< "zene:gui:1"

	/*! "vendor.<domain>:<version>". Returns an empty string for a domain that
	 *  cannot form one (empty, or carrying a ':' or whitespace), because a
	 *  malformed namespace is not a namespace. */
	static QString vendorNamespace( const QString & domain, int version );

	//! The three first-party namespaces, in declaration order.
	static QStringList knownNamespaces();
	//! Is \a uri one of the three first-party namespaces?
	static bool isKnownNamespace( const QString & uri );
	//! Does \a uri have the `vendor.<domain>:<version>` shape?
	static bool isVendorNamespace( const QString & uri );

	/*! The compatibility table: the namespace an UNPREFIXED legacy name belongs
	 *  to. `zene:gui:1` for the five GUI-owned sections, `zene:core:1` for
	 *  everything else - a legacy file predates namespaces, and everything it
	 *  carries today except those five is core. */
	static const QString & legacyNamespace( const QString & name );
	//! The unprefixed legacy names the table maps away from `zene:core:1`.
	static QStringList legacyGuiSections();

	/*! Binds \a prefix to \a uri for this document. Returns false and binds
	 *  nothing when either is empty. Rebinding a prefix replaces it; the
	 *  insertion order of distinct prefixes is preserved so prefixForUri() has
	 *  one deterministic answer. */
	bool bindPrefix( const QString & prefix, const QString & uri );

	bool hasPrefix( const QString & prefix ) const;
	QString uriForPrefix( const QString & prefix ) const;
	//! The first prefix bound to \a uri, or an empty string when none is.
	QString prefixForUri( const QString & uri ) const;

	//! Resolve one document name. Never returns a made-up namespace.
	ResolvedName resolve( const QString & name ) const;

	/*! `prefix:local` for \a uri, or an empty string when no bound prefix
	 *  addresses it - qualify() does not invent prefixes either. */
	QString qualify( const QString & uri, const QString & localName ) const;

private:
	QHash<QString, QString> m_prefixToUri;
	QStringList m_prefixOrder; //!< insertion order, for prefixForUri()
};

} // namespace lmms

#endif // LMMS_NAMESPACE_REGISTRY_H
