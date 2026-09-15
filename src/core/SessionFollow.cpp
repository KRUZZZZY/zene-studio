/*
 * SessionFollow.cpp - Follow Action evaluation and Arrangement Record's
 *                     audio-thread side, as SessionScheduler members
 *                     (task #641, SPEC-zene-studio A3 / §4.1).
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

/* WHY THIS IS ITS OWN TRANSLATION UNIT. It is the second half of the session
 * engine and it belongs to SessionScheduler, but src/core/SessionScheduler.cpp
 * is at the file-length ratchet (497 of 500 lines when this landed) and the
 * ratchet is not moved for convenience. Splitting by WHAT RUNS WHEN - the
 * launch state machine in SessionScheduler.cpp, the Follow Action evaluation and
 * the recorder feed here - is the same split the rest of this tree uses for its
 * large command groups, and it keeps the audio-path hook in the launch file to
 * one call.
 *
 * THE ONE INVARIANT THIS FILE EXISTS TO KEEP: everything in it runs on the audio
 * thread, inside SessionScheduler::advanceSlots(), and it allocates, locks and
 * syscalls nothing. Every loop is over fixed storage (m_active, m_followPlans);
 * the only writes to shared state are relaxed/release atomics. The pure
 * decisions it uses live in include/SessionFollow.h and the ring in
 * include/SessionArrangementRecorder.h, both of which a test drives directly.
 */

#include <cstddef>

#include "SessionArrangementRecorder.h"
#include "SessionFollow.h"
#include "SessionScheduler.h"

