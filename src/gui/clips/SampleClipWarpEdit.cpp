/*
 * SampleClipWarpEdit.cpp - warp markers over a sample clip's waveform: the guides, the hit test, the snap and the
 *                          approximate waveform preview of a marker being dragged (Q050-063)
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

#include "SampleClipWarpEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QPainter>
#include <QPolygon>

namespace lmms::gui
{

namespace
{

constexpr int GripSize = 7;
constexpr int GuideReach = 5;  //!< how far from a guide's line a press still takes it

int xOfGuide(const ClipOverlayFrame& frame, int tick)
{
	return frame.xOrigin + static_cast<int>(std::lround(tick * static_cast<double>(frame.pixelsPerTick)));
}

} // namespace

std::vector<WarpGuide> warpGuides(const WarpMarkers& map, int originShiftTicks)
{
	std::vector<WarpGuide> guides;
	guides.reserve(static_cast<std::size_t>(map.size()));
	for (const WarpMarker& marker : map.all())
	{
		guides.push_back({marker.sourceFrame, marker.offsetTicks, static_cast<int>(marker.offsetTicks) + originShiftTicks});
	}
	return guides;
}

void paintWarpGuides(QPainter& p, const std::vector<WarpGuide>& guides, const ClipOverlayFrame& frame,
	const QColor& colour, int active)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing, false);
	const int bottom = frame.top + frame.height;
	for (std::size_t i = 0; i < guides.size(); ++i)
	{
		const int x = xOfGuide(frame, guides[i].tick);
		const bool heavy = static_cast<int>(i) == active;
		p.setPen(QPen(colour, heavy ? 2 : 1));
		p.drawLine(x, frame.top, x, bottom);
		// The grip: a small downward flag at the top edge, the thing to take hold of.
		p.setPen(Qt::NoPen);
		p.setBrush(colour);
		p.drawPolygon(QPolygon({QPoint(x - GripSize / 2, frame.top), QPoint(x + GripSize / 2, frame.top),
			QPoint(x, frame.top + GripSize)}));
	}
	p.restore();
}

int warpGuideAt(const std::vector<WarpGuide>& guides, const ClipOverlayFrame& frame, const QPoint& pos)
{
	int found = -1;
	int best = GuideReach + 1;
	for (std::size_t i = 0; i < guides.size(); ++i)
	{
		const int distance = std::abs(pos.x() - xOfGuide(frame, guides[i].tick));
		if (distance < best) { best = distance; found = static_cast<int>(i); }
	}
	return found;
}

tick_t warpOffsetAtX(int x, const ClipOverlayFrame& frame, int originShiftTicks)
{
	const double tick = (x - frame.xOrigin) / std::max(0.001, static_cast<double>(frame.pixelsPerTick));
	return static_cast<tick_t>(std::lround(tick)) - originShiftTicks;
}

tick_t snapWarpOffset(tick_t wanted, WarpSnap snap, tick_t originTicks, int gridTicks,
	const std::vector<int>& transientOffsets, int reachTicks, tick_t low, tick_t high)
{
	tick_t target = wanted;
	if (snap == WarpSnap::Grid && gridTicks > 0)
	{
		const double steps = static_cast<double>(wanted + originTicks) / gridTicks;
		target = static_cast<tick_t>(std::lround(steps)) * gridTicks - originTicks;
	}
	else if (snap == WarpSnap::Transient)
	{
		int nearest = reachTicks + 1;
		for (const int offset : transientOffsets)
		{
			const int distance = std::abs(static_cast<int>(offset - wanted));
			if (distance < nearest) { nearest = distance; target = offset; }
		}
	}
	return std::clamp(target, low, std::max(low, high));
}

std::vector<WarpWaveformSegment> warpWaveformSegments(const WarpMarkers& map, f_cnt_t sourceIn, f_cnt_t sourceOut,
	f_cnt_t bufferFrames, float framesPerTick, int originShiftTicks, const ClipOverlayFrame& frame)
{
	std::vector<WarpWaveformSegment> segments;
	if (bufferFrames == 0 || sourceOut <= sourceIn) { return segments; }

	// The cut points: the window's edges and every marker inside it.
	std::vector<f_cnt_t> cuts{sourceIn};
	for (const WarpMarker& marker : map.all())
	{
		if (marker.sourceFrame > sourceIn && marker.sourceFrame < sourceOut) { cuts.push_back(marker.sourceFrame); }
	}
	cuts.push_back(sourceOut);

	const auto xOf = [&](f_cnt_t sourceFrame)
	{
		return xOfGuide(frame, static_cast<int>(map.timelineOffsetAt(sourceFrame, sourceIn, framesPerTick)) + originShiftTicks);
	};
	for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
	{
		const int x0 = xOf(cuts[i]);
		const int x1 = xOf(cuts[i + 1]);
		if (x1 <= x0) { continue; }
		segments.push_back({x0, x1, static_cast<double>(cuts[i]) / bufferFrames, static_cast<double>(cuts[i + 1]) / bufferFrames});
	}
	return segments;
}

} // namespace lmms::gui
