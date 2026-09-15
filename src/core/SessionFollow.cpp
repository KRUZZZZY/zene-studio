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
	for( const auto& installed : m_followPlans )
	{
		if( installed.track >= 0 && installed.plan.enabled && installed.plan.count > 0 )
		{
			++armed;
		}
	}
	m_followArmed.store( armed, std::memory_order_relaxed );
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
			slot.state.startedTick = firedAt;
			++slot.state.startCount;
			slot.followScheduled = false;
			slot.followNextTick = 0;
			m_launches.fetch_add( 1, std::memory_order_relaxed );
			publishStart( firedAt, ctx.positionTicks );
			m_recorder.recordStart( slot.track, slot.scene, firedAt );
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

} // namespace lmms
