/*
 * ProjectContainer.h - the `.mmpz` v2 container: a STORE ZIP holding the
 *                      document skeleton and one entry per section, so a reader
 *                      can address a section without parsing its neighbours.
 *                      SPEC-ARCH-4 5.1 (S2c). ARCH-4 S2c.
 *
 * WHY THIS EXISTS, AND WHAT IT DELIBERATELY IS NOT.
 *
 * A `.mmpz` today is one blob: the document's XML through qCompress(). That
 * makes the whole document one indivisible read - a caller who wants the tracks
 * and the tempo map still hands the audio-clip section to the parser. The v2
 * container is the same XML cut along the section boundaries the <z:index>
 * already names (DocumentIndex.h, ARCH-4 S2a), with the skeleton in one entry
 * and every section in its own.
 *
 * The container is NOT new ZIP code. It is a policy layer over the writer and
 * reader already in the tree (DawProjectInterchange.h: dawProjectZipWrite,
 * dawProjectZipRead) - STORE entries, one local header per entry, one central
 * directory, no compression, no directory entries, no data descriptors, no
 * spanning. DawProjectZip.cpp:8-15 records why compression cannot be reused
 * here: DataFile.cpp's mmpz path uses Qt's qCompress/qUncompress, which is
 * zlib's own framed format and NOT a raw DEFLATE stream. The cost of that rule
 * is stated plainly rather than discovered: a v2 container is LARGER than the
 * v1 blob it replaces, because it stores what v1 compressed.
 *
 * THIS MODULE DOES NOT PARSE, PRUNE OR REDUCE ANYTHING. It moves entries. The
 * section boundaries come from the index, the bytes come from the caller, and
 * the only judgements made here are about entry NAMES - which is enough to be
 * load-bearing, because section names collide (ARCH-4 S2a measured it:
 * collidingNamesAreAllOrNothing) and a colliding name is a container that has
 * silently lost a section.
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

#ifndef LMMS_PROJECT_CONTAINER_H
#define LMMS_PROJECT_CONTAINER_H

#include "lmms_export.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace lmms
{

namespace projectcontainer
{

/*! The shapes a project file's bytes can take. Ordered the way a loader tries
 *  them, and decided by CONTENT only - see shapeOf(). */
enum class Shape
{
	Empty,             //!< no bytes at all
	ZipContainer,      //!< a v2 container (this module)
	LegacyCompressed,  //!< Qt's qCompress() framing - the `.mmpz` that ships today
	PlainXml,          //!< the XML text itself - an `.mmp`
	Unknown            //!< none of the above, so a caller must refuse it by name
};

/*! Which shape \a bytes are, decided by what they contain and NEVER by a file
 *  name or extension.
 *
 *  SPEC-ARCH-4 risk 5 (":589-591") requires exactly that: a `.mmpz` that is v1
 *  and a `.mmpz` that is v2 are told apart by their bytes, because the
 *  extension is chosen by configuration (DataFile::nameWithExtension,
 *  src/core/DataFile.cpp:275-299) and would otherwise make the same name mean
 *  two formats depending on a user setting.
 *
 *  A file name cannot be the discriminator, and neither can the document's own
 *  `version` attribute be the FIRST one: that attribute lives inside the XML,
 *  which a ZIP is not. So the container is recognised from its own magic, and
 *  the version attribute confirms the document once the container is open.
 *
 *  Every test here is cheap and none of them inflates the payload: a ZIP local
 *  header signature, a zlib header, or a leading `<` after an optional BOM and
 *  whitespace. Deciding "this is a v2 container" is a 4-byte comparison; the
 *  full validation is the reader's, and it refuses by name (DawProjectZip.cpp
 *  bounds-checks every offset and size against the file's own length). */
LMMS_EXPORT Shape shapeOf( const QByteArray& bytes );

//! True when \a bytes are a v2 container. Sugar for shapeOf() == ZipContainer.
LMMS_EXPORT bool isContainer( const QByteArray& bytes );

/*! The name of the entry holding the document skeleton: the root element, its
 *  `<head>`, and the `<z:index>` that names every other entry. A function
 *  rather than a constant, per the project's static-init rule (no QString
 *  before main()). */