namespace lmms
{

namespace
{

/*! The tick a Follow Action's outcome starts from: the action time, which is
 *  what the published fire reports and what the next action time is measured
 *  from. Kept here so the three call sites cannot disagree. */
tick_t actionTime( tick_t nextActionTick, tick_t step ) noexcept
{
	return step > 0 ? nextActionTick - step : nextActionTick;
}

} // namespace


// ---------------------------------------------------------------------------
// the model thread's half: installing a plan
// ---------------------------------------------------------------------------

bool SessionScheduler::requestFollowPlan( int track, int scene, const FollowPlan& plan ) noexcept
{
	Command command;
	command.track = track;
	command.scene = scene;
	command.type = LaunchCommandType::Follow;
	command.plan = plan;
	return enqueue( command );
}


// ---------------------------------------------------------------------------
// the audio thread's half
// ---------------------------------------------------------------------------

bool SessionScheduler::installFollowPlan( int track, int scene, const FollowPlan& plan ) noexcept
{
	// A cell already carrying a plan REPLACES it in place: re-arming a cell must
	// not consume a second slot of the fixed table, or a client that arms the
	// same cell twice would exhaust it.
	for( auto& installed : m_followPlans )
	{
		if( installed.track == track && installed.scene == scene )
		{
			installed.plan = plan;
			recountArmedFollowCells();
			return true;
		}
	}
	if( !plan.enabled )
	{
		// Disarming a cell that carries no plan is not a failure and must not
		// consume an entry.
		recountArmedFollowCells();
		return true;
	}
	for( auto& installed : m_followPlans )
	{
		if( installed.track < 0 )
		{
			installed.track = track;
			installed.scene = scene;
			installed.plan = plan;
			recountArmedFollowCells();
			return true;
		}
	}
	return false;
}


void SessionScheduler::recountArmedFollowCells() noexcept
{
	int armed = 0;
	std::uint64_t mask = 0;
	for( const auto& installed : m_followPlans )
	{
		if( installed.track >= 0 && installed.plan.enabled && installed.plan.count > 0 )
		{
			++armed;
			if( installed.track < 8 && installed.scene >= 0 && installed.scene < 8 )
			{
				mask |= std::uint64_t{ 1 } << ( installed.track * 8 + installed.scene );
			}
		}
	}
	m_followArmed.store( armed, std::memory_order_relaxed );
	// One store each, both relaxed: the pair is a diagnostic reading, not a
	// synchronisation primitive (the same rule lastStartLine() follows). A
	// reader may therefore see the count and the mask from two different
	// installs; neither is used to gate anything.
	m_followArmedMask.store( mask, std::memory_order_relaxed );
}


const FollowPlan* SessionScheduler::planFor( int track, int scene ) const noexcept
{
	for( const auto& installed : m_followPlans )
	{
		if( installed.track == track && installed.scene == scene )
		{
			return &installed.plan;
		}
	}
	return nullptr;
}


void SessionScheduler::afterLaunchEvents( ActiveSlot& slot, const SessionClockContext& ctx,
	LaunchEvent event ) noexcept
{
	if( event == LaunchEvent::Started || event == LaunchEvent::Retriggered )
	{
		m_launches.fetch_add( 1, std::memory_order_relaxed );
		// The slot's own scheduled line, not this period's position, so two
		// clips launched for the same bar report the SAME line whatever
		// period noticed them; `observed` says how far past it we were.
		publishStart( slot.state.startedTick, ctx.positionTicks );
		// Follow Actions are measured from the start of THIS playback, and only
		// the engine knows when that was: a schedule carried over from an
		// earlier playback would fire the chain early.
		slot.followScheduled = false;
		slot.followNextTick = 0;
		m_recorder.recordStart( slot.track, slot.scene, slot.state.startedTick );
		return;
	}

	if( event == LaunchEvent::Stopped )
	{
		m_recorder.recordStop( slot.track, slot.scene, slot.state.pendingTick );
		return;
	}

	evaluateFollow( slot, ctx );
}


void SessionScheduler::evaluateFollow( ActiveSlot& slot, const SessionClockContext& ctx ) noexcept
{
	if( slot.state.phase != SlotPhase::Playing )
	{
		return;
	}
	const FollowPlan* plan = planFor( slot.track, slot.scene );
	if( plan == nullptr || !plan->enabled || plan->count <= 0 )
	{
		return;
	}
	const tick_t step = followActionTicks( *plan, ctx.ticksPerBar );
	if( step <= 0 )
	{
		// No clock and no clip length: there is no action time to fire on, and
		// firing every period is the one behaviour that would be worse than
		// not firing at all.
		return;
	}
	if( !slot.followScheduled )
	{
		slot.followScheduled = true;
		slot.followNextTick = slot.state.startedTick + step;
		return;
	}
	if( ctx.positionTicks < slot.followNextTick )
	{
		return;
	}

	FollowEval eval;
	eval.startedTick = slot.state.startedTick;
	eval.positionTicks = ctx.positionTicks;
	eval.ticksPerBar = ctx.ticksPerBar;
	eval.scene = slot.scene;
	// The grid's own height is what the action addresses, and the engine is not
	// given a grid object: the bound travels with the plan (see
	// FollowPlan::sceneCount).
	eval.sceneCount = plan->sceneCount;
	eval.rngUnit = followRandomUnit( m_followRng );

	const FollowFire fire = decideFollowFire( *plan, eval );
	// Advance FIRST and by exactly one step, so this action time fires once:
	// an outcome that does nothing still consumes its action time rather than
	// being re-decided every period.
	const tick_t firedAt = actionTime( slot.followNextTick, step );
	slot.followNextTick += step;

	if( fire.outcome == FollowOutcome::None )
	{
		return;
	}
	m_followFires.fetch_add( 1, std::memory_order_relaxed );
	publishFollowFire( fire, firedAt );

	switch( fire.outcome )
	{
		case FollowOutcome::Stop:
		{
			// The stop is recorded at the action time, not at the period that
			// noticed it - the same rule the start line follows.
			m_recorder.recordStop( slot.track, slot.scene, firedAt );
			slot.state.phase = SlotPhase::Idle;
			slot.state.held = false;
			return;
		}
		case FollowOutcome::Restart:
		{
			// PlayAgain: the cell starts over at its action time. This is
			// counted and published like any other start, so a session driven by
			// Follow Actions is as observable as one driven by the socket.
			//
			// Arrangement Record records NOTHING here, and that is deliberate: a
			// restart is the SAME playback continuing, so its arrangement clip
			// is the one span from the launch to the stop that ends it. A start
			// event here would open a second pair that only a later stop could
			// close, and a performance whose clips all restart would leave the
			// ring full of pairs that never complete.
			slot.state.startedTick = firedAt;
			++slot.state.startCount;
			slot.followScheduled = false;
			slot.followNextTick = 0;
			m_launches.fetch_add( 1, std::memory_order_relaxed );
			publishStart( firedAt, ctx.positionTicks );
			return;
		}
		case FollowOutcome::SwitchScene:
		{
			// Previous / Next / First / Last / Any / Other / Jump: the track
			// COLUMN moves to another cell, which is what a Follow Action means
			// in a grid (SPEC A1: session and arrangement content are mutually
			// exclusive per track, so a column plays one cell at a time).
			m_recorder.recordStop( slot.track, slot.scene, firedAt );
			slot.scene = fire.targetScene;
			slot.state = SlotLaunchState{};
			slot.state.phase = SlotPhase::Playing;
			slot.state.startedTick = firedAt;
			slot.state.startCount = 1;
			slot.followScheduled = false;
			slot.followNextTick = 0;
			// The mode and quantisation the target cell is played with are the
			// plan's own (filled from the cell the plan was armed on), so a
			// column that follows from one clip to the next keeps the behaviour
			// the client set up rather than a default it never chose.
			slot.mode = plan->launchMode;
			slot.quantisation = plan->quantisation;
			m_launches.fetch_add( 1, std::memory_order_relaxed );
			publishStart( firedAt, ctx.positionTicks );
			m_recorder.recordStart( slot.track, fire.targetScene, firedAt );
			return;
		}
		case FollowOutcome::Refused:
		case FollowOutcome::None:
		default:
			return;
	}
}

// ---------------------------------------------------------------------------
// the published reading: the model thread's whole view of the engine's state
// ---------------------------------------------------------------------------

/* WHY THESE BODIES ARE HERE AND NOT IN THE HEADER. They were inline accessors
 * until this task's reset path joined them, and include/SessionScheduler.h is at
 * the file-length ratchet: the accessors are one atomic load each, so an
 * out-of-line definition changes nothing a reader of the header needs except the
 * line budget, and the design they document is in this file and in
 * include/SessionFollow.h. */

int SessionScheduler::armedFollowCells() const noexcept
{
	return m_followArmed.load( std::memory_order_relaxed );
}


std::uint64_t SessionScheduler::armedFollowCellsMask() const noexcept
{
	return m_followArmedMask.load( std::memory_order_relaxed );
}


std::uint64_t SessionScheduler::followFires() const noexcept
{
	return m_followFires.load( std::memory_order_relaxed );
}


std::uint64_t SessionScheduler::lastFollowFire() const noexcept
{
	return m_lastFollowFire.load( std::memory_order_relaxed );
}


SessionArrangementRecorder& SessionScheduler::arrangementRecorder() noexcept
{
	return m_recorder;
}


const SessionArrangementRecorder& SessionScheduler::arrangementRecorder() const noexcept
{
	return m_recorder;
}


// ---------------------------------------------------------------------------
// the reset path - the performance's end, and the end of every plan with it
// ---------------------------------------------------------------------------

/*! DEFINED HERE rather than in src/core/SessionScheduler.cpp, which is at the
 *  file-length ratchet and where this function used to live. It belongs beside
 *  the Arrangement Record feed anyway: what this task changed about the reset is
 *  entirely about the recorder and the Follow Action tables.
 *
 *  Audio thread (called at the top of processAudio, before the clock advances),
 *  bounded loops over fixed storage, no allocation. */
bool SessionScheduler::consumeResetRequest() noexcept
{
	const std::uint32_t generation = m_resetGeneration.load( std::memory_order_acquire );
	if( generation == m_seenGeneration )
	{
		return false;
	}
	m_seenGeneration = generation;
	// Arrangement Record: a reset is the END of the performance for every slot
	// it drops, so each playing slot's stop is recorded first, at the clock the
	// reset was carried out at. Without this the ring would keep START events
	// whose stop never came - the open pairs session.arrangement_record_land
	// refuses to land - and session.back_to_arrangement, whose whole contract is
	// "the session stops and the performance can still be landed", would produce
	// exactly that.
	for( const auto& slot : m_active )
	{
		if( slot.track >= 0
			&& ( slot.state.phase == SlotPhase::Playing
				|| slot.state.phase == SlotPhase::StopPending ) )
		{
			m_recorder.recordStop( slot.track, slot.scene, m_positionTicks );
		}
	}
	for( auto& slot : m_active )
	{
		slot = ActiveSlot{};
	}
	for( auto& installed : m_followPlans )
	{
		installed = InstalledFollowPlan{};
	}
	m_followArmed.store( 0, std::memory_order_relaxed );
	m_followArmedMask.store( 0, std::memory_order_relaxed );
	m_followFires.store( 0, std::memory_order_relaxed );
	m_lastFollowFire.store( 0, std::memory_order_relaxed );
	// Arrangement Record's ring is deliberately NOT cleared: the events it
	// already carries belong to the performance that has just ended, and the
	// caller lands them (the disarm path). Dropping them here is the data loss
	// the feature exists to prevent.
	m_positionTicks = 0;
	m_freeRunFrames = 0.0;
	m_wasRunning = false;
	// A project change starts a fresh session: the launch bookkeeping the
	// model thread can read goes back to zero with the launch state.
	m_launches.store( 0, std::memory_order_relaxed );
	m_lastStartLine.store( 0, std::memory_order_relaxed );
	m_lastStartObservedTick.store( 0, std::memory_order_relaxed );
	return true;
}

} // namespace lmms
