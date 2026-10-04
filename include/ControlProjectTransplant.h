/*
 * ControlProjectTransplant.h - moving parts of one project's document into this project
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

#ifndef LMMS_CONTROL_PROJECT_TRANSPLANT_H
#define LMMS_CONTROL_PROJECT_TRANSPLANT_H

#include <QDomElement>
#include <QJsonObject>
#include <QList>

#include "ControlRegistry.h"

namespace lmms::control::transplant
{

/*! What project.import_tracks (WF-036), project.paste_tracks (WF-042) and the reference project's copies
 *  (reference.*, WF-042) share - defined in ControlCommandsImportTracks.cpp, where they began. */

//! Drop the stable ids a copied subtree carries and point its mixer channel at the master; a track's
//! 'folder' goes too unless @a keepFolders (it is another track's id).
void detach(QDomElement element, bool keepFolders = false);

//! Point a copied subtree's media references that resolve against @a sourceDir (the source project's
//! folder) at their files, so they do not resolve against this project's folder instead.
void rebaseMedia(QDomElement element, const QString& sourceDir);

//! The song's <track> elements under a project document's @a content (its <song> element).
QList<QDomElement> songTracks(const QDomElement& content);

/*! project.import_tracks' copy, from tracks already read: the ones @a args' 'tracks' names (indices; all
 *  when it is left out) of @a tracks, detached and media rebased from @a sourceDir, made in this song as
 *  ONE undo step. The reply lists them under @a key ('imported', 'copied'...). */
ControlResult importTrackElements(const QList<QDomElement>& tracks, const QJsonObject& args, const QString& sourceDir,
	const QString& key);

} // namespace lmms::control::transplant

#endif // LMMS_CONTROL_PROJECT_TRANSPLANT_H
