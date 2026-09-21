/*
 * DocumentIndex.h - the <z:index> a project document carries, naming the
 *                   sections a reader may skip. SPEC-ARCH-4 1.4. ARCH-4 S2a.
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

#ifndef LMMS_DOCUMENT_INDEX_H
#define LMMS_DOCUMENT_INDEX_H

#include "lmms_export.h"

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QVector>

namespace lmms
{

/*! The element name of the index itself, and of one entry in it. Functions
 *  rather than constants because the project's static-init rule is that a
 *  QString is built on use, never before main(). */
LMMS_EXPORT QString documentIndexNodeName();
LMMS_EXPORT QString documentIndexSectionNodeName();

/*! The prefix declaration the index requires on the document's root: the
 *  namespace attribute (`xmlns:z`) and the URI it binds. The project's reader
 *  parses with QDomDocument namespace processing OFF, so both are plain
 *  attributes and the prefix is part of the element name - which is what keeps
 *  an old reader's `nodeName()` comparisons working on a document that carries
 *  an index it does not know about. */
LMMS_EXPORT QString documentIndexNamespaceAttribute();
LMMS_EXPORT QString documentIndexNamespaceUri();

/*! One section of a project document: a child of the document's CONTENT element
 *  that a reader may skip. SPEC-ARCH-4 1.4: "each section named, versioned,
 *  digested".
 *
 *  `name` is the section element's OWN tag name, so the index cannot disagree
 *  with the document about what a section is called.
 *
 *  `entry` is the name a sectioned container would file the section under
 *  (`name` + `.xml`). `.mmpz` v2 is S2c's; this field is written now so the
 *  container adds no second naming rule, and it is uniform rather than the
 *  format sketch's `project.xml#song`, which describes the v1 fallback where
 *  every section shares one file.
 *
 *  `version` is the section's own `version` ATTRIBUTE when it has one - the
 *  in-tree precedent is the `<session>` block, which carries `version="1"`
 *  (src/core/SessionModel.cpp:245, read at `:300`) - and otherwise the
 *  document's own version. A section that evolves faster than the document
 *  says so; one that does not inherits.
 *
 *  `digest` is `sha256:` + the hex digest of the section in canonical form,
 *  rendered as the root of its own save so that it does not depend on where in a
 *  document the section sits - nor, deliberately, on which process renders it.
 *  The exact definition, and the two measurements that show the two obvious
 *  readings of it are both wrong, are on sectionDigest() below. */
struct DocumentSection
{
	QString name;
	QString entry;
	int version = 0;
	QString digest;
};

/*! The index of one document: its sections, in document order. */
struct DocumentIndex
{
	QVector<DocumentSection> sections;

	bool isEmpty() const { return sections.isEmpty(); }

	/*! The entry for \a name, or nullptr when the document does not carry that
	 *  section. A reader that was asked for a section it cannot have needs to
	 *  tell "absent" from "present and empty", which is why this answers a
	 *  pointer rather than a default-constructed entry. */
	const DocumentSection* section( const QString & name ) const;
};

/*! Enumerate \a content's children as sections, digesting each one.
 *
 *  \a content is the document's CONTENT element - `DataFile::content()`, which
 *  is `<song>` for a song project - and not the root. The granularity is fixed
 *  by the spec's own motivating case for partial load (SPEC-ARCH-4 1.4): "an
 *  agent that wants the tracks and the tempo map no longer pays for a 400 MB
 *  audio-clip section". Both of those are children of `<song>`, and so are the
 *  rest of 1.1's section list; the root's children are `head` and the content
 *  element besides, which is a granularity at which nothing can be skipped.
 *
 *  Three children are never sections, whichever element the caller passes:
 *   - `head`, which every reader loads and so can never skip;
 *   - the index itself, which cannot index its own digest;
 *   - `z:unclaimed`, if a later slice introduces it - the format sketch
 *     (SPEC-ARCH-4 1.1) addresses preserved content by document PATH, and a
 *     section is addressed by NAME, so an element whose whole purpose is to
 *     carry arbitrary foreign content is not a section.
 *
 *  \a documentVersion is what a section that carries no `version` attribute of
 *  its own is recorded as, and is the caller's because only the document knows
 *  it (DataFile writes it as the root's `version`).
 *
 *  Order is document order, never sorted: this is an index of a document, and
 *  SPEC-ARCH-4 1.6.4 makes document order the rule the preserved set already
 *  follows. */
