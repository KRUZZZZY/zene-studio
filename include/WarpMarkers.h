/*
 * WarpMarkers.h - the warp map: markers that pin source frames to the timeline
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_WARP_MARKERS_H
#define LMMS_WARP_MARKERS_H

#include <algorithm>
#include <array>
#include <span>

#include "LmmsTypes.h"

namespace lmms
{

/*! One warp marker: the source frame it pins, and where that frame lands.
 *
 *  `offsetTicks` is measured from the clip's own origin (`startPosition() +
 *  startTimeOffset()`), not from the project start. That is what makes a warp
 *  independent of BOTH ends of an edit that would otherwise move it:
 *
 *  - **trim** moves `sourceIn`/`sourceOut` in the frame domain; the marker's
 *    `sourceFrame` is a position on the audio, so it does not move with them;
 *  - **moving the clip** moves the timeline under its audio; a clip-relative
 *    `offsetTicks` moves with the clip, so the warp is preserved.
 *
 *  The model's mapping functions still speak absolute timeline positions: the
 *  clip adds its own origin back (docs/CLIP-CAPTURE-DESIGN.md §2.4).
 */
struct WarpMarker
{
	f_cnt_t sourceFrame = 0;
	tick_t offsetTicks = 0;

	bool operator==(const WarpMarker& other) const
	{
		return sourceFrame == other.sourceFrame && offsetTicks == other.offsetTicks;
	}
	bool operator!=(const WarpMarker& other) const { return !(*this == other); }
};

/*! A monotonic marker set and the piecewise-linear map it defines.
 *
 *  The set is a value type with a **fixed capacity**: no marker operation, and
 *  no lookup, allocates. `set()`/`append()` are authoring calls (control
 *  thread, stack only); `sourceFrameAt`/`timelineOffsetAt`/`framesPerTickAt`
 *  are pure, allocation-free and lock-free, because the audio thread calls them
 *  (docs/CONVENTIONS.md, realtime rule I8).
 *
 *  **The mapping.** With no markers there is no warp: the clip's own linear map
 *  is the whole answer and this class is never consulted. With markers, the
 *  markers *are* the mapping:
 *
 *  - a segment between two markers interpolates **linearly in both
 *    coordinates** — a constant source-frames-per-tick rate for that segment,
 *    which is what makes a marker pair a tempo change and not a curve;
 *  - outside the outermost markers the map continues at the clip's own base
 *    rate (`baseFramesPerTick`), extrapolated from the nearest marker, so the
 *    function is **continuous at every marker** — the intercept comes from the
 *    marker, not from the clip origin, because anchoring to the origin instead
 *    would put a step at the first marker.
 *
 *  Two properties follow and are the ones a warp engine is judged on:
 *  **monotonic** (every segment and both tails increase in both coordinates)
 *  and **exact at the markers** (each marker's own source frame is returned for
 *  its own offset, with no interpolation error, because the interpolation
 *  numerator is zero there).
 */
class WarpMarkers
{
public:
	//! A fixed capacity is what keeps every marker operation allocation-free.
	static constexpr int MaxMarkers = 128;

	bool empty() const { return m_count == 0; }
	int size() const { return m_count; }
	const WarpMarker& operator[](int index) const { return m_markers[static_cast<std::size_t>(index)]; }
	std::span<const WarpMarker> all() const
	{
		return { m_markers.data(), static_cast<std::size_t>(m_count) };
	}

	void clear() { m_count = 0; }

	/*! Replaces the whole set (authoring call).
	 *
	 *  The set is sorted by source frame and **rejected outright** — returning
	 *  false and leaving the previous set intact — unless it is strictly
	 *  increasing in both coordinates. A non-monotonic set is not a mapping,
	 *  and a warp engine that accepted one would not be trustworthy.
	 */
	bool set(std::span<const WarpMarker> markers)
	{
		if (markers.size() > static_cast<std::size_t>(MaxMarkers)) { return false; }

		std::array<WarpMarker, MaxMarkers> sorted{};
		std::copy(markers.begin(), markers.end(), sorted.begin());
		std::sort(sorted.begin(), sorted.begin() + markers.size(),
			[](const WarpMarker& a, const WarpMarker& b) { return a.sourceFrame < b.sourceFrame; });

		for (std::size_t i = 1; i < markers.size(); ++i)
		{
			if (sorted[i].sourceFrame == sorted[i - 1].sourceFrame
				|| sorted[i].offsetTicks <= sorted[i - 1].offsetTicks)
			{
				return false;
			}
		}

		std::copy(sorted.begin(), sorted.begin() + markers.size(), m_markers.begin());
		m_count = static_cast<int>(markers.size());
		return true;
	}

	//! Adds one marker, keeping the set sorted; false if it would break the order.
	bool append(const WarpMarker& marker)
	{
		if (m_count >= MaxMarkers) { return false; }
		std::array<WarpMarker, MaxMarkers> next{};
		std::copy(m_markers.begin(), m_markers.begin() + m_count, next.begin());
		next[static_cast<std::size_t>(m_count)] = marker;
		return set(std::span<const WarpMarker>(next.data(), static_cast<std::size_t>(m_count) + 1));
	}

	bool operator==(const WarpMarkers& other) const
	{
		return m_count == other.m_count
			&& std::equal(m_markers.begin(), m_markers.begin() + m_count, other.m_markers.begin());
	}
	bool operator!=(const WarpMarkers& other) const { return !(*this == other); }

