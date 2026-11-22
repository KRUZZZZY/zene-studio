/*
 * VstShmHandoff.h - lock-free handoff for the VST remote client's shared block
 *
 * Copyright (c) 2026 LMMS developers
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
 * License along with this program (see COPYING); if not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_VST_SHM_HANDOFF_H
#define LMMS_VST_SHM_HANDOFF_H

#include <atomic>
#include <cstdint>
#include <thread>

namespace lmms::vst
{

//! Handoff that lets the audio period read the shared block without a mutex.
/*!
 * The processing thread opens a read window with beginRead() and closes it
 * with endRead(). The only writer is the non-realtime updateInOutCount()
 * caller, which first invalidates the block and waits for the active readers
 * to drain before resizing it. All operations use seq_cst so that a reader
 * which sees a valid block cannot overlap a writer which observed no readers.
 */
class ShmHandoff
{
public:
	bool beginRead()
	{
		m_readers.fetch_add( 1, std::memory_order_seq_cst );
		if( !m_valid.load( std::memory_order_seq_cst ) )
		{
			m_readers.fetch_sub( 1, std::memory_order_seq_cst );
			return false;
		}
		return true;
	}

	void endRead()
	{
		m_readers.fetch_sub( 1, std::memory_order_seq_cst );
	}

	void invalidate()
	{
		m_valid.store( false, std::memory_order_seq_cst );
		m_writerWaiting.store( true, std::memory_order_seq_cst );
		while( m_readers.load( std::memory_order_seq_cst ) != 0 )
		{
			std::this_thread::yield();
		}
		m_writerWaiting.store( false, std::memory_order_seq_cst );
	}

	void validate()
	{
		m_valid.store( true, std::memory_order_seq_cst );
	}

	bool isValid() const
	{
		return m_valid.load( std::memory_order_seq_cst );
	}

	bool isWriterWaiting() const
	{
		return m_writerWaiting.load( std::memory_order_seq_cst );
	}

	std::uint32_t activeReaders() const
	{
		return m_readers.load( std::memory_order_seq_cst );
	}

	static constexpr bool audioPathIsLockFree()
	{
		return std::atomic<bool>::is_always_lock_free &&
			std::atomic<std::uint32_t>::is_always_lock_free;
	}

private:
	std::atomic<bool> m_valid{false};
	std::atomic<std::uint32_t> m_readers{0};
	std::atomic<bool> m_writerWaiting{false};
};

static_assert( std::atomic<bool>::is_always_lock_free,
	"the VST shared-memory handoff must be lock-free" );
static_assert( std::atomic<std::uint32_t>::is_always_lock_free,
	"the VST shared-memory handoff must be lock-free" );

} // namespace lmms::vst

#endif // LMMS_VST_SHM_HANDOFF_H
