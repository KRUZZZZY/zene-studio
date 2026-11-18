/*
 * LocklessList.h - list with lockless push and pop
 *
 * Copyright (c) 2016 Javier Serrano Polo <javier@jasp.net>
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

#ifndef LMMS_LOCKLESS_LIST_H
#define LMMS_LOCKLESS_LIST_H

#include "LocklessAllocator.h"

#include <atomic>
#include <memory>
#include <utility>

namespace lmms
{

template<typename T>
class LocklessList
{
public:
	struct Element
	{
		T value;
		Element * next;
	} ;

	LocklessList( size_t size ) :
		m_first(nullptr),
		m_allocator(new LocklessAllocatorT<Element>(size))
	{
	}

	~LocklessList()
	{
		for (Element * e = m_first.exchange(nullptr, std::memory_order_acquire); e; )
		{
			Element * next = e->next;
			std::destroy_at(e);
			m_allocator->free(e);
			e = next;
		}
		delete m_allocator;
	}

	bool push( T value )
	{
		Element * e = m_allocator->alloc();
		if( e == nullptr )
		{
			return false;
		}
		try
		{
			std::construct_at(e, std::move_if_noexcept(value),
				m_first.load(std::memory_order_relaxed));
		}
		catch (...)
		{
			m_allocator->free(e);
			throw;
		}

		while (!m_first.compare_exchange_weak(e->next, e,
				std::memory_order_release,
				std::memory_order_relaxed))
		{
			// Empty loop (compare_exchange_weak updates e->next)
		}
		return true;
	}

	Element * popList()
	{
		return m_first.exchange(nullptr);
	}

	Element * first()
	{
		return m_first.load(std::memory_order_acquire);
	}

	void setFirst( Element * e )
	{
		m_first.store(e, std::memory_order_release);
	}

	void free( Element * e )
	{
		std::destroy_at(e);
		m_allocator->free( e );
	}


private:
	std::atomic<Element*> m_first;
	LocklessAllocatorT<Element> * m_allocator;

} ;


} // namespace lmms

#endif // LMMS_LOCKLESS_LIST_H
