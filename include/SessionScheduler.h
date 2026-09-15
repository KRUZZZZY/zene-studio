/*
 * SessionScheduler.h - Session View launch scheduler and quantisation engine
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

#ifndef LMMS_SESSION_SCHEDULER_H
#define LMMS_SESSION_SCHEDULER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "LmmsTypes.h"
#include "SessionArrangementRecorder.h"
#include "SessionFollow.h"
#include "SessionModel.h"
#include "lmms_export.h"

namespace lmms
{

/*! Session View launch scheduler (task #595, SPEC-zene-studio A2/A3).
 *
 *  The engine is split in two halves that share nothing but a lock-free
 *  command queue:
 *
 *   - a *pure* half - `quantisationTicks()`, `resolveQuantisation()`,
 *     `launchTickAt()`, `applyLaunchCommand()` and `advanceLaunchState()`.
 *     No globals, no threads, no audio state: a test constructs a
 *     `SessionClockContext` and a `SlotLaunchState` and drives the whole
 *     decision exhaustively.
 *
 *   - the *engine* - `SessionScheduler` - which owns the launch state of every
 *     launched slot and consumes the queue from the audio thread.
 *
 *  THREADING. The launch state belongs to the audio thread. Nothing else ever
 *  reads or writes it. The model/GUI thread crosses over with `requestLaunch()`
 *  / `requestRelease()` / `requestStop()` / `reset()`, which do nothing but an
 *  atomic push (or an atomic increment) into fixed storage: no allocation, no
 *  lock, no syscall. The audio thread drains that queue in `processAudio()`.
 *  The command only says *what* was asked; *when* it takes effect is decided on
 *  the audio thread, from the audio thread's own clock - the GUI cannot know
 *  the sample-accurate transport position.
 *
 *  Quantisation is exactly the set the data layer already persists
 *  (SessionModel.h:70, `LaunchQuantisation`): None / 1 bar / 2 bars / 4 bars,
 *  plus Global which defers to the session default. That set is Live's, it is
 *  the one SPEC-zene-studio §4.1 names, and it is the one the versioned
 *  <session> block already round-trips - so the engine implements it rather
 *  than inventing a second vocabulary.
 */

// ---------------------------------------------------------------------------
// Pure launch decision
// ---------------------------------------------------------------------------

/*! Everything the pure decision needs to know about the clock. A caller reads
 *  these from the engine (see SessionScheduler::processAudio) or a test just
 *  fills them in. */
struct SessionClockContext
{
	//! The session clock's position right now, in ticks.
	tick_t positionTicks = 0;
	//! Ticks in one bar for the current time signature
	//! (Song::ticksPerBar() -> TimePos::ticksPerBar(), include/Song.h:132).
	tick_t ticksPerBar = 0;
	//! Frames in one tick (Engine::framesPerTick(), include/Engine.h:96).
	float framesPerTick = 0.0f;
	//! The song transport is running (as opposed to the session clock
	//! free-running on its own - SPEC A2's separate clock domain).
	bool transportRunning = false;
};

/*! Length of one quantisation step in ticks; 0 for None, which means "no
 *  musical boundary - take effect as soon as the audio thread sees it".
 *  Global is not a length: it is resolved by resolveQuantisation() first, and
 *  is treated as 1 bar here so a caller that forgot cannot ask for a 0-tick
 *  grid by accident. */
tick_t quantisationTicks( LaunchQuantisation quantisation, tick_t ticksPerBar ) noexcept;

//! Resolves a per-clip quantisation against the session default (the only legal
//! meaning of LaunchQuantisation::Global).
LaunchQuantisation resolveQuantisation( LaunchQuantisation perClip,
	LaunchQuantisation sessionDefault ) noexcept;

/*! The tick at which a launch requested while the clock reads
 *  `ctx.positionTicks` takes effect: the next grid line *at or after* the
 *  request, so a request that lands exactly on a boundary fires at that
 *  boundary. Pure - the same inputs always give the same tick. */
tick_t launchTickAt( LaunchQuantisation quantisation, const SessionClockContext& ctx ) noexcept;

// ---------------------------------------------------------------------------
// Pure launch state machine
// ---------------------------------------------------------------------------

