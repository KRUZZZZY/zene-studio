/*
 * AutomationRenderScratch.h - fixed-capacity containers for the automation read on the audio thread
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

#ifndef LMMS_AUTOMATION_RENDER_SCRATCH_H
#define LMMS_AUTOMATION_RENDER_SCRATCH_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "Clip.h"
#include "Track.h"

namespace lmms
{

class AutomatableModel;

/*! The clips and the model values one automation read works on.
 *
 *  Song::processAutomations used to build a std::vector<Track*>, a std::vector<Clip*> and a
 *  QMap<AutomatableModel*, float> on every tick of the audio thread, and RealtimeSanitizer reports
 *  every one of those allocations (hosted run 38707621703). This holds the same data in vectors
 *  whose capacity is reserved once, on the control thread:
 *
 *   - `bounded` (the song's render scratch): nothing is ever added past the reserved capacity - the
 *     extra clip or value is counted in overflows() and dropped, so the audio thread cannot allocate;
 *   - not bounded (the control thread's automatedValuesAt, which the tests and the editors call): the
 *     vectors grow as they like.
 *
 *  Both modes run the same code in TrackContainer, so the answer the audio thread acts on and the one
 *  a caller can ask for are the same by construction. */
class AutomationRenderScratch
{
public:
	struct Entry
	{
		AutomatableModel* model;
		float value;
	};

	//! The song's render scratch reserves this many clips and this many model values per level.
	static constexpr std::size_t RenderCapacity = 16384;

	AutomationRenderScratch() = default;

	//! Reserves \a capacity clips and values and refuses to grow past it.
	explicit AutomationRenderScratch(std::size_t capacity) : m_bounded(true)
	{
		m_clips.reserve(capacity);
		m_entries.reserve(capacity);
	}

	void clear() noexcept
	{
		m_clips.clear();
		m_entries.clear();
	}

	// ---- the clips of this read, sorted by start position (Track::getClipsInRange's order) ----

	const std::vector<Clip*>& clips() const noexcept { return m_clips; }

	//! Inserts \a clip after every clip that starts at or before it. False when the scratch is full.
	bool insertClip(Clip* clip)
	{
		if (!canGrow(m_clips)) { return false; }
		m_clips.insert(std::upper_bound(m_clips.begin(), m_clips.end(), clip, Clip::comparePosition), clip);
		return true;
	}

	//! Appends \a clip without sorting (the pattern editor's one clip per track). False when full.
	bool appendClip(Clip* clip)
	{
		if (!canGrow(m_clips)) { return false; }
		m_clips.push_back(clip);
		return true;
	}

	//! The clips of \a track whose span touches [start, end], as Track::getClipsInRange picks them.
	void insertClipsInRange(const Track& track, const TimePos& start, const TimePos& end)
	{
		for (Clip* clip : track.getClips())
		{
			const int s = clip->startPosition();
			const int e = clip->endPosition();
			if (s <= end && e >= start) { insertClip(clip); }
		}
	}

	// ---- the values: model -> value, one entry per model, ordered by the model's address ----

	const std::vector<Entry>& entries() const noexcept { return m_entries; }
	std::size_t size() const noexcept { return m_entries.size(); }

	const float* find(const AutomatableModel* model) const noexcept
	{
		const auto it = lower(model);
		return it != m_entries.end() && it->model == model ? &it->value : nullptr;
	}

	bool contains(const AutomatableModel* model) const noexcept { return find(model) != nullptr; }

	//! Sets \a model's value, replacing an earlier one. False when a new model does not fit.
	bool set(AutomatableModel* model, float value)
	{
		const auto it = m_entries.begin() + (lower(model) - m_entries.begin());
		if (it != m_entries.end() && it->model == model)
		{
			it->value = value;
			return true;
		}
		if (!canGrow(m_entries)) { return false; }
		m_entries.insert(it, Entry{model, value});
		return true;
	}

	//! How many clips or values did not fit since the last takeOverflows().
	std::size_t overflows() const noexcept { return m_overflows; }
	std::size_t takeOverflows() noexcept { return std::exchange(m_overflows, 0); }

private:
	template<typename T>
	bool canGrow(const std::vector<T>& v)
	{
		if (!m_bounded || v.size() < v.capacity()) { return true; }
		++m_overflows;
		return false;
	}

	std::vector<Entry>::const_iterator lower(const AutomatableModel* model) const noexcept
	{
		return std::lower_bound(m_entries.begin(), m_entries.end(), model,
			[](const Entry& entry, const AutomatableModel* key) { return std::less<const AutomatableModel*>{}(entry.model, key); });
	}

	std::vector<Clip*> m_clips;
	std::vector<Entry> m_entries;
	std::size_t m_overflows = 0;
	bool m_bounded = false;
};

} // namespace lmms

#endif // LMMS_AUTOMATION_RENDER_SCRATCH_H
