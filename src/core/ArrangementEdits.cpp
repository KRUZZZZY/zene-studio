/*
 * ArrangementEdits.cpp - clip edits over a time range: split, shift, crop, the range itself
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

#include "ArrangementEdits.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "ContainedCrossfade.h"
#include "ConfigManager.h"
#include "ControlReversibility.h"
#include "EditLocks.h"
#include "ProjectJournal.h"
#include "ProjectMarkers.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace arrangement
{

void deleteClip(Track* track, Clip* clip)
{
	track->removeClip(clip);
	delete clip;
}

Clip* splitClipAt(Clip* clip, tick_t position)
{
	// A split changes this instance's independent window. The clone keeps the same
	// link id, so both pieces continue to show the shared content through their windows.
	return splitClipAlone(clip, position);
}

Clip* splitClipAlone(Clip* clip, tick_t position)
{
	const tick_t start = clip->startPosition().getTicks();
	const tick_t end = clip->endPosition().getTicks();
	if (position <= start || position >= end) { return nullptr; }
	// ClipView::splitClip's recipe: clone, then resize both halves; the right half keeps its
	// content where it was by moving its start offset back by the left half's length.
	Track* track = clip->getTrack();
	track->saveJournallingState(false);
	Clip* right = clip->clone();
	clip->changeLength(TimePos(position - start));
	clip->setAutoResize(false);
	right->movePosition(TimePos(position));
	right->changeLength(TimePos(end - position));
	right->setStartTimeOffset(clip->startTimeOffset() - clip->length());
	right->setAutoResize(false);
	track->restoreJournallingState();
	return right;
}

int splitTrackAt(Track* track, tick_t position)
{
	int split = 0;
	const auto clips = track->getClips();  // a copy: splitting adds clips
	for (Clip* clip : clips)
	{
		if (splitClipAt(clip, position) != nullptr) { ++split; }
	}
	return split;
}

QVector<Clip*> clipsWithin(Track* track, tick_t start, tick_t end)
{
	QVector<Clip*> out;
	for (Clip* clip : track->getClips())
	{
		if (clip->startPosition().getTicks() >= start && clip->endPosition().getTicks() <= end) { out.append(clip); }
	}
	return out;
}

int shiftClipsFrom(Track* track, tick_t from, tick_t delta)
{
	int moved = 0;
	for (Clip* clip : track->getClips())
	{
		const tick_t start = clip->startPosition().getTicks();
		if (start < from) { continue; }
		clip->movePosition(TimePos(std::max<tick_t>(0, start + delta)));
		++moved;
	}
	return moved;
}

int deleteRange(Track* track, tick_t start, tick_t end, bool ripple)
{
	splitTrackAt(track, start);
	splitTrackAt(track, end);
	const QVector<Clip*> inside = clipsWithin(track, start, end);
	for (Clip* clip : inside) { deleteClip(track, clip); }
	if (ripple) { shiftClipsFrom(track, end, start - end); }
	return static_cast<int>(inside.size());
}

int cropToRange(Track* track, tick_t start, tick_t end)
{
	splitTrackAt(track, start);
	splitTrackAt(track, end);
	int removed = 0;
	const auto clips = track->getClips();
	for (Clip* clip : clips)
	{
		if (clip->startPosition().getTicks() >= start && clip->endPosition().getTicks() <= end) { continue; }
		deleteClip(track, clip);
		++removed;
	}
	return removed;
}

Modes modes()
{
	ConfigManager* config = ConfigManager::inst();
	Modes out;
	out.ripple = static_cast<Ripple>(std::clamp(config->value(QStringLiteral("arrangement"), QStringLiteral("ripple"), QStringLiteral("0")).toInt(), 0, 2));
	// 0.5.0 theme 2: with nothing chosen, a dragged clip trims what it lands on (Overlap::Trim).
	out.overlap = static_cast<Overlap>(std::clamp(config->value(QStringLiteral("arrangement"), QStringLiteral("overlap"), QStringLiteral("1")).toInt(), 0, 2));
	out.snapToEdges = config->value(QStringLiteral("arrangement"), QStringLiteral("snapedges"), QStringLiteral("1")).toInt() != 0;
	out.automationFollowsClips = config->value(QStringLiteral("arrangement"), QStringLiteral("followauto"), QStringLiteral("1")).toInt() != 0;
	out.autoFadeMs = std::clamp(config->value(QStringLiteral("arrangement"), QStringLiteral("autofade"), QStringLiteral("0")).toInt(), 0, MaxAutoFadeMs);
	out.showTransients = config->value(QStringLiteral("arrangement"), QStringLiteral("showtransients"), QStringLiteral("0")).toInt() != 0;
	out.snapToZeroCrossings = config->value(QStringLiteral("arrangement"), QStringLiteral("zerocross"), QStringLiteral("0")).toInt() != 0;
	return out;
}

namespace
{

std::atomic<int> s_autoFadeMs{0};

} // namespace

int autoFadeMs() { return s_autoFadeMs.load(std::memory_order_relaxed); }

void loadModes() { s_autoFadeMs.store(modes().autoFadeMs, std::memory_order_relaxed); }

void setModes(const Modes& modes)
{
	ConfigManager* config = ConfigManager::inst();
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("ripple"), QString::number(static_cast<int>(modes.ripple)));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("overlap"), QString::number(static_cast<int>(modes.overlap)));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("snapedges"), modes.snapToEdges ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("followauto"), modes.automationFollowsClips ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("showtransients"), modes.showTransients ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("zerocross"), modes.snapToZeroCrossings ? QStringLiteral("1") : QStringLiteral("0"));
	const int autoFade = std::clamp(modes.autoFadeMs, 0, MaxAutoFadeMs);
	config->setValue(QStringLiteral("arrangement"), QStringLiteral("autofade"), QString::number(autoFade));
	s_autoFadeMs.store(autoFade, std::memory_order_relaxed);
}

QVector<Track*> rippleTracks(Track* track, Ripple mode)
{
	if (mode == Ripple::Off) { return {}; }
	if (mode == Ripple::Track) { return {track}; }
	QVector<Track*> all;
	for (Track* each : Engine::getSong()->tracks()) { all.append(each); }
	return all;
}

void checkpointForDelete(Track* track, Ripple mode)
{
	QVector<JournallingObject*> touched;
	for (Track* each : mode == Ripple::All ? rippleTracks(track, mode) : QVector<Track*>{track}) { touched.append(each); }
	control::addUndoStep(touched);
}

namespace
{

void mergeFrom(ProjectJournal* journal, int mark)
{
	if (journal != nullptr && mark >= 0) { journal->mergeCheckpointsFrom(mark); }
}

} // namespace

void checkpointRipple(Track* track)
{
	for (Track* each : rippleTracks(track, modes().ripple))
	{
		if (each != track) { each->addJournalCheckPoint(); }
	}
}

int rippleShift(Track* track, tick_t from, tick_t delta, const Clip* keep)
{
	int moved = 0;
	if (delta == 0) { return moved; }
	for (Track* each : rippleTracks(track, modes().ripple))
	{
		for (Clip* clip : each->getClips())
		{
			const tick_t start = clip->startPosition().getTicks();
			if (clip == keep || start < from) { continue; }
			clip->movePosition(TimePos(std::max<tick_t>(0, start + delta)));
			++moved;
		}
	}
	return moved;
}

void rippleAfterMove(Clip* moved, tick_t oldStart, bool ripple)
{
	// ED-043: the clips after the moved clip's old end follow it by the same distance (a shuffle).
	if (!ripple) { return; }
	checkpointRipple(moved->getTrack());
	rippleShift(moved->getTrack(), oldStart + moved->length().getTicks(), moved->startPosition().getTicks() - oldStart, moved);
}

namespace
{

bool movedOnATrack(const Clip* moved, tick_t oldStart) { return moved != nullptr && moved->getTrack() != nullptr && oldStart >= 0; }

} // namespace

bool afterUserMove(Clip* moved, tick_t oldStart, QString* skipped, bool containerCrossfade)
{
	if (!movedOnATrack(moved, oldStart)) { return true; }
	const tick_t newStart = moved->startPosition().getTicks();
	// A press released where it started (a click, or a drag back) moved nothing onto anything.
	if (newStart == oldStart) { return true; }
	const Overlap mode = containerCrossfade ? Overlap::Crossfade : modes().overlap;
	// 0.5.0 theme 2: a drop that would trim or fade a locked clip is refused - the clip goes back.
	if (lockedUnder(moved, newStart, moved->endPosition().getTicks(), mode) != nullptr)
	{
		moved->movePosition(TimePos(oldStart));
		return false;
	}
	const bool follow = modes().automationFollowsClips;
	const bool ripple = modes().ripple != Ripple::Off;
	if (mode == Overlap::Free && !follow && !ripple) { return true; }
	// ONE undo step for the whole gesture (review W2a-1 #3): the track's checkpoint is taken at the
	// PRE-drag position - the clip is put back for it - and merged with the clip checkpoint the
	// press took, so Ctrl+Z returns to before the drag, never to "moved but not yet trimmed".
	ProjectJournal* journal = Engine::projectJournal();
	const int mark = journal != nullptr ? journal->undoDepth() - 1 : -1;
	Track* track = moved->getTrack();
	moved->movePosition(TimePos(oldStart));
	track->addJournalCheckPoint();
	moved->movePosition(TimePos(newStart));
	rippleAfterMove(moved, oldStart, ripple);
	if (follow) { moveFollowingAutomation(track, oldStart, moved->length().getTicks(), newStart - oldStart, true); }
	applyOverlap(moved, mode, skipped, containerCrossfade);
	mergeFrom(journal, mark);
	return true;
}

int moveFollowingAutomation(Track* track, tick_t oldStart, tick_t length, tick_t delta, bool checkpoint)
{
	if (!modes().automationFollowsClips || delta == 0) { return 0; }
	int changed = 0;
	for (Track* each : Engine::getSong()->tracks())
	{
		auto* lane = dynamic_cast<AutomationTrack*>(each);
		if (lane == nullptr || lane->followsTrack() != track->id()) { continue; }
		for (Clip* clip : lane->getClips())
		{
			auto* automation = dynamic_cast<AutomationClip*>(clip);
			if (automation == nullptr) { continue; }
			// The clip's points are keyed from its start less its start offset, as playback reads them.
			const tick_t origin = clip->startPosition().getTicks() + clip->startTimeOffset().getTicks();
			if (checkpoint) { automation->addJournalCheckPoint(); }
			automation->transformRange(oldStart - origin, oldStart + length - 1 - origin, 1.0, delta, 1.0, 0.0);
		}
		++changed;
	}
	return changed;
}

int rippleAfterDelete(Track* track, tick_t start, tick_t length, Ripple mode)
{
	int moved = 0;
	for (Track* each : rippleTracks(track, mode)) { moved += shiftClipsFrom(each, start + length, -length); }
	return moved;
}

namespace
{

//! The explicit container gesture fades a containing pair at both inner edges. 0 when neither contains the other,
//! 1 when the ramps were written, -1 (with \a why) when authored inner fades do not fit.
int crossfadeContainer(Clip* a, Clip* b, QString* why)
{
	const Clip* container = nullptr;
	const Clip* contained = nullptr;
	if (!containedcrossfade::containment(a, b, &container, &contained)) { return 0; }
	int entry = 0;
	int exit = 0;
	const QString invalidRamps = containedcrossfade::ramps(*container, *contained, 0, true, &entry, &exit);
	if (!invalidRamps.isEmpty())
	{
		if (why != nullptr) { *why = invalidRamps; }
		return -1;
	}
	Clip* outer = container == a ? a : b;
	Clip* inner = outer == a ? b : a;
	ClipEdits outerEdits = outer->clipEdits();
	ClipEdits innerEdits = inner->clipEdits();
	containedcrossfade::apply(*container, *contained, entry, exit, FadeShape::EqualPower, FadeShape::EqualPower, &outerEdits, &innerEdits);
	outer->setClipEdits(outerEdits);
	inner->setClipEdits(innerEdits);
	return 1;
}

//! Crossfade @a earlier into @a later over their overlap using edge-anchored ramps.
bool crossfade(Clip* earlier, Clip* later)
{
	const tick_t overlap = std::min(earlier->endPosition().getTicks(), later->endPosition().getTicks())
		- later->startPosition().getTicks();
	if (overlap <= 0) { return true; }
	ClipEdits out = earlier->clipEdits();
	out.fadeInOffsetTicks = 0;
	out.fadeOutTicks = overlap;
	out.fadeOutShape = FadeShape::EqualPower;
	out.fadeOutOffsetTicks = 0;
	out.fadeOffsetGap = false;
	earlier->setClipEdits(out);
	ClipEdits in = later->clipEdits();
	in.fadeOutOffsetTicks = 0;
	in.fadeInTicks = overlap;
	in.fadeInShape = FadeShape::EqualPower;
	in.fadeInOffsetTicks = 0;
	in.fadeOffsetGap = false;
	later->setClipEdits(in);
	return true;
}

} // namespace

namespace
{

//! What the overlap mode does to ONE clip @a other under @a moved (which spans [start, end)). False when it could
//! do nothing (a crossfade of a clip too short for it, with \a why).
bool overlapOne(Clip* moved, Clip* other, Overlap mode, tick_t start, tick_t end, bool containerGesture, QString* why)
{
	if (mode == Overlap::Crossfade)
	{
		if (containerGesture)
		{
			const int container = crossfadeContainer(moved, other, why);
			if (container != 0) { return container > 0; }
		}
		return other->startPosition().getTicks() < start ? crossfade(other, moved) : crossfade(moved, other);
	}
	// Trim: cut the other clip at the moved clip's edges and drop what lies under it.
	// Each resulting piece keeps its own independent window and the original link id.
	splitClipAlone(other, end);
	Clip* inside = splitClipAlone(other, start);
	Clip* under = inside != nullptr ? inside : other;
	if (under->startPosition().getTicks() >= start && under->endPosition().getTicks() <= end) { deleteClip(moved->getTrack(), under); }
	return true;
}

} // namespace

QVector<OverlapEffect> overlapEffects(const Clip* moved, tick_t start, tick_t end)
{
	QVector<OverlapEffect> out;
	if (moved == nullptr || moved->getTrack() == nullptr) { return out; }
	for (Clip* other : moved->getTrack()->getClips())
	{
		const tick_t otherStart = other->startPosition().getTicks();
		const tick_t otherEnd = other->endPosition().getTicks();
		if (other == moved || otherEnd <= start || otherStart >= end) { continue; }
		out.append({other, std::max(start, otherStart), std::min(end, otherEnd), EditLocks::instance().locks(other)});
	}
	return out;
}

const Clip* lockedUnder(const Clip* moved, tick_t start, tick_t end, Overlap mode)
{
	if (mode == Overlap::Free) { return nullptr; }
	for (const OverlapEffect& effect : overlapEffects(moved, start, end))
	{
		if (effect.locked) { return effect.clip; }
	}
	return nullptr;
}

int applyOverlap(Clip* moved, Overlap mode, QString* skipped, bool containerGesture)
{
	const Overlap effectiveMode = containerGesture ? Overlap::Crossfade : mode;
	if (effectiveMode == Overlap::Free) { return 0; }
	const tick_t start = moved->startPosition().getTicks();
	const tick_t end = moved->endPosition().getTicks();
	int changed = 0;
	// overlapEffects() is a copy taken before anything is split, so the splits below do not feed it.
	for (const OverlapEffect& effect : overlapEffects(moved, start, end))
	{
		if (effect.locked) { continue; }  // a lock protects it (the drop or the command was refused already)
		QString why;
		if (overlapOne(moved, effect.clip, effectiveMode, start, end, containerGesture, &why)) { ++changed; }
		else if (skipped != nullptr && skipped->isEmpty()) { *skipped = why; }
	}
	return changed;
}

SnapHit snapToEdgesHit(tick_t proposed, tick_t length, const Clip* moving, tick_t tolerance)
{
	SnapHit best{proposed, proposed, SnapHit::Kind::None, QString()};
	// A tick is 32 bits, so both the tolerance and the difference between two positions can wrap.
	// Measuring in 64 bits keeps a maximum tolerance (and a candidate at the far end of the song)
	// comparable with the best distance so far.
	std::int64_t bestDistance = std::int64_t{tolerance} + 1;
	const auto consider = [&](tick_t edge, SnapHit::Kind kind, const QString& label) {
		for (const tick_t candidate : {edge, edge - length})  // the start lands there, or the end does
		{
			if (candidate < 0) { continue; }  // a start before the song's beginning cannot be snapped to
			const std::int64_t distance = std::abs(std::int64_t{candidate} - std::int64_t{proposed});
			if (distance < bestDistance) { best = {candidate, edge, kind, label}; bestDistance = distance; }
		}
	};
	const QString clipEdge = QStringLiteral("Clip edge");
	for (Track* track : Engine::getSong()->tracks())
	{
		for (const Clip* clip : track->getClips())
		{
			if (clip == moving) { continue; }
			consider(clip->startPosition().getTicks(), SnapHit::Kind::ClipEdge, clipEdge);
			consider(clip->endPosition().getTicks(), SnapHit::Kind::ClipEdge, clipEdge);
		}
	}
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		consider(marker.tick, SnapHit::Kind::Marker, QStringLiteral("Marker: %1").arg(marker.name));
	}
	return best;
}

tick_t snapToEdges(tick_t proposed, tick_t length, const Clip* moving, tick_t tolerance)
{
	return snapToEdgesHit(proposed, length, moving, tolerance).start;
}

tick_t anchoredViewStart(tick_t viewStart, int widthPx, double ppbBefore, double ppbAfter, tick_t anchor)
{
	const double ticksPerBar = TimePos::ticksPerBar();
	const double width = std::max(widthPx, 1);
	const double visibleBefore = width * ticksPerBar / std::max(ppbBefore, 1.0);
	double fraction = (anchor - viewStart) / visibleBefore;
	if (fraction < 0.0 || fraction > 1.0) { fraction = 0.5; }  // off screen before: into the middle
	const double visibleAfter = width * ticksPerBar / std::max(ppbAfter, 1.0);
	return std::max<tick_t>(0, static_cast<tick_t>(std::lround(anchor - fraction * visibleAfter)));
}

} // namespace arrangement

ArrangerRange& ArrangerRange::instance()
{
	static ArrangerRange* range = new ArrangerRange();
	return *range;
}

QVector<Track*> ArrangerRange::tracks() const
{
	QVector<Track*> out;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (m_allTracks || m_trackIds.contains(track->id())) { out.append(track); }
	}
	return out;
}

void ArrangerRange::set(tick_t start, tick_t end, const QVector<int>& trackIds, bool allTracks)
{
	m_start = std::max<tick_t>(0, std::min(start, end));
	m_end = std::max<tick_t>(0, std::max(start, end));
	m_trackIds = trackIds;
	m_allTracks = allTracks || trackIds.isEmpty();
	emit changed();
}

void ArrangerRange::clear()
{
	m_start = m_end = 0;
	m_trackIds.clear();
	m_allTracks = true;
	emit changed();
}

} // namespace lmms