LMMS_EXPORT DocumentIndex documentIndex( const QDomElement & content, int documentVersion );

/*! Serialise \a section in CANONICAL form and return `sha256:` + the hex digest
 *  of the UTF-8 bytes.
 *
 *  WHAT THE DIGEST ADDRESSES, stated exactly, because two obvious readings of it
 *  are both wrong and both were MEASURED wrong on 2026-09-21:
 *
 *   1. It is the section rendered as the ROOT OF ITS OWN SAVE, not the section's
 *      characters where they sit inside the document. QDomElement::save(n) takes
 *      `n` as the indent width PER LEVEL and puts the node it is called on at one
 *      level, so the same `<trackcontainer>` sits at column 2 here and column 4
 *      in a document whose content element is at column 2. Hashing a section's
 *      substring out of a saved file does not reproduce this value.
 *
 *   2. It is NOT the bytes the saved file carries for that section, and it is not
 *      QDomElement::save()'s rendering of it either. save() emits attributes in
 *      the order of Qt's attribute QHash, and Qt seeds qHash() RANDOMLY PER
 *      PROCESS - measured: five processes parsing one unchanged file re-saved one
 *      element in five different orders, and forcing the seed with
 *      QHashSeed::setDeterministicGlobalSeed() pinned them. save() is therefore a
 *      function of the process, not of the document. A reader that recomputed a
 *      digest through it got a different answer a few percent of the time, which
 *      is the flake the integration slot in tests/src/core/DocumentIndexTest.cpp
 *      used to show. So this digest is taken over a canonical rendering instead:
 *      attributes sorted by name, indent 2 per level, a childless element
 *      self-closed, whitespace-only text and comments ignored.
 *
 *  What the digest is FOR is what makes both of those the right choices: the
 *  index is recorded per section so a section can be identified ACROSS a
 *  container boundary - SPEC-ARCH-4 1.4's `entry`, the .mmpz v2 container S2c
 *  files each section into its own file. That comparison happens in a process
 *  that is not the one that wrote the document, so the value has to be
 *  reproducible outside the writer: the digest does not depend on where the
 *  section sits, nor on which process renders it, and both properties are pinned
 *  by slots in tests/src/core/DocumentIndexTest.cpp rather than only asserted
 *  here. */
LMMS_EXPORT QString sectionDigest( const QDomElement & section );

/*! Write \a index into \a document as a `<z:index>` child of \a root, and bind
 *  the `z` prefix on \a root. The element is inserted directly AFTER `<head>`,
 *  the position the format sketch gives it.
 *
 *  Answers false and changes nothing when \a index is empty. An empty index
 *  must not be written: a document with no skippable section that carries an
 *  empty `<z:index>` has still changed its bytes, which is the one thing the
 *  additive rule forbids. */
LMMS_EXPORT bool writeDocumentIndex( const DocumentIndex & index, QDomDocument & document,
	QDomElement & root );

/*! Read the index \a root carries, or an empty one when it carries none. An
 *  entry with no `name` is skipped - a section that cannot be named cannot be
 *  loaded by name, so admitting it would turn a corrupt index into a load that
 *  silently omits a section. */
LMMS_EXPORT DocumentIndex parseDocumentIndex( const QDomElement & root );

} // namespace lmms

#endif // LMMS_DOCUMENT_INDEX_H
