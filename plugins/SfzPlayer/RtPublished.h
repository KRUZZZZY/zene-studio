/*
 * RtPublished.h - publish an object to the audio thread and reclaim the old one safely
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_RT_PUBLISHED_H
#define LMMS_RT_PUBLISHED_H

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace lmms
{

/*! One object the render path reads and a non-realtime thread replaces.
 *
 *  The render path takes a Guard around its use of the object: two atomic read-modify-writes and one atomic
 *  load - no lock, no allocation, no wait. A control-thread replace() swaps the pointer and then waits until
 *  no Guard is alive before it hands the old object back for deletion. A guard that began before the swap
 *  holds the old object and keeps the count above zero until it ends; a guard that begins after the swap sees
 *  the new one. Several render threads may hold Guards at once.
 *
 *  Only replace() waits, and only the thread that calls it. If a render thread stays inside a Guard past the
 *  timeout, replace() gives up and returns nothing: the old object is leaked rather than freed under a reader.
 */
template<class T>
class RtPublished
{
public:
	explicit RtPublished(std::unique_ptr<T> initial = nullptr) noexcept
		: m_ptr{initial.release()}
	{
	}

	RtPublished(const RtPublished&) = delete;
	RtPublished& operator=(const RtPublished&) = delete;

	~RtPublished() { delete m_ptr.load(std::memory_order_relaxed); }

	//! Held by the render path for as long as it uses the object.
	class Guard
	{
	public:
		explicit Guard(const RtPublished& owner) noexcept
			: m_owner{&owner}
		{
			// seq_cst on the count and the pointer load: a replace() that has swapped and then reads zero
			// readers cannot have missed this guard's use of the object it swapped out.
			owner.m_readers.fetch_add(1, std::memory_order_seq_cst);
			m_object = owner.m_ptr.load(std::memory_order_seq_cst);
		}
		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;
		~Guard() { m_owner->m_readers.fetch_sub(1, std::memory_order_seq_cst); }

		T* get() const noexcept { return m_object; }
		T* operator->() const noexcept { return m_object; }
		explicit operator bool() const noexcept { return m_object != nullptr; }

	private:
		const RtPublished* m_owner;
		T* m_object;
	};

	Guard acquire() const noexcept { return Guard{*this}; }

	/*! Control thread only. Installs \a next and returns the previous object once no render thread can still
	 *  hold it; the caller deletes it (or lets the returned pointer do so). */
	std::unique_ptr<T> replace(std::unique_ptr<T> next,
		std::chrono::milliseconds timeout = std::chrono::milliseconds{500})
	{
		T* old = m_ptr.exchange(next.release(), std::memory_order_seq_cst);
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (m_readers.load(std::memory_order_seq_cst) != 0)
		{
			if (std::chrono::steady_clock::now() >= deadline) { return nullptr; }
			std::this_thread::yield();
		}
		return std::unique_ptr<T>{old};
	}

private:
	std::atomic<T*> m_ptr;
	mutable std::atomic<int> m_readers{0};
};

} // namespace lmms

#endif // LMMS_RT_PUBLISHED_H
