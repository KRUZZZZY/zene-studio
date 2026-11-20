/*
 * RealtimeFreeList.h - allocation-free free list for audio-thread object pools
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_REALTIME_FREE_LIST_H
#define LMMS_REALTIME_FREE_LIST_H

#include <atomic>
#include <cstddef>

namespace lmms
{

//! Base of a node that can sit on a RealtimeFreeList. The list links nodes through this field only.
template <typename Node>
struct RealtimeFreeNode
{
	Node* nextFree = nullptr;
};

/*! An intrusive Treiber stack of nodes that are allocated elsewhere (on the control thread) and only linked here.
 *
 *  Thread contract:
 *  - push() is lock-free and allocation-free and may run on any thread: it links the node in front of the head with
 *    a compare-exchange.
 *  - pop() is lock-free and allocation-free and may be called from any thread, but only one pop runs at a time. A
 *    Treiber pop reads head->nextFree before its compare-exchange; with two poppers a node could be taken, reused and
 *    pushed back in between (the ABA hazard), so pop() first takes an atomic claim. A caller that finds the claim
 *    held gets nullptr, exactly as for an empty list: it treats the pool as exhausted and never waits.
 *  Nodes are never freed while the list is in use; the owner keeps them alive. Growth is the owner's job and happens
 *  on the control thread (push new nodes).
 */
template <typename Node>
class RealtimeFreeList
{
public:
	//! Any thread. Links @a node at the head.
	void push(Node* node) noexcept
	{
		Node* head = m_head.load(std::memory_order_relaxed);
		do
		{
			node->nextFree = head;
		} while (!m_head.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_relaxed));
		m_count.fetch_add(1, std::memory_order_relaxed);
	}

	//! Any thread. Returns the head node, or nullptr when the list is empty or another pop holds the claim.
	Node* pop() noexcept
	{
		// While the claim is held no other pop can take the head, so the node read below cannot leave and re-enter
		// the stack under this pop's compare-exchange (ABA).
		if (m_popClaim.test_and_set(std::memory_order_acquire)) { return nullptr; }
		Node* head = m_head.load(std::memory_order_acquire);
		while (head != nullptr
			&& !m_head.compare_exchange_weak(head, head->nextFree, std::memory_order_acquire,
				std::memory_order_acquire))
		{
		}
		if (head != nullptr) { m_count.fetch_sub(1, std::memory_order_relaxed); }
		m_popClaim.clear(std::memory_order_release);
		return head;
	}

	//! Approximate while a pop runs concurrently; exact when the caller is the only thread touching the list.
	std::size_t count() const noexcept { return m_count.load(std::memory_order_relaxed); }

private:
	std::atomic<Node*> m_head{nullptr};
	std::atomic<std::size_t> m_count{0};
	std::atomic_flag m_popClaim{};
};

} // namespace lmms

#endif // LMMS_REALTIME_FREE_LIST_H
