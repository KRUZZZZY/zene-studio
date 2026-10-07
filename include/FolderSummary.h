/*
 * FolderSummary.h - a folder track's summary clips: the merged spans of its children's clips
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

#ifndef LMMS_FOLDER_SUMMARY_H
#define LMMS_FOLDER_SUMMARY_H

#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Track;
class TrackFolder;

/*! 0.5.0 ED-035: one summary clip of a folder track - a stretch of the song where the folder's
 *  children (its sub-folders' too) hold clips, with no gap inside it. Clips that overlap or touch
 *  make one span, so every child clip lies inside exactly one; editing a span edits those clips. */
struct FolderSpan
{
	tick_t start = 0;  //!< the earliest start of the span's clips
	tick_t end = 0;    //!< the latest end of them
	int clips = 0;     //!< how many clips the span holds
};

//! @a folder's summary clips, earliest first. Empty when no track inside it holds a clip.
LMMS_EXPORT std::vector<FolderSpan> folderSummary(const TrackFolder& folder);

//! Every track inside @a folder at any depth (not the sub-folders themselves) that holds a clip
//! starting inside [@a from, @a to), in track-list order.
LMMS_EXPORT std::vector<Track*> folderTracksWithin(const TrackFolder& folder, tick_t from, tick_t to);

} // namespace lmms

#endif // LMMS_FOLDER_SUMMARY_H
