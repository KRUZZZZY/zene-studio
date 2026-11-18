/*
 * WarpPublication.h - hands a clip's warp map to the render thread without either side waiting
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

#ifndef LMMS_WARP_PUBLICATION_H
#define LMMS_WARP_PUBLICATION_H

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include "WarpMarkers.h"

namespace lmms
{

class WarpPublication;

/*! One generation of the map, held by a render pass (Q050-063). Taking and dropping it is one atomic
 *  operation each, with no lock, allocation or free, so it is safe on the audio thread. */
class WarpSnapshot
{
public:
	WarpSnapshot() = default;
	inline explicit WarpSnapshot(const WarpPublication& publication) noexcept;
	WarpSnapshot(WarpSnapshot&& other) noexcept :
		m_publication(std::exchange(other.m_publication, nullptr)),
		m_map(std::exchange(other.m_map, nullptr))
	{
	}
	WarpSnapshot& operator=(WarpSnapshot&& other) noexcept
	{
		if (this != &other)
		{
			release();
			m_publication = std::exchange(other.m_publication, nullptr);
			m_map = std::exchange(other.m_map, nullptr);
		}
		return *this;
	}
	WarpSnapshot(const WarpSnapshot&) = delete;
	WarpSnapshot& operator=(const WarpSnapshot&) = delete;
	~WarpSnapshot() { release(); }

	//! The generation, or nullptr for a clip with no markers.
	const WarpMarkers* map() const noexcept { return m_map; }

private:
	inline void release() noexcept;

	const WarpPublication* m_publication = nullptr;
	const WarpMarkers* m_map = nullptr;
};

/*! The published generations of one clip's map. The control thread publishes a validated map as a new
 *  immutable generation and swaps one pointer; a render pass counts itself in and reads that pointer.
 *  A generation replaced while any pass is counted in is kept, and only the control thread frees it: at a
 *  later publish, or with the clip. Neither side ever waits for the other.
 *
 *  Why freeing is safe: the pass counts itself in BEFORE reading the pointer and publish() swaps the pointer
 *  BEFORE reading the count, all sequentially consistent. If publish() reads a count of zero, any pass
 *  that counts itself in afterwards reads the new pointer, so nobody holds a replaced generation. */
class WarpPublication
{
public:
	WarpPublication() = default;
	WarpPublication(const WarpPublication&) = delete;
	WarpPublication& operator=(const WarpPublication&) = delete;
	~WarpPublication() { delete m_live.load(std::memory_order_acquire); }

	//! Control thread only: allocates and frees. An empty map publishes no generation at all.
	void publish(const WarpMarkers& map)
	{
		const WarpMarkers* next = map.empty() ? nullptr : new WarpMarkers(map);
		const WarpMarkers* replaced = m_live.exchange(next, std::memory_order_seq_cst);
		if (replaced != nullptr) { m_retired.emplace_back(replaced); }
		if (m_readers.load(std::memory_order_seq_cst) == 0) { m_retired.clear(); }
	}

	//! Control thread: the replaced generations not freed yet, because a pass was counted in at the last publish.
	std::size_t retiredCount() const { return m_retired.size(); }

private:
	friend class WarpSnapshot;

	mutable std::atomic<int> m_readers{0};
	std::atomic<const WarpMarkers*> m_live{nullptr};
	std::vector<std::unique_ptr<const WarpMarkers>> m_retired;
};

inline WarpSnapshot::WarpSnapshot(const WarpPublication& publication) noexcept :
	m_publication(&publication)
{
	publication.m_readers.fetch_add(1, std::memory_order_seq_cst);
	m_map = publication.m_live.load(std::memory_order_seq_cst);
}

inline void WarpSnapshot::release() noexcept
{
	if (m_publication != nullptr) { m_publication->m_readers.fetch_sub(1, std::memory_order_seq_cst); }
	m_publication = nullptr;
	m_map = nullptr;
}

} // namespace lmms

#endif // LMMS_WARP_PUBLICATION_H
