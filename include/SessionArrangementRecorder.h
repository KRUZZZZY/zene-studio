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
#include <thread>

#include "LmmsTypes.h"
// LMMS_EXPORT on the class below. Included BY NAME rather than relied on
// transitively: this header is included FIRST by
// tests/src/core/SessionArrangementRecordTest.cpp, and a TU that reaches it
// before anything that defines the macro fails with "variable
// 'lmms::LMMS_EXPORT lmms::SessionArrangementRecorder' has initializer but
// incomplete type" - measured, not feared (the first build of this branch).
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;

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
		enum class ContentKind : std::uint8_t { Unspecified, Empty, Midi, Audio };
		int patternId = -1;
		const SampleBuffer* audioClip = nullptr;
		ContentKind contentKind = ContentKind::Unspecified;
	};

	//! Transitions between two command calls. Sized for ONE WHOLE worst-case
	//! performance: the scheduler runs SessionScheduler::MaxActiveSlots slots at
	//! once, and a launch is two events (start + stop), so the ring holds two
	//! events per slot. Below that the starts alone can fill the ring, every
	//! later stop is DROPPED, and a land pass that still sees those starts
	//! refuses without consuming - a performance that can never be landed,
	//! because neither disarm nor reset clears the ring. Beyond a worst-case
	//! performance the ring drops and counts, as before.
	static constexpr std::size_t Capacity = 2 * 64; // 2 * SessionScheduler::MaxActiveSlots

	//! Arm / disarm. Model thread. Disarming does NOT clear what is already in
	//! the ring: the caller lands it (see the drain in
	//! ControlCommandsSessionRecord.cpp), because dropping a performance on
	//! disarm is exactly the data loss the feature exists to prevent.
	//!
	//! THE HAND-OFF IS ORDERED, and the audio thread never waits. A disarm may
	//! race a push that has already passed the armed gate; snapshot() therefore
	//! waits out that in-flight push before it reads the ring (see
	//! waitForProducer()). The wait is on the MODEL thread only - the producer
	//! does one atomic increment and one decrement around its write, whatever
	//! the consumer is doing. seq_cst on the flag and the counter makes the two
	//! outcomes exactly: a push that read `true` is in the snapshot, and a push
	//! that starts after the disarm reads `false` and records nothing. There is
	//! no interleaving that leaves a late start behind the consumer's back.
	void setArmed( bool armed ) noexcept { m_armed.store( armed, std::memory_order_seq_cst ); }
	bool armed() const noexcept { return m_armed.load( std::memory_order_seq_cst ); }

#ifdef LMMS_TESTING
	/*! Test seams (clusters 2495 / 4926). The push hook runs after the producer
	 *  has passed the armed gate and before it publishes, so a test can park a
	 *  REAL in-flight write; the wait hook runs when the consumer first sees
	 *  that write, so the test can release it without a sleep or a timer.
	 *  Production builds have neither hook and no extra branch on the write. */
	void setPushBoundaryHookForTest( void ( *hook )() ) noexcept
	{
		m_testPushHook.store( hook, std::memory_order_relaxed );
	}
	void setConsumerWaitHookForTest( void ( *hook )() ) noexcept
	{
		m_testWaitHook.store( hook, std::memory_order_relaxed );
	}
