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

} // namespace projectcontainer

} // namespace lmms

#endif // LMMS_PROJECT_CONTAINER_H