LMMS_EXPORT QString skeletonEntryName();

/*! The name a section entry must carry: \a index in the container's own order,
 *  then \a sectionName.
 *
 *  The position comes FIRST and is not decoration. Section names are not unique
 *  - two `<track>` elements are two sections with the same name, and an
 *  authored document can hold two `<bigclip>` siblings - so a name used as the
 *  container key would merge them into one entry and lose a section. The index
 *  is what maps a position back to a name, and this is where that mapping is
 *  written down.
 *
 *  Returns an empty QString for a name that cannot be an entry, so a caller
 *  has one place to ask rather than a rule to re-implement. */
LMMS_EXPORT QString sectionEntryName( int index, const QString& sectionName );

/*! Write a v2 container: \a skeleton as the skeleton entry, then one STORE
 *  entry per section, in the order given.
 *
 *  \a sections are (entry name, bytes) pairs and their names are validated
 *  rather than trusted: an empty name, a name that repeats, or a name that
 *  collides with the skeleton entry is refused with \a error set and NO FILE
 *  LEFT BEHIND. That last part is the difference between a refusal and a
 *  half-written project, and the underlying writer already removes its output
 *  on a failed commit. */
LMMS_EXPORT bool writeContainer( const QString& path, const QByteArray& skeleton,
	const std::vector<std::pair<QString, QByteArray>>& sections, QString* error );

/*! Read a v2 container into \a entries.
 *
 *  \a entries is in the container's own (central-directory) order, and the
 *  skeleton entry is not special-cased out of it - a caller that wants the
 *  document asks for the entry named skeletonEntryName(). A container without
 *  that entry is refused: it is a ZIP, but it is not one of ours, and guessing
 *  which entry is "the project" is how a foreign archive gets misread.
 *
 *  When \a keepEntries is non-empty only those entries are returned, plus the
 *  skeleton. HONEST LIMIT, stated because it is easy to assume otherwise: the
 *  reader underneath reads the file whole and copies every entry's body before
 *  this selection runs (DawProjectZip.cpp:260, :410, :422). So a subset read
 *  saves PARSING, not I/O and not peak memory. Turning the container's
 *  addressing into a real I/O saving needs a filtered reader, which is a
 *  separate change and is recorded as such rather than implied here. */
LMMS_EXPORT bool readContainer( const QString& path, const QStringList& keepEntries,
	std::vector<std::pair<QString, QByteArray>>* entries, QString* error );

/*! True when \a entryName is shaped to be one of this container's section
 *  entries: the `sections/` namespace (sectionNamespace(), as spelled by
 *  sectionEntryName()) and nothing outside the character set
 *  sectionEntryName() can produce.
 *
 *  The predicate version of the rule writeContainer() enforces on every write
 *  (src/core/ProjectContainer.cpp:276-283), exported so the REASSEMBLER checks
 *  entry names by the SAME rule the writer enforces instead of restating it -
 *  two spellings of the entry-shape rule could drift apart without either being
 *  wrong alone, and this module has already paid for that class of bug once
 *  (the index's `entry` attribute, reconciled on deriveContainerEntries below). */
LMMS_EXPORT bool isSectionEntryName( const QString& entryName );

