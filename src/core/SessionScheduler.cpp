/*
 * SessionScheduler.cpp - Session View launch scheduler and quantisation engine
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

#include "SessionScheduler.h"

namespace lmms
{

// ---------------------------------------------------------------------------
// Pure launch decision
// ---------------------------------------------------------------------------

tick_t quantisationTicks( LaunchQuantisation quantisation, tick_t ticksPerBar ) noexcept
{
	if( ticksPerBar <= 0 )
	{
		return 0;
	}
	switch( quantisation )
	{
		case LaunchQuantisation::None:
			return 0;
		case LaunchQuantisation::FourBars:
			return 4 * ticksPerBar;
		case LaunchQuantisation::TwoBars:
			return 2 * ticksPerBar;
		case LaunchQuantisation::Bar:
		case LaunchQuantisation::Global:
		default:
			// Global is a pointer to the session default, not a length; a
			// caller that reaches here unresolved gets the session default's
			// own default (one bar, SessionModel::DefaultLaunchQuantisation).
			return ticksPerBar;
	}
}


LaunchQuantisation resolveQuantisation( LaunchQuantisation perClip,
	LaunchQuantisation sessionDefault ) noexcept
{
	if( perClip != LaunchQuantisation::Global )
	{
		return perClip;
	}
	if( sessionDefault == LaunchQuantisation::Global )
	{
		return SessionModel::DefaultLaunchQuantisation;
	}
	return sessionDefault;
}


tick_t launchTickAt( LaunchQuantisation quantisation, const SessionClockContext& ctx ) noexcept
{
	const tick_t quantum = quantisationTicks( quantisation, ctx.ticksPerBar );
	if( quantum <= 0 )
	{
		// None: no musical boundary to wait for - it takes effect at the tick
		// the audio thread is on right now.
		return ctx.positionTicks;
	}
	if( ctx.positionTicks <= 0 )
	{
		return 0;
	}
	// Ceiling to the next grid line, with a position already *on* a line
	// taking effect at that line.
	return ( ( ctx.positionTicks + quantum - 1 ) / quantum ) * quantum;
}

// ---------------------------------------------------------------------------
// Pure launch state machine
// ---------------------------------------------------------------------------

namespace
{

//! Schedules `state` to enter `phase` at `pendingTick`. Nothing else changes.
void scheduleAt( SlotLaunchState& state, SlotPhase phase, tick_t pendingTick ) noexcept
{
	state.phase = phase;
	state.pendingTick = pendingTick;
}


//! Trigger / Gate / Toggle / Repeat, press half.
void pressSlot( SlotLaunchState& state, LaunchMode mode, LaunchQuantisation quantisation,
	const SessionClockContext& ctx ) noexcept
{
	const tick_t at = launchTickAt( quantisation, ctx );

	switch( mode )
	{
		case LaunchMode::Toggle:
			// The second press decides the opposite of what is scheduled:
			// cancel a pending start, stop a playing clip, or cancel a
			// pending stop.
			if( state.phase == SlotPhase::LaunchPending )
			{
				state.phase = SlotPhase::Idle;
				return;
			}
			if( state.phase == SlotPhase::Playing )
			{
				state.held = false;
				scheduleAt( state, SlotPhase::StopPending, at );
				return;
			}
			if( state.phase == SlotPhase::StopPending )
			{
				state.phase = SlotPhase::Playing;
				return;
			}
			break;

		case LaunchMode::Gate:
		case LaunchMode::Repeat:
			// Held modes: the press arms the slot and the release is what ends
			// it - and what cancels it before it ever starts.
			state.held = true;
			break;

		case LaunchMode::Trigger:
		default:
			// Trigger ignores the release, so `held` stays false and only an
			// explicit stop ends it. A re-press of a playing slot re-launches
			// it from the grid line: same code path as the first press.
			break;
	}

	state.retriggerTicks = quantisationTicks( quantisation, ctx.ticksPerBar );
	scheduleAt( state, SlotPhase::LaunchPending, at );
}


//! Gate / Repeat, release half. Trigger and Toggle ignore the release.
void releaseSlot( SlotLaunchState& state, LaunchMode mode, LaunchQuantisation quantisation,
	const SessionClockContext& ctx ) noexcept
{
	if( mode == LaunchMode::Trigger || mode == LaunchMode::Toggle )
	{
		return;
	}
	state.held = false;
	if( state.phase == SlotPhase::LaunchPending )
	{
		// Released before the launch fired: it never plays at all.
		state.phase = SlotPhase::Idle;
		return;
	}
	if( state.phase == SlotPhase::Playing )
	{
		scheduleAt( state, SlotPhase::StopPending, launchTickAt( quantisation, ctx ) );
	}
}


//! Explicit stop, whatever the mode. A slot that never started stays idle.
void stopSlot( SlotLaunchState& state, LaunchQuantisation quantisation,
	const SessionClockContext& ctx ) noexcept
{
	state.held = false;
	if( state.phase == SlotPhase::Idle )
	{
		return;
	}
	scheduleAt( state, SlotPhase::StopPending, launchTickAt( quantisation, ctx ) );
}

} // namespace


void applyLaunchCommand( SlotLaunchState& state, LaunchMode mode,
	LaunchQuantisation quantisation, LaunchCommandType command,
	const SessionClockContext& ctx ) noexcept
{
	switch( command )
	{
		case LaunchCommandType::Press:
			pressSlot( state, mode, quantisation, ctx );
			return;
		case LaunchCommandType::Release:
			releaseSlot( state, mode, quantisation, ctx );
			return;
		case LaunchCommandType::Stop:
		default:
			stopSlot( state, quantisation, ctx );
			return;
	}
}


LaunchEvent advanceLaunchState( SlotLaunchState& state, LaunchMode mode,
	const SessionClockContext& ctx ) noexcept
{
	if( state.phase == SlotPhase::LaunchPending && ctx.positionTicks >= state.pendingTick )
	{
		state.phase = SlotPhase::Playing;
		state.startedTick = state.pendingTick;
		++state.startCount;
		return LaunchEvent::Started;
	}

	if( state.phase == SlotPhase::StopPending && ctx.positionTicks >= state.pendingTick )
	{
		state.phase = SlotPhase::Idle;
		state.held = false;
		return LaunchEvent::Stopped;
	}

	// Repeat is the only mode that restarts on its own: while the trigger is
	// held the clip starts over at every grid line (Gate would sit on the one
	// launch; Trigger would not restart at all).
	if( mode == LaunchMode::Repeat && state.phase == SlotPhase::Playing && state.held
		&& state.retriggerTicks > 0 )
	{
		const tick_t next = state.startedTick + state.retriggerTicks;
		if( ctx.positionTicks >= next )
		{
			state.startedTick = next;
			++state.startCount;
			return LaunchEvent::Retriggered;
		}
	}

	return LaunchEvent::None;
}

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

bool SessionScheduler::enqueue( const Command& command ) noexcept
{
	if( m_queue.push( command ) )
	{
		return true;
	}
	m_dropped.fetch_add( 1, std::memory_order_relaxed );
	return false;
}


bool SessionScheduler::requestLaunch( int track, int scene, LaunchMode mode,
	LaunchQuantisation quantisation ) noexcept
{
	return enqueue( Command{ track, scene, LaunchCommandType::Press, mode, quantisation } );
}


bool SessionScheduler::requestRelease( int track, int scene, LaunchMode mode,
	LaunchQuantisation quantisation ) noexcept
{
	return enqueue( Command{ track, scene, LaunchCommandType::Release, mode, quantisation } );
}


bool SessionScheduler::requestStop( int track, int scene, LaunchMode mode,
	LaunchQuantisation quantisation ) noexcept
{
	return enqueue( Command{ track, scene, LaunchCommandType::Stop, mode, quantisation } );
}


void SessionScheduler::reset() noexcept
{
	m_resetGeneration.fetch_add( 1, std::memory_order_release );
}


SessionScheduler::ActiveSlot* SessionScheduler::findSlot( int track, int scene ) noexcept
{
	for( auto& slot : m_active )
	{
		if( slot.track == track && slot.scene == scene )
		{
			return &slot;
		}
	}
	return nullptr;
}


SessionScheduler::ActiveSlot* SessionScheduler::claimSlot( int track, int scene ) noexcept
{
	for( auto& slot : m_active )
	{
		if( slot.track < 0 )
		{
			slot = ActiveSlot{};
			slot.track = track;
			slot.scene = scene;
			return &slot;
		}
	}
	return nullptr;
}


void SessionScheduler::drainCommands( const SessionClockContext& ctx ) noexcept
{
	Command command;
	while( m_queue.pop( command ) )
	{
		m_processed.fetch_add( 1, std::memory_order_relaxed );
		ActiveSlot* slot = findSlot( command.track, command.scene );

		if( command.type == LaunchCommandType::Press )
		{
			if( slot == nullptr )
			{
				slot = claimSlot( command.track, command.scene );
			}
			if( slot == nullptr )
			{
				// No free entry: the press is lost rather than allocated for.
				m_dropped.fetch_add( 1, std::memory_order_relaxed );
				continue;
			}
			// The mode and quantisation of the *press* decide how the slot
			// behaves; a Release/Stop later uses what was stored here, so a
			// model edit mid-launch cannot half-apply.
			slot->mode = command.mode;
			slot->quantisation = command.quantisation;
		}
		else if( slot == nullptr )
		{
			// Nothing is launched on that slot, so a release or stop is a
			// no-op (in particular a Gate release never seen by a press).
			continue;
		}

		applyLaunchCommand( slot->state, slot->mode, slot->quantisation,
			command.type, ctx );
	}
}


bool SessionScheduler::consumeResetRequest() noexcept
{
	const std::uint32_t generation = m_resetGeneration.load( std::memory_order_acquire );
	if( generation == m_seenGeneration )
	{
		return false;
	}
	m_seenGeneration = generation;
	for( auto& slot : m_active )
	{
		slot = ActiveSlot{};
	}
	m_positionTicks = 0;
	m_freeRunFrames = 0.0;
	m_wasRunning = false;
	// A project change starts a fresh session: the launch bookkeeping the
	// model thread can read goes back to zero with the launch state.
	m_launches.store( 0, std::memory_order_relaxed );
	return true;
}


void SessionScheduler::advanceClock( const SessionClockContext& snapshot,
	f_cnt_t framesThisPeriod ) noexcept
{
	// The session clock is its own domain (SPEC A2). While the song transport
	// runs the session clock follows it, so launches land on the arrangement's
	// grid lines; while it is stopped the session clock free-runs, so a launch
	// can still be scheduled with no transport at all.
	if( snapshot.transportRunning )
	{
		m_positionTicks = snapshot.positionTicks;
		m_wasRunning = true;
		return;
	}
	if( snapshot.framesPerTick <= 0.0f )
	{
		return;
	}
	if( m_wasRunning )
	{
		m_freeRunFrames = static_cast<double>( m_positionTicks )
			* static_cast<double>( snapshot.framesPerTick );
		m_wasRunning = false;
	}
	m_freeRunFrames += static_cast<double>( framesThisPeriod );
	m_positionTicks = static_cast<tick_t>(
		m_freeRunFrames / static_cast<double>( snapshot.framesPerTick ) );
}


void SessionScheduler::advanceSlots( const SessionClockContext& ctx ) noexcept
{
	for( auto& slot : m_active )
	{
		if( slot.track < 0 )
		{
			continue;
		}
		const LaunchEvent event = advanceLaunchState( slot.state, slot.mode, ctx );
		if( event == LaunchEvent::Started || event == LaunchEvent::Retriggered )
		{
			m_launches.fetch_add( 1, std::memory_order_relaxed );
		}
		if( slot.state.phase == SlotPhase::Idle )
		{
			// Finished (or cancelled): give the entry back so the table stays
			// a fixed bound rather than growing with the launch traffic.
			slot = ActiveSlot{};
		}
	}
}


void SessionScheduler::processAudio( const SessionClockContext& snapshot,
	f_cnt_t framesThisPeriod ) noexcept
{
	consumeResetRequest();
	advanceClock( snapshot, framesThisPeriod );

	SessionClockContext ctx = snapshot;
	ctx.positionTicks = m_positionTicks;

	drainCommands( ctx );
	advanceSlots( ctx );
}


bool SessionScheduler::trackIsSessionActive( int track ) const noexcept
{
	for( const auto& slot : m_active )
	{
		// StopPending counts as active: the clip has not stopped yet, so the
		// track is still taken over until the scheduled stop fires.
		if( slot.track == track
			&& ( slot.state.phase == SlotPhase::Playing
				|| slot.state.phase == SlotPhase::StopPending ) )
		{
			return true;
		}
	}
	return false;
}


SlotPhase SessionScheduler::slotPhase( int track, int scene ) const noexcept
{
	for( const auto& slot : m_active )
	{
		if( slot.track == track && slot.scene == scene )
		{
			return slot.state.phase;
		}
	}
	return SlotPhase::Idle;
}


int SessionScheduler::activeSlotCount() const noexcept
{
	int count = 0;
	for( const auto& slot : m_active )
	{
		if( slot.track >= 0 )
		{
			++count;
		}
	}
	return count;
}

} // namespace lmms