//! What the GUI/model thread asks for. Stop is mode-independent (an explicit
//! "stop this slot" from the UI or the API); Press/Release are the trigger. */
enum class LaunchCommandType : std::uint8_t
{
	Press = 0,
	Release,
	Stop,
	/*! Installs a Follow Action plan (or clears one, with `enabled` false) on a
	 *  cell. It touches no launch state: the plan is stored and the slot's own
	 *  state machine consults it while it plays (task #641). Same queue, same
	 *  producer, same POD rule as the three above. */
	Follow
};

//! Where a slot is in its launch life cycle.
enum class SlotPhase : std::uint8_t
{
	Idle = 0,     //!< nothing scheduled, nothing playing
	LaunchPending,//!< a start is scheduled for pendingTick
	Playing,      //!< started, not (yet) stopping
	StopPending   //!< a stop is scheduled for pendingTick
};

//! What the clock has reached, returned by advanceLaunchState().
enum class LaunchEvent : std::uint8_t
{
	None = 0,
	Started,     //!< playback begins at pendingTick (or a Trigger re-launch)
	Retriggered, //!< Repeat: the clip started over at a grid line while held
	Stopped      //!< playback ends at pendingTick
};

/*! The launch state of one slot. Owned by the audio thread; trivially
 *  copyable so it can live in fixed storage. */
struct SlotLaunchState
{
	SlotPhase phase = SlotPhase::Idle;
	//! Tick the pending transition fires at (LaunchPending / StopPending).
	tick_t pendingTick = 0;
	//! Tick playback last started at (Repeat retriggers from here).
	tick_t startedTick = 0;
	//! Grid step in ticks captured when the launch was requested; 0 means the
	//! slot was launched with None quantisation (no grid to retrigger on).
	tick_t retriggerTicks = 0;
	//! How many times playback has started (Started + Retriggered events).
	std::uint32_t startCount = 0;
	//! Gate/Repeat: the trigger is still held.
	bool held = false;
};

/*! Applies one command to a slot's state. Pure; `state` is the only thing it
 *  touches. `quantisation` must already be resolved (never Global). */
void applyLaunchCommand( SlotLaunchState& state, LaunchMode mode,
	LaunchQuantisation quantisation, LaunchCommandType command,
	const SessionClockContext& ctx ) noexcept;

/*! Fires whatever `ctx` has reached and advances `state` accordingly. Pure.
 *  Call once per audio period, before using the resulting phase. */
LaunchEvent advanceLaunchState( SlotLaunchState& state, LaunchMode mode,
	const SessionClockContext& ctx ) noexcept;

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

//! Grid line of a lastStartLine() value (the tick the start events were
//! scheduled on, i.e. SlotLaunchState::startedTick - not the audio period that
//! noticed them).
inline tick_t startLineTick( std::uint64_t packed ) noexcept
{
	return static_cast<tick_t>( packed >> 32 );
}

//! Start events that fired on that one grid line.
inline std::uint32_t startLineStarts( std::uint64_t packed ) noexcept
{
	return static_cast<std::uint32_t>( packed & 0xffffffffULL );
}

//! Packs (grid line, start count) the way lastStartLine() reports them.
inline std::uint64_t packStartLine( tick_t line, std::uint32_t starts ) noexcept
{
	return ( static_cast<std::uint64_t>( static_cast<std::uint32_t>( line ) ) << 32 ) | starts;
}

/*! Owns the launch state of the launched slots and the lock-free command
 *  queue. See the file comment for the threading contract. */
class LMMS_EXPORT SessionScheduler
{
public:
	//! Concurrently launched slots. A press with no free entry is dropped and
	//! counted (droppedCommands()) rather than allocating.
	static constexpr std::size_t MaxActiveSlots = 64;
	//! Commands that can be in flight model thread -> audio thread.
	static constexpr std::size_t CommandQueueCapacity = 256;

	SessionScheduler() = default;
	SessionScheduler( const SessionScheduler& ) = delete;
	SessionScheduler& operator=( const SessionScheduler& ) = delete;

	// ---- model/GUI thread ---------------------------------------------

	/*! Queues a trigger press. Returns false when the queue is full (the
	 *  request is dropped; nothing is allocated either way). */
	bool requestLaunch( int track, int scene, LaunchMode mode,
		LaunchQuantisation quantisation ) noexcept;
	//! Queues a trigger release (Gate/Repeat; ignored by Trigger/Toggle).
	bool requestRelease( int track, int scene, LaunchMode mode,
		LaunchQuantisation quantisation ) noexcept;
	//! Queues an explicit stop, in the slot's own mode.
	bool requestStop( int track, int scene, LaunchMode mode,
		LaunchQuantisation quantisation ) noexcept;