/*! Derive a document's container entries: the skeleton, then one entry per
 *  section.
 *
 *  \a document is a project document carrying a `<z:index>` (DocumentIndex.h,
 *  SPEC-ARCH-4 1.4) - the index this tree writes when a project has a section a
 *  reader may skip. \a contentElementName names the document's CONTENT element
 *  (`"song"` for a song project), the parameter reduceDocumentSections()
 *  already takes: only the document knows which child of the root holds the
 *  sections, so the caller says. On success \a entries receives pairs in one
 *  order and only one: skeletonEntryName() first, then
 *  sectionEntryName( position, name ) per index row in index (document) order -
 *  the same shape readContainer() returns (see :138-154), so a derived set and
 *  a read set are interchangeable, and writeContainer() takes the first pair as
 *  its skeleton argument and the rest as its sections.
 *
 *  THE INDEX'S `entry` ATTRIBUTE IS NOT THIS CONTAINER'S KEY. The two naming
 *  rules are reconciled HERE, because neither header used to cross-reference the
 *  other. documentIndex() records `entry` as `<name> + ".xml"`
 *  (src/core/DocumentIndex.cpp:263; the field's contract is DocumentIndex.h
 *  around its DocumentSection struct), while this container files sections
 *  under sectionEntryName() - `sections/<position>-<name>`
 *  (src/core/ProjectContainer.cpp:216-227, validated on every write :276-283).
 *  The two cannot be swapped, measured by their own contracts rather than by
 *  preference: `<name>.xml` carries no position, so a document with two
 *  `<track>` sections - the exact case sectionEntryName() exists for (see
 *  :112-120) - would make both index rows claim `track.xml`, and writeContainer
 *  refuses that collision as "a section silently lost" (:285-294); and
 *  `<name>.xml` lacks the `sections/` namespace every entry must carry
 *  (:276-283), so a writer fed it would refuse on the first row. So this
 *  function derives each entry name from the row's POSITION plus its name,
 *  through sectionEntryName(), and never reads the recorded `entry` attribute;
 *  the reassembler below regenerates the same expected names from the
 *  skeleton's index and matches the container's entries against those. The
 *  attribute rides along inside the skeleton, written and read exactly as
 *  DocumentIndex defines it - this slice redefines nothing (additive rule).
 *
 *  THE ROUND-TRIP GUARANTEE IS RAW BYTE IDENTITY, chosen deliberately against
 *  the spec's own corrected oracle. SPEC-ARCH-4's S0 correction (:544-557 of
 *  SPEC-ARCH-4-DOCUMENT-MODEL-DRAFT.md) reclassifies every "byte identity"
 *  proof as CANONICAL byte identity, measured: five `zene upgrade` runs on one
 *  unchanged file produced five distinct raw byte streams, because
 *  QDomElement::save() orders attributes by Qt's per-process-random QHash seed
 *  - "a raw-cmp gate would be flaky by construction". That flake lives in
 *  RE-SERIALISATION, and this pair never re-serialises: the deriver copies the
 *  document's own bytes verbatim (the reduceDocumentSections precedent,
 *  DocumentIndex.h - "every byte outside the removed ranges is copied verbatim,
 *  so the answer is exactly \a data minus whole section subtrees") and the
 *  reassembler splices those same bytes back, so the round trip is compared RAW
 *  in the test and cannot flake for the reason the spec measured. Parsing here
 *  is navigation only - byte offsets from QXmlStreamReader - never a re-render;
 *  neither QDomDocument::save() nor QTextStream is called on this path.
 *
 *  HOW THE SPLICE WORKS (why raw identity is possible at all): each section's
 *  entry carries the section element AND every byte up to the next `<` - the
 *  whitespace that FOLLOWS it in the document - while the skeleton keeps each
 *  section's LEADING whitespace. The reassembler inserts the entries, in index
 *  order, immediately before the content element's close tag; the concatenation
 *  of `section + trailing whitespace`, in order, is then exactly the original
 *  interior. What could sit between sections but never travels in an entry - a
 *  comment or a processing instruction - would break that placement, so this
 *  function REFUSES a document whose content interior is not whitespace-only
 *  once the indexed sections are out, rather than promising identity it cannot
 *  hold. Nothing this tree writes puts one there today; a document that does is
 *  refused by name instead of mis-assembled.
 *
 *  REFUSALS: \a error is set and \a entries is left exactly as passed (no
 *  half-derived output - the unchanged-bytes shape where bytes are involved) for:
 *  a null \a entries; an empty \a contentElementName; a document that is not
 *  well-formed XML; a document with no content element; a document with no
 *  `<z:index>`; an index carrying a `<z:section>` with no name
 *  (parseDocumentIndex() silently drops that row by contract - see its
 *  declaration - but the deriver cannot, because the row-count-to-document
 *  correspondence IS the split's cross-check, and a dropped row silently shifts
 *  every later position); an index that names no section; a section name
 *  sectionEntryName() cannot file; a document scanSectionRanges() cannot follow
 *  to its end; an index and a document that disagree about which section sits
 *  where (count or order); an interior that still carries a child the index
 *  does not name; and a section whose end cannot be bounded (defensive - a
 *  well-formed document cannot reach it).
 *
 *  HONEST LIMITS, stated because they are easy to assume away: the document is
 *  parsed twice (QDomDocument to navigate, QXmlStreamReader to slice) - two
 *  passes over bytes that may be large, accepted because this runs on save, not
 *  on the audio thread; the index's digests are NOT verified against the entry
 *  bytes (the index rides along verbatim; sectionDigest() is a reader's
 *  integrity check to apply, not this function's job); and a section name
 *  outside sectionEntryName()'s character set (a non-ASCII element name, say)
 *  is refused, not escaped - an entry name this module cannot round-trip is a
 *  name the container would silently lose. */
