/*
 * SessionArrangementRecorder.h - Arrangement Record: the session performance's
 *                                own event ring (SPEC-zene-studio A3 and §4.1,
 *                                "Arrangement Record"; board task #641, the
 *                                #596 half).
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
 */

/* WHY THIS FILE EXISTS. SPEC §4.1 lists "Arrangement Record: session performance
 * (launches, moves) recorded into the Arrangement as clips/automation;
 * Back-to-Arrangement switch" and `docs/FEATURE-LIST-0.3.0.md` row 1 names it as
 * the second of the two gaps 0.3.0 still owed: there was no arrangement-record
 * path at all - `grep -rniI 'BackToArrangement|arrangement record' src include`
 * returned nothing outside the design docs.
 *
 * WHAT IS RECORDED, and in whose words. The launch engine already detects, ON
 * THE AUDIO THREAD, the two transitions that are a performance: a slot STARTED
 * (Started / Retriggered) and a slot STOPPED (Stopped). Those are the events
 * this recorder carries; the model thread turns each start/stop PAIR into one
 * arrangement clip on that column's song track, at the tick the launch actually
 * fired on (not the period that noticed it - SessionScheduler publishes the
 * scheduled line for exactly that reason).
 *
 * "Launches, moves" in SPEC §4.1's own words: a LAUNCH is a pair, a MOVE is what
 * a Follow Action's SwitchScene does - it stops the cell that was playing and
 * starts the next one, so it records as its own pair and lands as its own clip.
 * Nothing else in 0.3.0 moves session content (the grid UI is #598, out of this
 * release).
 *
 * THREADING. This is the second SPSC ring in the session engine and it points
 * the other way: the AUDIO thread is the producer (recordStart / recordStop,
 * one relaxed load and one release store - no allocation, no lock, no syscall)
 * and the MODEL thread is the consumer (pop(), from the command handlers that
 * land the events). It is deliberately NOT the launch command queue with the
 * roles swapped: that queue is single-producer by construction (the model
 * thread), and a second producer on it would break the one invariant
 * commandsCrossThreadsWithoutLoss exists to prove.
 *
 * A full ring drops the event and counts it (dropped()) rather than growing -
 * the same bounded-failure rule the launch queue follows. Nothing here allocates
 * at any point, on either side.
 */

#ifndef LMMS_SESSION_ARRANGEMENT_RECORDER_H
#define LMMS_SESSION_ARRANGEMENT_RECORDER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"

namespace lmms
{

/*! The session performance's event ring. Trivially copyable, fixed storage. */
class LMMS_EXPORT SessionArrangementRecorder
{
public:
	/*! One transition of one slot. `started` distinguishes the pair's two
	 *  halves; `tick` is the tick the transition fired ON. */
	struct Event
	{
		int track = -1;
		int scene = -1;
		tick_t tick = 0;
		bool started = false;
	};

	//! 64 transitions between two command calls. A launch is two events, so a
	//! full 32-cell performance fits; beyond that the ring drops and counts.
	static constexpr std::size_t Capacity = 64;

	//! Arm / disarm. Model thread. Disarming does NOT clear what is already in
	//! the ring: the caller lands it (see the drain in
	//! ControlCommandsSessionRecord.cpp), because dropping a performance on
	//! disarm is exactly the data loss the feature exists to prevent.
	void setArmed( bool armed ) noexcept { m_armed.store( armed, std::memory_order_relaxed ); }
	bool armed() const noexcept { return m_armed.load( std::memory_order_relaxed ); }

	//! Audio thread. False when disarmed (nothing recorded) or when the ring is
	//! full (dropped() has moved). Never allocates.
	bool recordStart( int track, int scene, tick_t tick ) noexcept
	{
		return push( Event{ track, scene, tick, true } );
	}
	bool recordStop( int track, int scene, tick_t tick ) noexcept
	{
		return push( Event{ track, scene, tick, false } );
	}

	bool push( const Event& event ) noexcept
	{
		if( !armed() )
		{
			return false;
		}
		const std::size_t write = m_write.load( std::memory_order_relaxed );
		const std::size_t next = ( write + 1 ) % storageSize;
		if( next == m_read.load( std::memory_order_acquire ) )
		{
			m_dropped.fetch_add( 1, std::memory_order_relaxed );
			return false;
		}
		m_data[write] = event;
		m_write.store( next, std::memory_order_release );
		m_recorded.fetch_add( 1, std::memory_order_relaxed );
		return true;
	}

	//! Model thread: one event out, oldest first.
	bool pop( Event& event ) noexcept
	{
		const std::size_t read = m_read.load( std::memory_order_relaxed );
		if( read == m_write.load( std::memory_order_acquire ) )
		{
			return false;
		}
		event = m_data[read];
		m_read.store( ( read + 1 ) % storageSize, std::memory_order_release );
		return true;
	}

	//! Events waiting to be landed (a bounded estimate: the two indices are
	//! read separately, so a concurrent push may not be counted).
	std::size_t pending() const noexcept
	{
		const std::size_t read = m_read.load( std::memory_order_relaxed );
		const std::size_t write = m_write.load( std::memory_order_acquire );
		return write >= read ? write - read : storageSize - read + write;
	}

	std::uint64_t recorded() const noexcept { return m_recorded.load( std::memory_order_relaxed ); }
	std::uint64_t dropped() const noexcept { return m_dropped.load( std::memory_order_relaxed ); }

private:
	static constexpr std::size_t storageSize = Capacity + 1;
	std::array<Event, storageSize> m_data{};
	std::atomic<std::size_t> m_read{ 0 };
	std::atomic<std::size_t> m_write{ 0 };
	std::atomic<std::uint64_t> m_recorded{ 0 };
	std::atomic<std::uint64_t> m_dropped{ 0 };
	std::atomic<bool> m_armed{ false };
};

} // namespace lmms

#endif // LMMS_SESSION_ARRANGEMENT_RECORDER_H
