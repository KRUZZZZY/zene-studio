/*
 * MpeStreamSnapshot.cpp - publication and deferred reclamation of a note's
 *                         MPE expression stream (Q050-016 round 3)
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

#include "MpeStreamSnapshot.h"

#include "RealtimeScope.h"

namespace lmms
{

// The realtime half must not fall back to a lock: std::atomic of a type the
// target cannot handle natively is implemented with the library's lock table.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
	"MpeStreamSnapshot's reference count must be lock-free");
static_assert(std::atomic<const MpeStreamSnapshot*>::is_always_lock_free,
	"MpeStreamSnapshot's slot and retire list must be lock-free");
static_assert(std::atomic<std::size_t>::is_always_lock_free,
	"MpeStreamSnapshot's in-flight counter must be lock-free");

namespace
{

//! acquire() calls between raising and lowering this (see the header, rule 1).
std::atomic<std::size_t> s_copiesInFlight{0};
//! The retire list's head (an intrusive Treiber stack; drained whole).
std::atomic<const MpeStreamSnapshot*> s_retired{nullptr};
std::atomic<std::size_t> s_retiredCount{0};
std::atomic<std::size_t> s_liveCount{0};

} // namespace


MpeStreamSnapshot::MpeStreamSnapshot(const MpeExpressionStream& stream)
	: m_stream(stream)
{
	s_liveCount.fetch_add(1, std::memory_order_relaxed);
}


MpeStreamSnapshot::~MpeStreamSnapshot()
{
	s_liveCount.fetch_sub(1, std::memory_order_relaxed);
}


const MpeStreamSnapshot* MpeStreamSnapshot::create(const MpeExpressionStream& stream)
{
	return new MpeStreamSnapshot(stream);
}


bool MpeStreamSnapshot::tryAddReference() const noexcept
{
	std::uint32_t refs = m_refs.load(std::memory_order_acquire);
	while (refs != 0)
	{
		if (m_refs.compare_exchange_weak(refs, refs + 1, std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return true;
		}
	}
	return false;
}


const MpeStreamSnapshot* MpeStreamSnapshot::acquire(const std::atomic<const MpeStreamSnapshot*>& slot) noexcept
{
	// An empty slot is the common case (every note without a stream): a copy
	// that reads it empty is simply ordered before any concurrent publication.
	if (slot.load(std::memory_order_acquire) == nullptr) { return nullptr; }

	s_copiesInFlight.fetch_add(1, std::memory_order_seq_cst);
	const MpeStreamSnapshot* snapshot = slot.load(std::memory_order_seq_cst);
	// A zero count means the slot swapped this snapshot out after our load;
	// the slot then holds a newer pointer (or none), so read it again. Each
	// retry follows a completed swap, so the loop ends.
	while (snapshot != nullptr && !snapshot->tryAddReference())
	{
		snapshot = slot.load(std::memory_order_seq_cst);
	}
	s_copiesInFlight.fetch_sub(1, std::memory_order_release);
	return snapshot;
}


void MpeStreamSnapshot::release(const MpeStreamSnapshot* snapshot, bool deferred) noexcept
{
	if (snapshot == nullptr) { return; }
	if (snapshot->m_refs.fetch_sub(1, std::memory_order_acq_rel) != 1) { return; }
	// The last reference. Free here only off the realtime path and with no
	// copy in flight (header, rules 2-3); anything else waits for reclaimRetired().
	if (deferred || RealtimeScope::isActive() || s_copiesInFlight.load(std::memory_order_seq_cst) != 0)
	{
		retire(snapshot);
		return;
	}
	destroy(snapshot);
}


void MpeStreamSnapshot::retire(const MpeStreamSnapshot* snapshot) noexcept
{
	// Push: a CAS loop on the head, no allocation (the link is the snapshot's
	// own). Lock-free; the only consumer takes the whole list with exchange(),
	// so there is no pop and no ABA.
	s_retiredCount.fetch_add(1, std::memory_order_relaxed);
	const MpeStreamSnapshot* head = s_retired.load(std::memory_order_relaxed);
	do
	{
		snapshot->m_nextRetired = head;
	}
	while (!s_retired.compare_exchange_weak(head, snapshot, std::memory_order_release, std::memory_order_relaxed));
}


void MpeStreamSnapshot::destroy(const MpeStreamSnapshot* snapshot) noexcept
{
	delete snapshot;
}


std::size_t MpeStreamSnapshot::reclaimRetired() noexcept
{
	if (RealtimeScope::isActive()) { return 0; }
	const MpeStreamSnapshot* list = s_retired.exchange(nullptr, std::memory_order_acquire);
	if (list == nullptr) { return 0; }
	if (s_copiesInFlight.load(std::memory_order_seq_cst) != 0)
	{
		// A copy may still hold an uncounted pointer to one of these: splice
		// the whole list back for the next call instead of waiting.
		const MpeStreamSnapshot* tail = list;
		while (tail->m_nextRetired != nullptr) { tail = tail->m_nextRetired; }
		const MpeStreamSnapshot* head = s_retired.load(std::memory_order_relaxed);
		do
		{
			tail->m_nextRetired = head;
		}
		while (!s_retired.compare_exchange_weak(head, list, std::memory_order_release, std::memory_order_relaxed));
		return 0;
	}
	std::size_t freed = 0;
	while (list != nullptr)
	{
		const MpeStreamSnapshot* next = list->m_nextRetired;
		destroy(list);
		list = next;
		++freed;
	}
	s_retiredCount.fetch_sub(freed, std::memory_order_relaxed);
	return freed;
}


std::size_t MpeStreamSnapshot::retiredCount() noexcept
{
	return s_retiredCount.load(std::memory_order_relaxed);
}


std::size_t MpeStreamSnapshot::liveCount() noexcept
{
	return s_liveCount.load(std::memory_order_relaxed);
}

} // namespace lmms