	/*! Drops every launched slot on the audio thread's next period. Used when
	 *  the project changes under the engine. Lock-free and allocation-free:
	 *  it is one atomic increment, so it is safe from anywhere. */
	void reset() noexcept;

	//! Commands refused because the queue was full. Any thread.
	std::uint64_t droppedCommands() const noexcept
	{
		return m_dropped.load( std::memory_order_relaxed );
	}

	// ---- audio thread --------------------------------------------------

	/*! Advances the session clock, drains the command queue and fires any
	 *  launch the clock has reached. Allocation-free and lock-free: the whole
	 *  path is a bounded loop over fixed storage
	 *  (see SessionSchedulerTest::audioThreadPathDoesNotAllocate).
	 *
	 *  `snapshot` carries the song's view of the clock for this period;
	 *  `framesThisPeriod` is how far the session clock advances when the song
	 *  transport is not running. */
	void processAudio( const SessionClockContext& snapshot,
		f_cnt_t framesThisPeriod ) noexcept;

	/*! True while a slot on this track column is Playing - the track's session
	 *  content has taken it over, so its arrangement content must not play
	 *  (SPEC-zene-studio A1 mutual exclusivity). Audio thread. */
	bool trackIsSessionActive( int track ) const noexcept;

	/*! Phase of the slot at (track, scene), Idle when it has never launched or
	 *  has finished. Audio thread. */
	SlotPhase slotPhase( int track, int scene ) const noexcept;

	//! Slots currently holding state. Audio thread.
	int activeSlotCount() const noexcept;

	//! Starts since the last reset() (Started + Retriggered). Any thread.
	std::uint64_t completedLaunches() const noexcept
	{
		return m_launches.load( std::memory_order_relaxed );
	}

	/*! Commands the audio thread has consumed since the last reset(). Relaxed
	 *  and allocation-free; it exists so the cross-thread hand-off can be
	 *  checked for loss without a debugger. Any thread. */
	std::uint64_t processedCommands() const noexcept
	{
		return m_processed.load( std::memory_order_relaxed );
	}

	/*! The most recent start events, packed as ONE value so a reader can never
	 *  see a grid line and a count from different events: see packStartLine()
	 *  / startLineTick() / startLineStarts(). `startLineStarts() == 4` after
	 *  four clips were launched at one bar line is the session-launch sync
	 *  proof - four starts on ONE grid line, reported without reading the
	 *  audio thread's slot table from the model thread.
	 *
	 *  A later start on a different line REPLACES the pair with a count of 1,
	 *  so the value always describes the newest line only. Any thread; relaxed,
	 *  because it is a diagnostic and never a synchronisation primitive. */
	std::uint64_t lastStartLine() const noexcept
	{
		return m_lastStartLine.load( std::memory_order_relaxed );
	}

	/*! The audio thread's clock when it noticed the most recent start event.
	 *  The distance from startLineTick(lastStartLine()) is the audio period the
	 *  event was noticed in - it is what makes the grid line a measurement
	 *  rather than a restatement of the request. Any thread. */
	tick_t lastStartObservedTick() const noexcept
	{
		return m_lastStartObservedTick.load( std::memory_order_relaxed );
	}

	//! The session clock's tick position after the last processAudio(). Audio
	//! thread.
	tick_t positionTicks() const noexcept { return m_positionTicks; }

	// ---- Follow Actions and Arrangement Record (task #641, SPEC §4.1) ---

	/*! Installs a cell's Follow Action plan, or clears it when
	 *  `plan.enabled` is false. Model thread; one queue push, no allocation.
	 *  False when the queue is full (dropped, counted), like the launches.
	 *
	 *  The accessors below are the model thread's WHOLE view of the engine's
	 *  Follow Action state, because the installed plans are audio-thread
	 *  storage: the armed cells as a count and as a bitmask (bit
	 *  `track * 8 + scene`, for track < 8 and scene < 8 - the count cannot
	 *  answer WHICH cell), the fires since the last reset(), and the newest fire
	 *  packed as ONE 64-bit word, so a reader can never pair a new outcome with
	 *  the previous tick. Their bodies are in src/core/SessionFollow.cpp with
	 *  the rest of the feature, so this header stays inside the ratchet. */
	bool requestFollowPlan( int track, int scene, const FollowPlan& plan ) noexcept;
	int armedFollowCells() const noexcept;
	std::uint64_t armedFollowCellsMask() const noexcept;
	std::uint64_t followFires() const noexcept;
	std::uint64_t lastFollowFire() const noexcept;

