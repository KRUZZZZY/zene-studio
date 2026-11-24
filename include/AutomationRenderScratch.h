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
#include <cstdint>
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
 *     extra clip or value is counted in overflows() and dropped, so the audio thread cannot allocate.
 *     The caller MUST treat a non-zero overflows() as "this read is incomplete" and redo it on an
 *     unbounded scratch (Song::processAutomations does); Song::ensureAutomationScratchCapacity keeps the
 *     capacity above the project's clip and automated-model counts so that redo does not happen;
 *   - not bounded (the control thread's automatedValuesAt, which the tests and the editors call): the
 *     vectors grow as they like.
 *
 *  Both modes run the same code in TrackContainer, so the answer the audio thread acts on and the one
 *  a caller can ask for are the same by construction.
 *
 *  The read's WORK is bounded too (new-p16-295). A reserved vector still moves every entry behind an
 *  insertion point, so inserting each of N clips or values into its ordered place cost O(N) per item -
 *  O(N^2) per read when a track hands its clips over in the order opposite their start positions, or
 *  when the targets arrive in descending address order. At the 16384-entry render capacity that is
 *  hundreds of millions of relocations in one period. So a read now COLLECTS: insertClip() and set()
 *  append, and the order is built once, when a caller first asks for it - clips() and entries() stable
 *  sort through a reserved buffer (stably, so clips at the same tick keep the order they arrived in,
 *  exactly as the ordered insert placed them) and set() finds-or-replaces a model through a reserved
 *  open-address index instead of searching the list. An input that already arrives in order moves
 *  nothing at all, which is what the common single-track read did before. clipMoves()/valueMoves()
 *  report the relocations actually done, so the bound is asserted, not assumed
 *  (tests/src/core/AutomationScratchWorkTest.cpp). */

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
		reserve(capacity);
	}

	//! Grows the reserved capacity (control thread, with the render locked out). Never shrinks.
	void reserve(std::size_t capacity)
	{
		if (capacity > m_clips.capacity()) { m_clips.reserve(capacity); }
		if (capacity > m_entries.capacity()) { m_entries.reserve(capacity); }
		if (capacity > m_clipSortBuffer.capacity()) { m_clipSortBuffer.reserve(capacity); }
		if (capacity > m_entrySortBuffer.capacity()) { m_entrySortBuffer.reserve(capacity); }
		const std::size_t tableSlots = slotCountFor(capacity);
		if (tableSlots > m_index.size()) { resizeIndex(tableSlots); }
	}

	std::size_t capacity() const noexcept { return std::min(m_clips.capacity(), m_entries.capacity()); }

	void clear() noexcept
	{
		m_clips.clear();
		m_entries.clear();
		m_clipsDisordered = false;
		m_clipsCallerOrder = false;
		m_entriesDisordered = false;
		m_clipMoves = 0;
		m_valueMoves = 0;
		clearIndex();
	}

	/*! How many entries of the clip list this scratch has relocated since the last clear().
	 *
	 *  This is the read's real work, not a guess at it: an ordered build counts every entry it shifts,
	 *  and a sort counts every element it moves. The audio thread has a period to fill, so the read must
	 *  stay within O(N log N) relocations for N clips; a quadratic build - an insert near the front of a
	 *  long list - shows up here as a count in the hundreds of thousands for a thousand clips. Tests
	 *  assert the bound (AutomationScratchWorkTest); nothing in the product reads it. */
	std::size_t clipMoves() const noexcept { return m_clipMoves; }

	//! The same count for the value list (model -> value entries).
	std::size_t valueMoves() const noexcept { return m_valueMoves; }

	// ---- the clips of this read, sorted by start position (Track::getClipsInRange's order) ----

	//! The clips in start order. Sorts once, through the reserved buffer, unless they arrived in order.
	const std::vector<Clip*>& clips()
	{
		if (m_clipsDisordered && !m_clipsCallerOrder) { sortClips(); }
		return m_clips;
	}

	/*! Inserts \a clip after every clip that starts at or before it.
	 *
	 *  The clip is collected here and placed by the sort behind clips(); the answer is the same ordered
	 *  insert gave, at O(1) here and O(N log N) once for the whole read instead of O(N) per clip.
	 *  False when the scratch is full. */
	bool insertClip(Clip* clip)
	{
		if (!canGrow(m_clips)) { return false; }
		if (!m_clips.empty() && Clip::comparePosition(clip, m_clips.back())) { m_clipsDisordered = true; }
		m_clips.push_back(clip);
		return true;
	}

	//! Appends \a clip without sorting (the pattern editor's one clip per track). False when full.
	bool appendClip(Clip* clip)
	{
		if (!canGrow(m_clips)) { return false; }
		m_clips.push_back(clip);
		// A caller-order clip is never moved: this list is read in the order the caller gave it.
		m_clipsCallerOrder = true;
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

	//! The values in model-address order. Sorts once, through the reserved buffer, unless they arrived in order.
	const std::vector<Entry>& entries()
	{
		if (m_entriesDisordered)
		{
			sortCounted(m_entries, m_entrySortBuffer, EntryBefore{}, m_valueMoves);
			m_entriesDisordered = false;
			reindex();
		}
		return m_entries;
	}

	std::size_t size() const noexcept { return m_entries.size(); }

	const float* find(const AutomatableModel* model) const noexcept
	{
		const IndexSlot* slot = entrySlot(model);
		return slot != nullptr ? &m_entries[slot->entry].value : nullptr;
	}

	bool contains(const AutomatableModel* model) const noexcept { return entrySlot(model) != nullptr; }

	//! Sets \a model's value, replacing an earlier one. False when a new model does not fit.
	bool set(AutomatableModel* model, float value)
	{
		if (IndexSlot* known = entrySlotForWrite(model))
		{
			m_entries[known->entry].value = value;
			return true;
		}
		if (!canGrow(m_entries)) { return false; }
		// The index is sized with the capacity (twice the entry count at worst), so a bounded scratch never
		// reaches the resizing branch below: that one allocates, and belongs to the control thread.
		if ((m_entries.size() + 1) * 2 > m_index.size())
		{
			resizeIndex(std::max<std::size_t>(2, slotCountFor(m_entries.size() + 1)));
		}
		if (!m_entries.empty() && std::less<const AutomatableModel*>{}(model, m_entries.back().model))
		{
			m_entriesDisordered = true;
		}
		m_entries.push_back(Entry{model, value});
		placeInIndex(m_entries.size() - 1);
		return true;
	}

	//! How many clips or values did not fit since the last takeOverflows().
	std::size_t overflows() const noexcept { return m_overflows; }
	std::size_t takeOverflows() noexcept { return std::exchange(m_overflows, 0); }

private:
	//! One slot of the model -> entry index. A slot is live only while its stamp is the current epoch, so
	//! clear() frees the whole table in O(1) and a read pays nothing to start over.
	struct IndexSlot
	{
		std::uint32_t entry;
		std::uint32_t stamp;
	};

	//! Orders the value list by the model's address, as the read has always handed it out.
	struct EntryBefore
	{
		bool operator()(const Entry& a, const Entry& b) const noexcept
		{
			return std::less<const AutomatableModel*>{}(a.model, b.model);
		}
	};

	template<typename T>
	bool canGrow(const std::vector<T>& v)
	{
		if (!m_bounded || v.size() < v.capacity()) { return true; }
		++m_overflows;
		return false;
	}

	void sortClips()
	{
		sortCounted(m_clips, m_clipSortBuffer, Clip::comparePosition, m_clipMoves);
		m_clipsDisordered = false;
	}

	/*! Merges the two runs [start, middle) and [middle, end) of \a source into \a destination.
	 *
	 *  Stable: on a tie the left run's element goes first, so equal start positions (or a stable order
	 *  over any comparator) keep the order the entries arrived in. Every element it writes is counted in
	 *  \a moves - the currency the read's work bound is measured in. */
	template<typename T, typename Compare>
	static void mergeRun(std::vector<T>& source, std::vector<T>& destination, std::size_t start,
		std::size_t middle, std::size_t end, Compare before, std::size_t& moves)
	{
		std::size_t left = start;
		std::size_t right = middle;
		std::size_t out = start;
		while (left < middle && right < end)
		{
			destination[out++] = before(source[right], source[left]) ? source[right++] : source[left++];
			++moves;
		}
		while (left < middle)
		{
			destination[out++] = source[left++];
			++moves;
		}
		while (right < end)
		{
			destination[out++] = source[right++];
			++moves;
		}
	}

	/*! A stable bottom-up merge sort through \a buffer: O(N log N) moves and no allocation once the two
	 *  vectors are reserved, which is why it can run on the audio thread. The caller sorts only when the
	 *  entries did not arrive in order, so an ordered read moves nothing at all. */
	template<typename T, typename Compare>
	static void sortCounted(std::vector<T>& items, std::vector<T>& buffer, Compare before, std::size_t& moves)
	{
		const std::size_t count = items.size();
		if (count < 2) { return; }
		buffer.resize(count);  // inside the reserved capacity: no allocation
		std::vector<T>* source = &items;
		std::vector<T>* destination = &buffer;
		for (std::size_t width = 1; width < count; width *= 2)
		{
			for (std::size_t start = 0; start < count; start += 2 * width)
			{
				mergeRun(*source, *destination, start, std::min(start + width, count),
					std::min(start + 2 * width, count), before, moves);
			}
			std::swap(source, destination);
		}
		if (source == &items) { return; }  // an even number of passes left the sorted run in place
		for (std::size_t i = 0; i < count; ++i)
		{
			items[i] = (*source)[i];
			++moves;
		}
	}

	//! A power of two at least twice \a capacity, so a full bounded scratch is still half an empty table.
	static std::size_t slotCountFor(std::size_t capacity) noexcept
	{
		std::size_t tableSlots = 2;
		while (tableSlots < capacity * 2 && tableSlots < (std::size_t{1} << (sizeof(std::size_t) * 8 - 2))) { tableSlots *= 2; }
		return tableSlots;
	}

	std::size_t indexHome(const AutomatableModel* model) const noexcept
	{
		const std::uintptr_t bits = reinterpret_cast<std::uintptr_t>(model);
		return ((bits >> 4) * 0x9E3779B1u) & (m_index.size() - 1);
	}

	//! The live slot holding \a model, or nullptr. A full table is impossible: it is at most half full.
	const IndexSlot* entrySlot(const AutomatableModel* model) const noexcept
	{
		if (m_index.empty()) { return nullptr; }
		const std::size_t mask = m_index.size() - 1;
		for (std::size_t at = indexHome(model);; at = (at + 1) & mask)
		{
			const IndexSlot& slot = m_index[at];
			if (slot.stamp != m_stamp) { return nullptr; }
			if (m_entries[slot.entry].model == model) { return &slot; }
		}
	}

	IndexSlot* entrySlotForWrite(const AutomatableModel* model) noexcept
	{
		return const_cast<IndexSlot*>(entrySlot(model));
	}

	//! Indexes \a position, whose model is not in the table yet.
	void placeInIndex(std::size_t position) noexcept
	{
		const std::size_t mask = m_index.size() - 1;
		for (std::size_t at = indexHome(m_entries[position].model);; at = (at + 1) & mask)
		{
			if (m_index[at].stamp != m_stamp)
			{
				m_index[at] = IndexSlot{static_cast<std::uint32_t>(position), m_stamp};
				return;
			}
		}
	}

	//! O(1): bumping the epoch makes every slot free without touching the table.
	void clearIndex() noexcept
	{
		if (m_index.empty()) { return; }
		if (++m_stamp != 0) { return; }
		for (IndexSlot& slot : m_index) { slot.stamp = 0; }
		m_stamp = 1;
	}

	//! Points the index at the entries again after the sort moved them (no allocation: the table is kept).
	void reindex() noexcept
	{
		clearIndex();
		for (std::size_t i = 0; i < m_entries.size(); ++i) { placeInIndex(i); }
	}

	//! Grows the table and re-points it at what the scratch already holds. Allocates: control thread, or
	//! an unbounded scratch.
	void resizeIndex(std::size_t tableSlots)
	{
		m_index.assign(tableSlots, IndexSlot{0, 0});
		m_stamp = 1;
		for (std::size_t i = 0; i < m_entries.size(); ++i) { placeInIndex(i); }
	}

	std::vector<Clip*> m_clips;
	std::vector<Entry> m_entries;
	//! The sort's working space, reserved with the capacity: the read never allocates it.
	std::vector<Clip*> m_clipSortBuffer;
	std::vector<Entry> m_entrySortBuffer;
	//! The model -> entry index, reserved with the capacity as well.
	std::vector<IndexSlot> m_index;
	std::uint32_t m_stamp = 1;
	std::size_t m_overflows = 0;
	//! Relocations of the two lists since the last clear(), for clipMoves() and valueMoves().
	std::size_t m_clipMoves = 0;
	std::size_t m_valueMoves = 0;
	bool m_clipsDisordered = false;
	bool m_clipsCallerOrder = false;
	bool m_entriesDisordered = false;
	bool m_bounded = false;
};

} // namespace lmms

#endif // LMMS_AUTOMATION_RENDER_SCRATCH_H
