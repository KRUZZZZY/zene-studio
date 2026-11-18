/*
 * ArrangerZoom.cpp - the arranger's zoom, kept around what the user is looking at
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

#include "ArrangerZoom.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QElapsedTimer>
#include <QScrollBar>
#include <QVector>

#include "ArrangementEdits.h"
#include "AutomatableModel.h"
#include "Clip.h"
#include "ClipView.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"

namespace lmms::gui::arrangerzoom
{

namespace
{

constexpr int HistoryDepth = 32;
constexpr double WheelNotch = 120.0;  //!< QWheelEvent's angleDelta for one mouse notch
QVector<int> s_history;  //!< earlier zoom model values, newest last (UI thread only)
QVector<int> s_rewalked;  //!< zoom model values a back step left behind, newest last: a forward step's (UI thread)
bool s_toggleValid = false;  //!< whether a toggle extent is known (UI thread only)
int s_toggleValue = 0;  //!< the extent the current zoom last left, the zoom toggle swaps to (UI thread only)

//! The wheel gesture under way (UI thread only): the part of a step not yet zoomed, and whether it is remembered.
struct WheelGesture
{
	QElapsedTimer since;
	double remainder = 0.0;
	bool remembered = false;
};
WheelGesture s_wheel;

//! Keep the two histories at the depth cap, oldest entry first out.
void trim()
{
	while (s_history.size() > HistoryDepth) { s_history.removeFirst(); }
	while (s_rewalked.size() > HistoryDepth) { s_rewalked.removeFirst(); }
}

//! Zooms before the same value are useless history (go back would answer the value the view is at).
bool tautology(int value) { return !s_history.isEmpty() && s_history.last() == value; }

//! Queue @a value as an extent to go back to (walk only: no forward break, as remember does).
void pushPast(int value)
{
	if (tautology(value)) { return; }
	s_history.append(value);
	trim();
}

} // namespace

bool selectedClipsSpan(SongEditor* editor, tick_t* begin, tick_t* end)
{
	*begin = std::numeric_limits<tick_t>::max();
	*end = 0;
	for (selectableObject* object : editor->selectedObjects())
	{
		auto* view = dynamic_cast<ClipView*>(object);
		if (view == nullptr) { continue; }
		*begin = std::min<tick_t>(*begin, view->getClip()->startPosition().getTicks());
		*end = std::max<tick_t>(*end, view->getClip()->endPosition().getTicks());
	}
	return *end > *begin;
}

int visibleWidth(SongEditor* editor)
{
	return std::max(1, editor->timeLine()->width() - editor->timeLine()->markerX(editor->currentPosition()));
}

tick_t visibleTicks(SongEditor* editor)
{
	return static_cast<tick_t>(visibleWidth(editor) * TimePos::ticksPerBar() / std::max(editor->pixelsPerBar(), 1.f));
}

tick_t tickAtGlobal(SongEditor* editor, const QPoint& global)
{
	const int x = editor->timeLine()->mapFromGlobal(global).x() - editor->timeLine()->markerX(editor->currentPosition());
	const double ticks = editor->currentPosition().getTicks() + x * TimePos::ticksPerBar() / std::max(editor->pixelsPerBar(), 1.f);
	return std::max<tick_t>(0, static_cast<tick_t>(ticks));
}

bool selectionSpan(SongEditor* editor, tick_t* begin, tick_t* end)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (range.active())
	{
		*begin = range.start();
		*end = range.end();
		return true;
	}
	return selectedClipsSpan(editor, begin, end);
}

tick_t playhead(SongEditor* editor, bool* shown)
{
	const tick_t at = editor->timeLine()->timeline()->pos().getTicks();
	const tick_t start = editor->currentPosition().getTicks();
	if (shown != nullptr) { *shown = at >= start && at <= start + visibleTicks(editor); }
	return at;
}

tick_t keyboardAnchor(SongEditor* editor)
{
	tick_t begin = 0;
	tick_t end = 0;
	if (selectionSpan(editor, &begin, &end)) { return begin + (end - begin) / 2; }
	bool shown = false;
	const tick_t at = playhead(editor, &shown);
	return shown ? at : editor->currentPosition().getTicks() + visibleTicks(editor) / 2;
}

void keepInPlace(SongEditor* editor, tick_t anchor, double ppbBefore, tick_t startBefore)
{
	const double after = editor->pixelsPerBar();
	if (after == ppbBefore) { return; }
	editor->horizontalScroll()->setValue(static_cast<int>(
		arrangement::anchoredViewStart(startBefore, visibleWidth(editor), ppbBefore, after, anchor)));
}

void zoomAround(SongEditor* editor, int value, tick_t anchor)
{
	const double before = editor->pixelsPerBar();
	const tick_t start = editor->currentPosition().getTicks();
	editor->zoomingModel()->setValue(value);
	keepInPlace(editor, anchor, before, start);
}

void wheelZoom(SongEditor* editor, int delta, bool fine, tick_t anchor)
{
	// A pause ends the gesture; a turn the other way keeps it but not the leftover of the turn before.
	const double steps = delta / WheelNotch * (fine ? 1 : WheelNotchSteps);
	const bool newGesture = !s_wheel.since.isValid() || s_wheel.since.elapsed() > WheelBurstMs;
	if (newGesture) { s_wheel.remembered = false; }
	if (newGesture || s_wheel.remainder * steps < 0) { s_wheel.remainder = 0.0; }
	s_wheel.since.start();
	s_wheel.remainder += steps;
	const int whole = static_cast<int>(std::trunc(s_wheel.remainder));
	if (whole == 0) { return; }
	s_wheel.remainder -= whole;
	IntModel* model = editor->zoomingModel();
	const int before = model->value();
	zoomAround(editor, std::clamp(before + whole, model->minValue(), model->maxValue()), anchor);
	if (model->value() != before && !s_wheel.remembered)
	{
		remember(before);
		s_wheel.remembered = true;
	}
}

void remember(int value)
{
	s_rewalked.clear();  // a new zoom breaks forward
	s_toggleValid = true;
	s_toggleValue = value;  // the extent left behind
	if (tautology(value)) { return; }
	s_history.append(value);
	trim();
}

void finishPinch(int startValue, int currentValue, bool ended)
{
	if (ended && currentValue != startValue) { remember(startValue); }
}

bool takeEarlier(int current, int* earlier)
{
	if (s_history.isEmpty()) { return false; }
	*earlier = s_history.takeLast();
	s_rewalked.append(current);  // so arrangement.zoom_forward can come back to it
	trim();
	s_toggleValid = true;
	s_toggleValue = current;  // the extent this step left behind is what the toggle swaps to
	return true;
}

bool takeNext(int current, int* next)
{
	if (s_rewalked.isEmpty()) { return false; }
	*next = s_rewalked.takeLast();
	pushPast(current);
	s_toggleValid = true;
	s_toggleValue = current;
	return true;
}

bool takeToggle(int current, int* extent)
{
	if (!s_toggleValid || s_toggleValue == current) { return false; }
	*extent = s_toggleValue;
	// The swap is a zoom like any other, so the walk must agree with where it lands (the round-1
	// review: a stale entry made Back succeed without changing the zoom). The extent swapped to sits
	// on top of one of the stacks - it is the extent the last zoom or walk step left - so it comes
	// off there (the view is at it now), and the extent left behind queues in its place, so Back and
	// Forward keep stepping over real changes only.
	const int left = current;
	if (!s_history.isEmpty() && s_history.last() == *extent)
	{
		s_history.removeLast();
		s_rewalked.append(left);  // so arrangement.zoom_forward can return over the swap
	}
	else if (!s_rewalked.isEmpty() && s_rewalked.last() == *extent)
	{
		s_rewalked.removeLast();
		pushPast(left);  // so arrangement.zoom_back can return over the swap
	}
	trim();
	s_toggleValue = left;  // the extent this swap left behind: pressing it again swaps back
	return true;
}

int historySize() { return static_cast<int>(s_history.size()); }

int forwardSize() { return static_cast<int>(s_rewalked.size()); }

} // namespace lmms::gui::arrangerzoom