#endif

	//! Audio thread. False when disarmed (nothing recorded) or when the ring is
	//! full (dropped() has moved). Never allocates.
	bool recordStart( int track, int scene, tick_t tick ) noexcept
	{
		return push( Event{ track, scene, tick, true } );
	}
	bool recordStart( int track, int scene, tick_t tick, int patternId,
		const SampleBuffer* audioClip ) noexcept
	{
		const Event::ContentKind kind = patternId >= 0 ? Event::ContentKind::Midi
			: audioClip != nullptr ? Event::ContentKind::Audio : Event::ContentKind::Empty;
		return push( Event{ track, scene, tick, true, patternId, audioClip, kind } );
	}
	bool recordStop( int track, int scene, tick_t tick ) noexcept
	{
		return push( Event{ track, scene, tick, false } );
	}

	bool push( const Event& event ) noexcept
	{
		// The counter goes up BEFORE the armed read: that is what lets the
		// model thread tell "this push has already passed the gate" from "this
		// push starts after the disarm and will see false". The producer never
		// waits on the consumer.
		m_producerInFlight.fetch_add( 1, std::memory_order_seq_cst );
		if( !shouldRecord( event ) )
		{
			m_producerInFlight.fetch_sub( 1, std::memory_order_seq_cst );
			return false;
		}
#ifdef LMMS_TESTING
		if( void ( *hook )() = m_testPushHook.load( std::memory_order_relaxed ) )
		{
			hook();
		}
#endif
		const std::size_t write = m_write.load( std::memory_order_relaxed );
		const std::size_t next = ( write + 1 ) % storageSize;
		if( next == m_read.load( std::memory_order_acquire ) )
		{
			m_dropped.fetch_add( 1, std::memory_order_relaxed );
			m_producerInFlight.fetch_sub( 1, std::memory_order_seq_cst );
			return false;
		}
		m_data[write] = event;
		m_write.store( next, std::memory_order_release );
		m_recorded.fetch_add( 1, std::memory_order_relaxed );
		m_producerInFlight.fetch_sub( 1, std::memory_order_seq_cst );
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

	/*! Model thread: copies up to \a max waiting events into \a out WITHOUT
	 *  consuming them, oldest first, and returns how many it copied. Safe for
	 *  the consumer to call: the producer only ever writes at the write index,
	 *  so everything between read and write is stable.
	 *
	 *  WHY IT EXISTS. session.arrangement_record_land has to know whether every
	 *  recorded start has its stop BEFORE it consumes anything. A pass that
	 *  discovered an open pair halfway through would have to drop the start it
	 *  could not pair - the data loss this ring exists to prevent - or land a
	 *  clip whose end it invented. The snapshot makes the decision checkable
	 *  first; only a pass that can land everything consumes anything. */
	std::size_t snapshot( Event* out, std::size_t max ) const noexcept
	{
		if( out == nullptr || max == 0 )
		{
			return 0;
		}
		waitForProducer();
		const std::size_t read = m_read.load( std::memory_order_relaxed );
		const std::size_t write = m_write.load( std::memory_order_acquire );
		std::size_t count = write >= read ? write - read : storageSize - read + write;
		if( count > max ) { count = max; }
		for( std::size_t i = 0; i < count; ++i )
		{
			out[i] = m_data[( read + i ) % storageSize];
		}
		return count;
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

	/*! Whether this event belongs in the ring. A START is taped only while the
	 *  tap is armed: a disarmed tap records nothing NEW. A STOP is taped while
	 *  the tap is armed, and also while it is DISARMED when it closes a span the
	 *  ring already carries. That last rule is what keeps a disarm from
	 *  stranding a recorded start: an open start is what a land pass refuses,
	 *  and it refuses without consuming, so dropping the closing stop would
	 *  leave the performance unlandable for good. */
	bool shouldRecord( const Event& event ) const noexcept
	{
		if( armed() )
		{
			return true;
		}
		return !event.started && closesRecordedStart( event.track, event.scene );
	}

	/*! True when the ring already carries a start for this cell with no stop
	 *  yet. Audio thread, but only for a STOP while disarmed: one bounded pass
	 *  over the ring's own events, no allocation and no second state to keep in
	 *  step. For a single cell a start opens and a stop closes the oldest open
	 *  one, exactly as landPairEvents() pairs them; an unmatched stop closes
	 *  nothing and does not go negative. */
	bool closesRecordedStart( int track, int scene ) const noexcept
	{
		const std::size_t read = m_read.load( std::memory_order_acquire );
		const std::size_t write = m_write.load( std::memory_order_acquire );
		const std::size_t count = write >= read ? write - read : storageSize - read + write;
		int open = 0;
		for( std::size_t i = 0; i < count; ++i )
		{
			const Event& event = m_data[( read + i ) % storageSize];
			if( event.track != track || event.scene != scene ) { continue; }
			if( event.started ) { ++open; }
			else if( open > 0 ) { --open; }
		}
		return open > 0;
	}

	/*! Model thread: wait out a producer that is between its armed read and its
	 *  publish. This is the disarm handshake - and it is the CONSUMER that
	 *  waits, never the audio thread. The counter is seq_cst so the two cases
	 *  above are exhaustive; the yield is a scheduling courtesy, not a timer.
	 *  snapshot() is reached from command handlers only, never from process(). */
	void waitForProducer() const noexcept
	{
#ifdef LMMS_TESTING
		if( m_producerInFlight.load( std::memory_order_seq_cst ) != 0 )
		{
			if( void ( *hook )() = m_testWaitHook.load( std::memory_order_relaxed ) )
			{
				hook();
			}
		}
#endif
		while( m_producerInFlight.load( std::memory_order_seq_cst ) != 0 )
		{
			std::this_thread::yield();
		}
	}

	std::array<Event, storageSize> m_data{};
	std::atomic<std::size_t> m_read{ 0 };
	std::atomic<std::size_t> m_write{ 0 };
	std::atomic<std::uint64_t> m_recorded{ 0 };
	std::atomic<std::uint64_t> m_dropped{ 0 };
	std::atomic<bool> m_armed{ false };
	//! Producers between their armed read and their publish. Audio thread
	//! increments/decrements; the model thread reads it to fence a disarm.
	std::atomic<std::size_t> m_producerInFlight{ 0 };
#ifdef LMMS_TESTING
	std::atomic<void ( * )()> m_testPushHook{ nullptr };
	std::atomic<void ( * )()> m_testWaitHook{ nullptr };
#endif
};

} // namespace lmms

#endif // LMMS_SESSION_ARRANGEMENT_RECORDER_H