	//! The source frame heard at `offsetTicks` from the clip's origin.
	f_cnt_t sourceFrameAt(tick_t offsetTicks, f_cnt_t sourceIn, f_cnt_t sourceOut,
		float baseFramesPerTick) const
	{
		if (m_count == 0) { return sourceIn; }
		const auto& first = m_markers[0];
		const auto& last = m_markers[static_cast<std::size_t>(m_count - 1)];
		const auto rate = baseFramesPerTick > 0.0f ? baseFramesPerTick : 1.0f;

		f_cnt_t frame;
		if (offsetTicks <= first.offsetTicks)
		{
			const auto below = static_cast<f_cnt_t>((first.offsetTicks - offsetTicks) * rate);
			frame = below > first.sourceFrame ? 0 : first.sourceFrame - below;
		}
		else if (offsetTicks >= last.offsetTicks)
		{
			frame = last.sourceFrame + static_cast<f_cnt_t>((offsetTicks - last.offsetTicks) * rate);
		}
		else
		{
			const auto index = segmentForOffset(offsetTicks);
			const auto& a = m_markers[static_cast<std::size_t>(index)];
			const auto& b = m_markers[static_cast<std::size_t>(index) + 1];
			const auto ticks = static_cast<double>(b.offsetTicks - a.offsetTicks);
			const auto frames = static_cast<double>(b.sourceFrame - a.sourceFrame);
			frame = a.sourceFrame + static_cast<f_cnt_t>(frames * (offsetTicks - a.offsetTicks) / ticks);
		}
		return std::clamp(frame, sourceIn, sourceOut);
	}

	//! The inverse: the clip-relative tick at which `sourceFrame` is heard.
	tick_t timelineOffsetAt(f_cnt_t sourceFrame, f_cnt_t sourceIn, float baseFramesPerTick) const
	{
		if (m_count == 0) { return 0; }
		const auto& first = m_markers[0];
		const auto& last = m_markers[static_cast<std::size_t>(m_count - 1)];
		const auto rate = baseFramesPerTick > 0.0f ? baseFramesPerTick : 1.0f;

		if (sourceFrame <= first.sourceFrame)
		{
			return first.offsetTicks
				- static_cast<tick_t>(static_cast<double>(first.sourceFrame - sourceFrame) / rate);
		}
		if (sourceFrame >= last.sourceFrame)
		{
			return last.offsetTicks
				+ static_cast<tick_t>(static_cast<double>(sourceFrame - last.sourceFrame) / rate);
		}

		const auto index = segmentForSourceFrame(sourceFrame);
		const auto& a = m_markers[static_cast<std::size_t>(index)];
		const auto& b = m_markers[static_cast<std::size_t>(index) + 1];
		const auto frames = static_cast<double>(b.sourceFrame - a.sourceFrame);
		const auto ticks = static_cast<double>(b.offsetTicks - a.offsetTicks);
		return a.offsetTicks
			+ static_cast<tick_t>(ticks * (sourceFrame - a.sourceFrame) / frames);
	}

	/*! The local rate at `sourceFrame`, in source frames per tick.
	 *
	 *  Segments are half-open to the right — `[marker_i, marker_{i+1})` — so the
	 *  rate a *render* uses from a source frame onwards is well defined at a
	 *  marker. Outside the outermost markers the clip's own base rate applies,
	 *  which is what makes an unwarped clip play at exactly its natural rate.
	 */
	float framesPerTickAt(f_cnt_t sourceFrame, float baseFramesPerTick) const
	{
		if (m_count < 2) { return baseFramesPerTick; }
		const auto& first = m_markers[0];
		const auto& last = m_markers[static_cast<std::size_t>(m_count - 1)];
		if (sourceFrame < first.sourceFrame || sourceFrame >= last.sourceFrame)
		{
			return baseFramesPerTick;
		}

		const auto index = segmentForSourceFrame(sourceFrame);
		const auto& a = m_markers[static_cast<std::size_t>(index)];
		const auto& b = m_markers[static_cast<std::size_t>(index) + 1];
		const auto frames = static_cast<double>(b.sourceFrame - a.sourceFrame);
		const auto ticks = static_cast<double>(b.offsetTicks - a.offsetTicks);
		if (frames <= 0.0 || ticks <= 0.0) { return baseFramesPerTick; }
		return static_cast<float>(frames / ticks);
	}

private:
	//! Largest index whose marker is at or before `offsetTicks` (m_count >= 1).
	int segmentForOffset(tick_t offsetTicks) const
	{
		int index = 0;
		while (index + 1 < m_count && m_markers[static_cast<std::size_t>(index) + 1].offsetTicks <= offsetTicks)
		{
			++index;
		}
		return index;
	}

	//! Largest index whose marker is at or before `sourceFrame` (m_count >= 1).
	int segmentForSourceFrame(f_cnt_t sourceFrame) const
	{
		int index = 0;
		while (index + 1 < m_count
			&& m_markers[static_cast<std::size_t>(index) + 1].sourceFrame <= sourceFrame)
		{
			++index;
		}
		return index;
	}

	std::array<WarpMarker, MaxMarkers> m_markers{};
	int m_count = 0;
};

} // namespace lmms

#endif // LMMS_WARP_MARKERS_H
