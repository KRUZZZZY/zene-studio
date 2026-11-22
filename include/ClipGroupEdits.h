/*
 * ClipGroupEdits.h - edits over a set of clips: summary clips and whole-song section moves
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

#ifndef LMMS_CLIP_GROUP_EDITS_H
#define LMMS_CLIP_GROUP_EDITS_H

#include <vector>

#include <QObject>
#include <QPointer>
#include <QVector>

#include "Clip.h"
#include "FolderSummary.h"
#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Track;
class TrackFolder;

namespace arrangement
{

//! What one clip of a group edit becomes: a new [start, end), or nothing at all.
struct ClipEdit
{
	Clip* clip = nullptr;
	tick_t start = 0;
	tick_t end = 0;
	bool remove = false;
};

enum class SpanEdge { Start, End };

/*! Q050-255: what dragging @a edge of summary clip @a span by @a delta does to its clips (appended to @a edits;
 *  a clip already there is left as it is, so a clip reached twice changes once). Lengthening takes the clips that
 *  end (or start) at that edge out to the new one; shortening cuts every clip crossing the new edge there and
 *  removes those wholly past it. The other clips of the span keep their place. */
LMMS_EXPORT void planSpanResize(const TrackFolder& folder, const FolderSpan& span, SpanEdge edge, tick_t delta,
	std::vector<ClipEdit>* edits);
//! The same drag of one ordinary clip's own edge; false (nothing planned) when the clip would vanish or start before 0.
LMMS_EXPORT bool planClipResize(Clip* clip, SpanEdge edge, tick_t delta, std::vector<ClipEdit>* edits);

//! Write @a edits. A head trim moves the clip's start offset with its start, so the content stays where it was on the
//! timeline - the rule of ClipView's left-edge drag and clip.trim.
LMMS_EXPORT void applyClipEdits(const std::vector<ClipEdit>& edits);

//! One undo checkpoint of every track holding one of @a clips (each track once).
LMMS_EXPORT void checkpointOwners(const std::vector<Clip*>& clips);
//! One undo checkpoint of @a tracks.
LMMS_EXPORT void checkpointTracks(const QVector<Track*>& tracks);
//! The first of @a clips a lock keeps from being edited (EditLocks), or nullptr.
LMMS_EXPORT const Clip* firstLocked(const std::vector<Clip*>& clips);

/*! Q050-219: the part of the song a section move from [@a start, @a end) to a new start @a target changes - the
 *  section's material, and with @a ripple the material it passes, which moves the opposite way by the section's length. */
struct SectionMove
{
	tick_t start = 0;
	tick_t end = 0;
	tick_t target = 0;
	bool ripple = false;

	tick_t length() const { return end - start; }
	tick_t delta() const { return target - start; }
	//! With ripple, where the material the section passes begins or ends: the third cut.
	tick_t passedEdge() const { return delta() > 0 ? target + length() : target; }
	//! How far a thing starting at @a tick moves.
	tick_t shiftOf(tick_t tick) const;
};
//! Every clip of the song's tracks @a move would cut or move.
LMMS_EXPORT std::vector<Clip*> clipsTouchedBy(const SectionMove& move);
//! Cut every track at the move's edges and move what lies inside; how many clips moved. The caller checkpoints.
LMMS_EXPORT int applySectionMove(const SectionMove& move);

} // namespace arrangement

/*! Q050-255: the summary clips the user (or an agent) has selected - interface state, never saved. A summary clip has no
 *  identity of its own (it is derived from its children), so each entry holds on to one child clip and to the tick
 *  it was last seen at: the entry follows that clip while it lives, and after an undo rebuilt the clips it finds the
 *  span at that tick; an entry with neither is gone. */
class LMMS_EXPORT FolderSpanSelection : public QObject
{
	Q_OBJECT
public:
	struct Selected
	{
		TrackFolder* folder = nullptr;
		FolderSpan span;
	};

	static FolderSpanSelection& instance();

	//! The selected summary clips as they are now, each once.
	std::vector<Selected> selected();
	bool holds(const TrackFolder* folder, const FolderSpan& span);
	bool isEmpty() { return selected().empty(); }

	//! Select the summary clip of @a folder holding @a tick, adding to the selection or replacing it; false if none does.
	bool select(TrackFolder* folder, tick_t tick, bool add);
	void deselect(const TrackFolder* folder, const FolderSpan& span);
	void clear();

signals:
	void changed();

private:
	struct Entry
	{
		int folderId = 0;
		QPointer<Clip> anchor;
		tick_t tick = 0;
	};

	FolderSpanSelection() = default;
	std::vector<Entry> m_entries;
};

} // namespace lmms

#endif // LMMS_CLIP_GROUP_EDITS_H