LMMS_EXPORT bool deriveContainerEntries( const QByteArray& document,
	const QString& contentElementName,
	std::vector<std::pair<QString, QByteArray>>* entries, QString* error );

/*! Rebuild a document from a container's entries - the inverse of
 *  deriveContainerEntries(), and the step this tree could not do at all when
 *  ProjectContainer shipped: "--dump does not read one... this build cannot yet
 *  reassemble one from a container's entries" (src/core/main.cpp:624-628).
 *
 *  \a entries is the pair list readContainer() returns - the container's own
 *  central-directory order, skeleton not special-cased out (:138-154).
 *  \a contentElementName means what it means to the deriver: the content
 *  element's own tag name (`"song"`).
 *
 *  THE ORDER OF THE PAIRS IS DELIBERATELY NOT CONSULTED. The position lives
 *  inside each entry's NAME (sectionEntryName embeds it,
 *  src/core/ProjectContainer.cpp:216-227), so the expected names are
 *  regenerated from the skeleton's `<z:index>` rows and every entry is matched
 *  BY NAME. A shuffled pair list reassembles to the same bytes; a list whose
 *  POSITIONS disagree with the index - names swapped, shifted, invented, or
 *  duplicated - is refused, because the position is what maps an entry back to
 *  an index row and a wrong position is a section filed where another belongs.
 *  The skeleton is likewise found by name rather than assumed to be first
 *  (readContainer's contract promises the container's own order, not this
 *  function's convenience).
 *
 *  On success \a document receives the reassembled bytes: the skeleton with the
 *  section entries spliced in before the content element's close tag, in index
 *  order - the ORIGINAL bytes, raw. The guarantee, and why it is raw rather
 *  than merely canonical, is deriveContainerEntries' contract above.
 *
 *  REFUSALS: \a error is set and \a document is left exactly as passed (no
 *  half-reassembled output) for: a null destination; no entries; no entry named
 *  skeletonEntryName(); two entries sharing a name (a container that lost or
 *  doubled an entry, refused rather than guessed between - the write side's own
 *  rule, :285-294); an entry that is neither the skeleton nor one
 *  isSectionEntryName() accepts (a foreign archive, or a name writeContainer
 *  would never have written); an entry the index does not name (it would be
 *  dropped); a section the index names that the entries lack (it would be a
 *  hole); a skeleton that is not well-formed XML; a skeleton with no content
 *  element; a skeleton whose `<z:index>` is absent, empty, or has a row whose
 *  name sectionEntryName() cannot file (no trustworthy rows to place sections
 *  against - a nameless row the deriver refused at the source reappears here as
 *  a shifted expected-name set, refused by the missing/unexpected pair); and a
 *  content interior that is not whitespace-only (only whitespace can sit where
 *  the deriver left the gaps).
 *
 *  NOT A VERIFIER: the index's digests are not checked against the entry bytes
 *  here, same as the deriver. This function answers "can these entries be
 *  reassembled into a document byte-for-byte"; "are these the entries the index
 *  recorded" is a separate question with a separate answer (sectionDigest()),
 *  and conflating them would make a digest mismatch look like a parse failure. */
LMMS_EXPORT bool reassembleContainerDocument(
	const std::vector<std::pair<QString, QByteArray>>& entries,
	const QString& contentElementName, QByteArray* document, QString* error );

} // namespace projectcontainer

} // namespace lmms

#endif // LMMS_PROJECT_CONTAINER_H
