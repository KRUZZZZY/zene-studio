/*
 * UnclaimedElements.h - the elements a loader did not claim, kept verbatim
 *                      so that opening and re-saving cannot destroy them.
 *                      ARCH-4 S1b.
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

#ifndef LMMS_UNCLAIMED_ELEMENTS_H
#define LMMS_UNCLAIMED_ELEMENTS_H

#include "lmms_export.h"

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QVector>

namespace lmms
{

/*! One element the loader did not claim, kept as the raw XML it was read as, so
 *  that a project written by a newer build (or by a build of this one with more
 *  features compiled in) cannot lose its data merely by being opened and saved
 *  here. SPEC-ARCH-4 1.6.1: "Any element the loader does not claim - unknown
 *  namespace, unknown section, unknown child - is kept verbatim, keyed by its
 *  document path, and re-emitted on save at the same path."
 *
 *  `key` is what the holder knows about the element at CAPTURE time: the section
 *  name at the top level of <song>, the child name inside a <track>. The full
 *  document path the spec keys on (its sketch is `/song/lane[12]`) is composed by
 *  the report that names them, because a track cannot know its own index while
 *  the load walk that is building it is still running. A capture therefore
 *  records the element's own name and the report composes the path around it
 *  (Song::unclaimedElements()).
 *
 *  `xml` is `key`'s element serialised the way the project writer serialises
 *  everything - QDomElement::save( stream, 2 ) - which is what makes a re-emitted
 *  block identical to the one that was read.
 *
 *  The in-tree precedent this generalises is the `<session>` block a build
 *  without the Session View reader reads and writes back verbatim (src/core/
 *  Song.cpp, the `#else` of `#ifdef LMMS_HAVE_SESSION_VIEW`). S1b does not
 *  rewrite that path; it gives the same treatment to everything else no reader
 *  claims.
 */
struct UnclaimedElement
{
	//! The element's own name: a section name at the top level, a child name in
	//! a track.
	QString key;
	//! The element, serialised as read.
	QString xml;
};

/*! Capture \a element into \a into under \a key. Appends, so the capture order
 *  is the document order and two elements sharing a name are BOTH kept. A null
 *  element captures nothing.
 */
LMMS_EXPORT void captureUnclaimed( const QDomElement & element, const QString & key,
	QVector<UnclaimedElement> & into );

/*! Re-emit every captured element as a child of \a parent, in capture order,
 *  resolving imports against \a document (which must be \a parent's document).
 *  Returns true when at least one element was re-emitted, false when \a elements
 *  was empty or nothing in it could be re-emitted.
 *
 *  An entry whose XML does not parse is skipped rather than fatal: an entry can
 *  only be malformed if something corrupted it after capture, and refusing to
 *  save an entire project over one bad preserved block would be a worse failure
 *  than dropping that one block. captureUnclaimed() cannot produce such an entry,
 *  which is what the test pins.
 */
LMMS_EXPORT bool reemitUnclaimed( const QVector<UnclaimedElement> & elements,
	QDomDocument & document, QDomElement & parent );

} // namespace lmms

#endif // LMMS_UNCLAIMED_ELEMENTS_H
