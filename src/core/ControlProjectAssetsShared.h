/*
 * ControlProjectAssetsShared.h - the internals the two halves of the row-38
 *                                engine share: what a project file references,
 *                                how a stored value becomes a path, and how a
 *                                document is read and written back.
 *
 * PRIVATE header, not an installed one: it is the seam between
 * ControlProjectAssets.cpp (READ: scan, hash, digest) and
 * ControlProjectAssetsRelink.cpp (WRITE: the rewrite). The product's own
 * convention for a group whose read and edit halves are separate translation
 * units, like ControlCommandsVcaShared.h and ControlCommandsSessionShared.h.
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

#ifndef LMMS_CONTROL_PROJECT_ASSETS_SHARED_H
#define LMMS_CONTROL_PROJECT_ASSETS_SHARED_H

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "ControlProjectAssets.h"
#include "ControlRegistry.h"

namespace lmms
{
namespace control
{
namespace projectassets
{

/*! One element kind a project file references a file from.
 *
 *  `tag`/`attribute` identify the reference; `embedAttribute` names the
 *  attribute that carries the media INSIDE the project when it is there;
 *  `parentPath` narrows a tag that is not unique in the document to the
 *  ancestors it must sit under (suffix-matched, so `<session>` being nested
 *  inside something else does not lose the reference).
 *
 *  Each row is a place the product itself writes a path:
 *    sampleclip          SampleClip::saveSettings, src/core/SampleClip.cpp:467
 *    audiofileprocessor  AudioFileProcessor::saveSettings,
 *                        plugins/AudioFileProcessor/AudioFileProcessor.cpp:196
 *    sf2player           Sf2Instrument::saveSettings, plugins/Sf2Player/Sf2Player.cpp:244
 *    clip (session slot) ClipSlot::saveState, src/core/SessionClip.cpp:77
 *
 *  The first two are the elements DataFile::ELEMENTS_WITH_RESOURCES names for
 *  its own bundle walk (src/core/DataFile.cpp:68-71); the other two are the
 *  remaining path-bearing ones in the format.
 */
struct AssetElement
{
	const char* tag;
	const char* attribute;
	const char* embedAttribute; //!< "" when the element has no inline form
	const char* parentPath;     //!< "" when the tag is unambiguous
};

//! The table itself, defined once in ControlProjectAssets.cpp.
extern const AssetElement kAssetElements[4];

bool elementMatches(const AssetElement& spec, const QString& tag, const QString& parentChain);

//! A reference's path, and the rule that placed it there.
struct Resolution
{
	QString path;
	QString via;
	bool resolved = false;
};

/*! Places a stored value on this filesystem. `local:` and an unresolvable
 *  legacy relative path both resolve against \a projectDir - see the header
 *  comment of ControlProjectAssets.cpp for why that is the project FILE's
 *  directory and not the open project's. */
Resolution resolveAssetPath(const QString& projectDir, const QString& raw);

//! Fills \a ref from \a element and the disk (existence, size, mtime).
void fillReference(const QDomElement& element, const AssetElement& spec, int index,
	const QString& projectDir, ProjectAssetReference* ref);

//! Reads .mmp (XML) or .mmpz (qCompress'd XML) into \a document. False with a
//! typed error for a missing, unreadable, mis-inflated or unparseable file.
bool readProjectDocument(const QString& projectPath, QDomDocument* document, QString* format,
	ControlResult* error);

//! The document as this product writes one: save() at DataFile::write's indent,
//! with the DOCTYPE line dropped at the output boundary
//! (src/core/DataFile.cpp:330-350).
QByteArray serialiseDocument(QDomDocument* document);

} // namespace projectassets
} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_PROJECT_ASSETS_SHARED_H
