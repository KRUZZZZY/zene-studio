/*
 * ClipGroupEdits.cpp - edits over a set of clips: summary clips and whole-song section moves
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

#include "ClipGroupEdits.h"

#include <algorithm>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlReversibility.h"
#include "EditLocks.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

namespace lmms
{

namespace arrangement
{

namespace
{

tick_t startOf(const Clip* clip) { return clip->startPosition().getTicks(); }
tick_t endOf(const Clip* clip) { return clip->endPosition().getTicks(); }

void plan(std::vector<ClipEdit>* edits, Clip* clip, tick_t start, tick_t end, bool remove = false)
{
	const bool known = std::any_of(edits->begin(), edits->end(), [clip](const ClipEdit& each) { return each.clip == clip; });
	if (!known) { edits->push_back({clip, start, end, remove}); }
}

//! The end edge moved to @a edge: the clips ending at the old edge follow it out, or everything past it is cut.
void planEndEdge(const std::vector<Clip*>& clips, const FolderSpan& span, tick_t edge, std::vector<ClipEdit>* edits)
{
	for (Clip* clip : clips)
	{
		if (edge > span.end)
		{
			if (endOf(clip) == span.end) { plan(edits, clip, startOf(clip), edge); }
		}
		else if (startOf(clip) >= edge) { plan(edits, clip, 0, 0, true); }
		else if (endOf(clip) > edge) { plan(edits, clip, startOf(clip), edge); }
	}
}

//! The start edge moved to @a edge, the mirror of planEndEdge.
void planStartEdge(const std::vector<Clip*>& clips, const FolderSpan& span, tick_t edge, std::vector<ClipEdit>* edits)
{
	for (Clip* clip : clips)
	{
		if (edge < span.start)
		{
			if (startOf(clip) == span.start) { plan(edits, clip, edge, endOf(clip)); }
		}
		else if (endOf(clip) <= edge) { plan(edits, clip, 0, 0, true); }
		else if (startOf(clip) < edge) { plan(edits, clip, edge, endOf(clip)); }
	}
}

} // namespace

void planSpanResize(const TrackFolder& folder, const FolderSpan& span, SpanEdge edge, tick_t delta, std::vector<ClipEdit>* edits)
{
	const std::vector<Clip*> clips = folderSpanClips(folder, span);
	if (edge == SpanEdge::End) { planEndEdge(clips, span, span.end + delta, edits); }
	else { planStartEdge(clips, span, span.start + delta, edits); }
}

bool planClipResize(Clip* clip, SpanEdge edge, tick_t delta, std::vector<ClipEdit>* edits)
{
	const tick_t start = startOf(clip) + (edge == SpanEdge::Start ? delta : 0);
	const tick_t end = endOf(clip) + (edge == SpanEdge::End ? delta : 0);
	if (start < 0 || end <= start) { return false; }
	plan(edits, clip, start, end);
	return true;
}

void applyClipEdits(const std::vector<ClipEdit>& edits)
{
	for (const ClipEdit& edit : edits)
	{
		if (edit.remove)
		{
			deleteClip(edit.clip->getTrack(), edit.clip);
			continue;
		}
		const tick_t offset = edit.clip->startTimeOffset().getTicks() + startOf(edit.clip) - edit.start;
		edit.clip->movePosition(TimePos(edit.start));
		edit.clip->changeLength(TimePos(edit.end - edit.start));
		edit.clip->setStartTimeOffset(TimePos(offset));
		edit.clip->setAutoResize(false);
	}
}

void checkpointTracks(const QVector<Track*>& tracks)
{
	QVector<JournallingObject*> objects;
	for (Track* track : tracks) { objects.append(track); }
	if (!objects.isEmpty()) { control::addUndoStep(objects); }
}

void checkpointOwners(const std::vector<Clip*>& clips)
{
	QVector<Track*> tracks;
	for (const Clip* clip : clips)
	{
		if (!tracks.contains(clip->getTrack())) { tracks.append(clip->getTrack()); }
	}
	checkpointTracks(tracks);
}

const Clip* firstLocked(const std::vector<Clip*>& clips)
{
	const auto found = std::find_if(clips.begin(), clips.end(), [](const Clip* clip) { return EditLocks::instance().locks(clip); });
	return found != clips.end() ? *found : nullptr;
}

tick_t SectionMove::shiftOf(tick_t tick) const
{
	if (tick >= start && tick < end) { return delta(); }
	if (!ripple) { return 0; }
	if (delta() > 0 && tick >= end && tick < passedEdge()) { return -length(); }
	if (delta() < 0 && tick >= passedEdge() && tick < start) { return length(); }
	return 0;
}

std::vector<Clip*> clipsTouchedBy(const SectionMove& move)
{
	// What may be cut or moved lies between the outermost of the section's edges and, with ripple, the third cut.
	const tick_t from = move.ripple ? std::min(move.start, move.passedEdge()) : move.start;
	const tick_t to = move.ripple ? std::max(move.end, move.passedEdge()) : move.end;
	std::vector<Clip*> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		for (Clip* clip : track->getClips())
		{
			if (endOf(clip) > from && startOf(clip) < to) { out.push_back(clip); }
		}
	}
	return out;
}

int applySectionMove(const SectionMove& move)
{
	int moved = 0;
	for (Track* track : Engine::getSong()->tracks())
	{
		splitTrackAt(track, move.start);
		splitTrackAt(track, move.end);
		if (move.ripple) { splitTrackAt(track, move.passedEdge()); }
		// A copy, read before anything moves: a clip must not be taken again at its new place.
		const auto clips = track->getClips();
		std::vector<tick_t> shifts;
		for (const Clip* clip : clips) { shifts.push_back(move.shiftOf(startOf(clip))); }
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (shifts[i] == 0) { continue; }
			clips[i]->movePosition(TimePos(startOf(clips[i]) + shifts[i]));
			++moved;
		}
	}
	return moved;
}

} // namespace arrangement

namespace
{

TrackFolder* folderWithId(int id)
{
	for (Track* track : Engine::getSong()->tracks())
	{
		if (track->id() == id) { return dynamic_cast<TrackFolder*>(track); }
	}
	return nullptr;
}

const FolderSpan* spanHolding(const std::vector<FolderSpan>& spans, tick_t tick)
{
	const auto found = std::find_if(spans.begin(), spans.end(), [tick](const FolderSpan& s) { return tick >= s.start && tick < s.end; });
	return found != spans.end() ? &*found : nullptr;
}

} // namespace

FolderSpanSelection& FolderSpanSelection::instance()
{
	static auto* selection = new FolderSpanSelection();
	return *selection;
}

std::vector<FolderSpanSelection::Selected> FolderSpanSelection::selected()
{
	std::vector<Selected> out;
	std::vector<Entry> kept;
	for (Entry& entry : m_entries)
	{
		TrackFolder* folder = folderWithId(entry.folderId);
		if (folder == nullptr) { continue; }
		if (!entry.anchor.isNull()) { entry.tick = entry.anchor->startPosition().getTicks(); }
		const std::vector<FolderSpan> spans = folderSummary(*folder);
		const FolderSpan* span = spanHolding(spans, entry.tick);
		if (span == nullptr) { continue; }
		kept.push_back(entry);
		const bool twice = std::any_of(out.begin(), out.end(), [&](const Selected& s) { return s.folder == folder && s.span.start == span->start; });
		if (!twice) { out.push_back({folder, *span}); }
	}
	m_entries = kept;
	return out;
}

bool FolderSpanSelection::holds(const TrackFolder* folder, const FolderSpan& span)
{
	const std::vector<Selected> all = selected();
	return std::any_of(all.begin(), all.end(), [&](const Selected& s) { return s.folder == folder && s.span.start == span.start; });
}

bool FolderSpanSelection::select(TrackFolder* folder, tick_t tick, bool add)
{
	const std::vector<FolderSpan> spans = folderSummary(*folder);
	const FolderSpan* span = spanHolding(spans, tick);
	if (span == nullptr) { return false; }
	// A loaded project's folders may reuse a selected folder's id.
	connect(Engine::getSong(), &Song::projectLoaded, this, &FolderSpanSelection::clear, Qt::UniqueConnection);
	if (!add) { m_entries.clear(); }
	const std::vector<Clip*> clips = folderSpanClips(*folder, *span);
	const auto first = std::min_element(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->startPosition() < b->startPosition(); });
	m_entries.push_back({folder->id(), *first, (*first)->startPosition().getTicks()});
	emit changed();
	return true;
}

void FolderSpanSelection::deselect(const TrackFolder* folder, const FolderSpan& span)
{
	selected();  // every entry's tick brought up to date
	m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
		return entry.folderId == folder->id() && entry.tick >= span.start && entry.tick < span.end;
	}), m_entries.end());
	emit changed();
}

void FolderSpanSelection::clear()
{
	if (m_entries.empty()) { return; }
	m_entries.clear();
	emit changed();
}

} // namespace lmms
