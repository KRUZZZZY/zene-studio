/*
 * FolderSummary.cpp - a folder track's summary clips: the merged spans of its children's clips
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

#include "FolderSummary.h"

#include <algorithm>
#include <utility>

#include "Clip.h"
#include "TrackContainer.h"
#include "TrackFolder.h"

namespace lmms
{

namespace
{

bool insideFolder(const Track* track, const TrackFolder& folder)
{
	for (const TrackFolder* parent = track->parentFolder(); parent != nullptr; parent = parent->parentFolder())
	{
		if (parent == &folder) { return true; }
	}
	return false;
}

//! Every clip inside @a folder as [start, end), sorted by start.
std::vector<std::pair<tick_t, tick_t>> childClipSpans(const TrackFolder& folder)
{
	std::vector<std::pair<tick_t, tick_t>> out;
	if (folder.trackContainer() == nullptr) { return out; }
	for (const Track* track : folder.trackContainer()->tracks())
	{
		if (!insideFolder(track, folder)) { continue; }
		for (const Clip* clip : track->getClips())
		{
			out.emplace_back(clip->startPosition().getTicks(), clip->endPosition().getTicks());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

std::vector<FolderSpan> folderSummary(const TrackFolder& folder)
{
	std::vector<FolderSpan> spans;
	for (const auto& [start, end] : childClipSpans(folder))
	{
		// A clip that starts before the open span ends (or right where it ends) belongs to it.
		if (!spans.empty() && start <= spans.back().end)
		{
			spans.back().end = std::max(spans.back().end, end);
			++spans.back().clips;
			continue;
		}
		spans.push_back({start, std::max(start, end), 1});
	}
	return spans;
}

std::vector<Track*> folderTracksWithin(const TrackFolder& folder, tick_t from, tick_t to)
{
	std::vector<Track*> out;
	if (folder.trackContainer() == nullptr) { return out; }
	for (Track* track : folder.trackContainer()->tracks())
	{
		if (!insideFolder(track, folder)) { continue; }
		const auto& clips = track->getClips();
		const bool holds = std::any_of(clips.begin(), clips.end(), [from, to](const Clip* clip) {
			const tick_t start = clip->startPosition().getTicks();
			return start >= from && start < to;
		});
		if (holds) { out.push_back(track); }
	}
	return out;
}

} // namespace lmms