	//! Arrangement Record's event ring (task #641): the model thread consumes it
	//! (ControlCommandsSessionRecord.cpp); the audio thread feeds it while armed.
	SessionArrangementRecorder& arrangementRecorder() noexcept;
	const SessionArrangementRecorder& arrangementRecorder() const noexcept;

private:
	struct Command
	{
		int track = 0;
		int scene = 0;
		LaunchCommandType type = LaunchCommandType::Press;
		LaunchMode mode = LaunchMode::Trigger;
		LaunchQuantisation quantisation = LaunchQuantisation::Bar;
		//! Only read for LaunchCommandType::Follow; a POD payload, so the queue
		//! stays a fixed array of trivially copyable elements.
		FollowPlan plan;
	};

	/*! Fixed-capacity single-producer/single-consumer queue. The model thread
	 *  only touches m_write and m_data[]; the audio thread only m_read and
	 *  m_data[]. Indices are published with release stores and observed with
	 *  acquire loads - the same invariant class as the in-tree WASM ring
	 *  (src/wasm/WasmSpscRingBuffer.h). */
	class CommandQueue
	{
	public:
		bool push( const Command& command ) noexcept
		{
			const std::size_t write = m_write.load( std::memory_order_relaxed );
			const std::size_t next = ( write + 1 ) % storageSize;
			if( next == m_read.load( std::memory_order_acquire ) )
			{
				return false;
			}
			m_data[write] = command;
			m_write.store( next, std::memory_order_release );
			return true;
		}

		bool pop( Command& command ) noexcept
		{
			const std::size_t read = m_read.load( std::memory_order_relaxed );
			if( read == m_write.load( std::memory_order_acquire ) )
			{
				return false;
			}
			command = m_data[read];
			m_read.store( ( read + 1 ) % storageSize, std::memory_order_release );
			return true;
		}

	private:
		static constexpr std::size_t storageSize = CommandQueueCapacity + 1;
		std::array<Command, storageSize> m_data{};
		std::atomic<std::size_t> m_read{ 0 };
		std::atomic<std::size_t> m_write{ 0 };
	};

	struct ActiveSlot
	{
		int track = -1;
		int scene = -1;
		LaunchMode mode = LaunchMode::Trigger;
		LaunchQuantisation quantisation = LaunchQuantisation::Bar;
		SlotLaunchState state;
		//! Follow Actions: the action time this slot waits for, and whether it
		//! has been initialised for the playback running now. Audio thread.
		bool followScheduled = false;
		tick_t followNextTick = 0;
	};

	/*! One installed plan. A separate fixed table rather than a member of
	 *  ActiveSlot, because a plan outlives the playback it was installed for and
	 *  a cell that has never launched has no ActiveSlot to hang it on. */
	struct InstalledFollowPlan
	{
		int track = -1;
		int scene = -1;
		FollowPlan plan;
	};

	bool enqueue( const Command& command ) noexcept;
	ActiveSlot* findSlot( int track, int scene ) noexcept;
	ActiveSlot* claimSlot( int track, int scene ) noexcept;
	void drainCommands( const SessionClockContext& ctx ) noexcept;
	/*! Publishes one start event for the model thread: repacks the pair when the
	 *  event landed on the line already reported, so the count is the number of
	 *  clips that began on THAT line. Audio thread; two relaxed stores. Defined
	 *  here rather than in the .cpp because that file is at the file-length
	 *  ratchet and this is the whole of it - the surrounding state machine lives
	 *  in the .cpp, the hot path belongs beside the atomics it writes. */
	void publishStart( tick_t line, tick_t observed ) noexcept
	{
		const std::uint64_t previous = m_lastStartLine.load( std::memory_order_relaxed );
		const std::uint32_t starts = startLineStarts( previous );
		const std::uint32_t count = ( startLineTick( previous ) == line && starts > 0 )
			? starts + 1 : 1;
		m_lastStartObservedTick.store( observed, std::memory_order_relaxed );
		// The pair goes in as ONE store, so a reader can never see the new line
		// with the old count (or the reverse) and conclude a synchronisation
		// that did not happen.
		m_lastStartLine.store( packStartLine( line, count ), std::memory_order_relaxed );
	}
	//! Applies a pending reset() request. True when everything was dropped.
	bool consumeResetRequest() noexcept;
	//! Moves the session clock for this period (SPEC A2's separate domain).
	void advanceClock( const SessionClockContext& snapshot, f_cnt_t framesThisPeriod ) noexcept;
	//! Fires whatever the clock has reached, one pass over the active slots.
	void advanceSlots( const SessionClockContext& ctx ) noexcept;

	// ---- Follow Actions, audio thread (bodies in SessionFollow.cpp) -----
	//! Stores (or clears) one cell's plan; false when the fixed table is full.
	bool installFollowPlan( int track, int scene, const FollowPlan& plan ) noexcept;
	//! The plan installed for a cell, or nullptr. Audio thread.
	const FollowPlan* planFor( int track, int scene ) const noexcept;
	/*! Evaluates one playing slot's chain against the clock, firing at most one
	 *  action per action time. Audio thread; the rules are in SessionFollow.h. */
	void evaluateFollow( ActiveSlot& slot, const SessionClockContext& ctx ) noexcept;
	/*! Everything the launch state machine's events imply for this task: the
	 *  launch counters and the start-line publication, the slot's schedule, and
	 *  the Arrangement Record's ring. One place, so they cannot disagree about
	 *  what happened this period. Audio thread. */
	void afterLaunchEvents( ActiveSlot& slot, const SessionClockContext& ctx,
		LaunchEvent event ) noexcept;
	//! Publishes a fire for the model thread. Audio thread; one relaxed store.
	void publishFollowFire( const FollowFire& fire, tick_t tick ) noexcept
	{
		m_lastFollowFire.store( packFollowFire( fire.outcome, fire.chosenIndex,
			fire.targetScene, tick ), std::memory_order_relaxed );
	}
	//! Audio thread: how many installed cells have an ENABLED plan.
	void recountArmedFollowCells() noexcept;

	std::array<ActiveSlot, MaxActiveSlots> m_active{};
	std::array<InstalledFollowPlan, MaxFollowPlans> m_followPlans{};
	CommandQueue m_queue;
	std::atomic<std::uint64_t> m_dropped{ 0 };
	std::atomic<std::uint64_t> m_launches{ 0 };
	std::atomic<std::uint64_t> m_processed{ 0 };
	//! Packed (grid line, start count) of the newest start events; see
	//! lastStartLine(). Model-thread readable, so it is a plain relaxed atomic
	//! rather than a member of the audio thread's slot table.
	std::atomic<std::uint64_t> m_lastStartLine{ 0 };
	//! The audio clock when that event was noticed; see lastStartObservedTick().
	std::atomic<tick_t> m_lastStartObservedTick{ 0 };
	//! Project-change generation; bumping it makes the audio thread drop every
	//! active slot on its next period (see reset()).
	std::atomic<std::uint32_t> m_resetGeneration{ 0 };
	std::uint32_t m_seenGeneration = 0;

	// ---- Follow Actions and Arrangement Record (task #641) --------------
	//! Newest fire, packed; see lastFollowFire().
	std::atomic<std::uint64_t> m_lastFollowFire{ 0 };
	std::atomic<std::uint64_t> m_followFires{ 0 };
	//! Cells with an enabled plan installed; see armedFollowCells().
	std::atomic<int> m_followArmed{ 0 };
	//! The same set as a bitmask, bit (track * 8 + scene); same store as the
	//! count. See armedFollowCellsMask().
	std::atomic<std::uint64_t> m_followArmedMask{ 0 };
	//! The audio thread's Follow Action RNG (xorshift32); needs no atomic.
	std::uint32_t m_followRng = 0x2545f491u;
	//! Arrangement Record's ring: fed by the audio thread, drained by the model
	//! thread (include/SessionArrangementRecorder.h).
	SessionArrangementRecorder m_recorder;

	// ---- audio-thread-only session clock (SPEC A2) ---------------------
	tick_t m_positionTicks = 0;
	double m_freeRunFrames = 0.0;
	bool m_wasRunning = false;
};

} // namespace lmms

#endif // LMMS_SESSION_SCHEDULER_H
