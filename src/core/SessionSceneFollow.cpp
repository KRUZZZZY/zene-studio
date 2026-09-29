/*
 * SessionSceneFollow.cpp - R5.2: Follow Action chains on SCENES
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

/* A cell's chain moves ONE column (SessionFollow.cpp). A scene's chain moves the ROW: when
 * a scene is launched as a row (session.launch_scene -> requestSceneLaunch) and it carries
 * an installed chain, then at the chain's action time every column still playing that row
 * stops, restarts, or moves to the target row together, on one grid line. While it runs it
 * takes precedence over the chains of the cells in that row (sceneOverrides, consulted by
 * evaluateFollow) - two chains deciding for one column at once would be two answers.
 *
 * The decision is the same pure function the cells use (decideFollowFire, SessionFollow.h),
 * so the ten action types, the chance weighting and the timing rule mean the same thing on
 * a row as on a cell. Linked timing is the row's LONGEST cell loop (the plan's
 * clipLengthTicks, filled by the command), falling back to one bar.
 *
 * The chain ends - without firing - when the row it follows has no column left playing
 * (a stop_all, or every column launched elsewhere), and on a reset. Everything here runs on
 * the audio thread over fixed tables: no allocation, no lock (AGENTS.md rule 4).
 */

#include <array>

#include "SessionArrangementRecorder.h"
#include "SessionFollow.h"
#include "SessionScheduler.h"

namespace lmms
{

// ---------------------------------------------------------------------------
// the model thread's half
// ---------------------------------------------------------------------------

bool SessionScheduler::requestSceneFollowPlan( int scene, const FollowPlan& plan ) noexcept
{
	Command command;
	command.scene = scene;
	command.type = LaunchCommandType::SceneFollow;
	command.plan = plan;
	return enqueue( command );
}


bool SessionScheduler::requestSceneLaunch( int scene, LaunchQuantisation quantisation ) noexcept
{
	Command command;
	command.scene = scene;
	command.type = LaunchCommandType::SceneLaunch;
	command.quantisation = quantisation;
	return enqueue( command );
}


int SessionScheduler::activeScene() const noexcept
{
	return m_activeSceneReading.load( std::memory_order_relaxed );
}


int SessionScheduler::armedFollowScenes() const noexcept
{
	return m_armedScenes.load( std::memory_order_relaxed );
}


std::uint64_t SessionScheduler::sceneFollowFires() const noexcept
{
	return m_sceneFollowFires.load( std::memory_order_relaxed );
}

// ---------------------------------------------------------------------------
// the audio thread's half
// ---------------------------------------------------------------------------

bool SessionScheduler::installScenePlan( int scene, const FollowPlan& plan ) noexcept
{
	InstalledScenePlan* target = nullptr;
	for( auto& installed : m_scenePlans )
	{
		if( installed.scene == scene ) { target = &installed; break; }
	}
	if( target == nullptr && plan.enabled )
	{
		for( auto& installed : m_scenePlans )
		{
			if( installed.scene < 0 ) { target = &installed; break; }
		}
		if( target == nullptr ) { return false; }
	}
	if( target != nullptr )
	{
		target->scene = scene;
		target->plan = plan;
	}
	int armed = 0;
	for( const auto& installed : m_scenePlans )
	{
		if( installed.scene >= 0 && installed.plan.enabled && installed.plan.count > 0 ) { ++armed; }
	}
	m_armedScenes.store( armed, std::memory_order_relaxed );
	return true;
}


const FollowPlan* SessionScheduler::scenePlanFor( int scene ) const noexcept
{
	for( const auto& installed : m_scenePlans )
	{
		if( installed.scene == scene && installed.plan.enabled && installed.plan.count > 0 )
		{
			return &installed.plan;
		}
	}
	return nullptr;
}


bool SessionScheduler::sceneOverrides( const ActiveSlot& slot ) const noexcept
{
	return m_activeScene >= 0 && slot.scene == m_activeScene && scenePlanFor( m_activeScene ) != nullptr;
}


void SessionScheduler::startScene( int scene, tick_t startTick ) noexcept
{
	m_activeScene = scene;
	m_sceneStartTick = startTick;
	m_sceneFollowScheduled = false;
	m_sceneFollowNext = 0;
	m_activeSceneReading.store( scene, std::memory_order_relaxed );
}


void SessionScheduler::resetSceneFollow() noexcept
{
	for( auto& installed : m_scenePlans ) { installed = InstalledScenePlan{}; }
	startScene( -1, 0 );
	m_armedScenes.store( 0, std::memory_order_relaxed );
	m_sceneFollowFires.store( 0, std::memory_order_relaxed );
}


void SessionScheduler::evaluateSceneFollow( const SessionClockContext& ctx ) noexcept
{
	if( m_activeScene < 0 || !m_followEnabled.load( std::memory_order_relaxed ) ) { return; }
	const FollowPlan* plan = scenePlanFor( m_activeScene );
	// The row has not started yet (its presses are still pending): nothing to time.
	if( plan == nullptr || ctx.positionTicks < m_sceneStartTick ) { return; }
	const tick_t step = followActionTicks( *plan, ctx.ticksPerBar );
	if( step <= 0 ) { return; }
	if( !m_sceneFollowScheduled )
	{
		m_sceneFollowScheduled = true;
		m_sceneFollowNext = m_sceneStartTick + step;
		return;
	}
	if( ctx.positionTicks < m_sceneFollowNext ) { return; }

	bool rowPlaying = false;
	for( const auto& slot : m_active )
	{
		rowPlaying = rowPlaying || ( slot.track >= 0 && slot.scene == m_activeScene
			&& slot.state.phase == SlotPhase::Playing );
	}
	if( !rowPlaying )
	{
		// Nothing of the row is left to move: the chain ends rather than firing into
		// an empty row every action time.
		startScene( -1, 0 );
		return;
	}

	FollowEval eval;
	eval.startedTick = m_sceneStartTick;
	eval.positionTicks = ctx.positionTicks;
	eval.ticksPerBar = ctx.ticksPerBar;
	eval.scene = m_activeScene;
	eval.sceneCount = plan->sceneCount;
	eval.rngUnit = followRandomUnit( m_followRng );
	const FollowFire fire = decideFollowFire( *plan, eval );
	// Reported at the tick it was scheduled for, then moved on by one step, so an
	// outcome that does nothing still consumes its action time (the cells' rule).
	const tick_t firedAt = m_sceneFollowNext;
	m_sceneFollowNext += step;
	if( fire.outcome == FollowOutcome::None ) { return; }
	m_followFires.fetch_add( 1, std::memory_order_relaxed );
	m_sceneFollowFires.fetch_add( 1, std::memory_order_relaxed );
	publishFollowFire( fire, firedAt );
	applySceneFire( fire, firedAt, ctx );
}


void SessionScheduler::applySceneFire( const FollowFire& fire, tick_t firedAt,
	const SessionClockContext& ctx ) noexcept
{
	if( fire.outcome == FollowOutcome::Refused ) { return; }
	const int row = m_activeScene;
	for( auto& slot : m_active )
	{
		if( slot.track < 0 || slot.scene != row || slot.state.phase != SlotPhase::Playing ) { continue; }
		if( fire.outcome == FollowOutcome::Stop )
		{
			m_recorder.recordStop( slot.track, slot.scene, firedAt );
			slot.state.phase = SlotPhase::Idle;
			slot.state.held = false;
			continue;
		}
		if( fire.outcome == FollowOutcome::SwitchScene )
		{
			m_recorder.recordStop( slot.track, slot.scene, firedAt );
			slot.scene = fire.targetScene;
			slot.state = SlotLaunchState{};
			slot.state.phase = SlotPhase::Playing;
			slot.state.startCount = 1;
			m_recorder.recordStart( slot.track, fire.targetScene, firedAt );
		}
		else
		{
			// PlayAgain: the same playback continues, so Arrangement Record records
			// nothing (the cells' Restart rule, SessionFollow.cpp).
			++slot.state.startCount;
		}
		slot.state.startedTick = firedAt;
		slot.followScheduled = false;
		slot.followNextTick = 0;
		m_launches.fetch_add( 1, std::memory_order_relaxed );
		publishStart( firedAt, ctx.positionTicks );
	}
	if( fire.outcome == FollowOutcome::Stop ) { startScene( -1, 0 ); }
	else { startScene( fire.outcome == FollowOutcome::SwitchScene ? fire.targetScene : row, firedAt ); }
}

// ---------------------------------------------------------------------------
// R5.3: the per-column reading the clip-launch grid draws from
// ---------------------------------------------------------------------------

namespace
{

//! 0 is idle; otherwise (phase << 16) | scene, scene < 65536 (the grid clamps at 512).
std::uint32_t packColumn( SlotPhase phase, int scene ) noexcept
{
	return phase == SlotPhase::Idle || scene < 0 ? 0u
		: ( static_cast<std::uint32_t>( phase ) << 16 ) | ( static_cast<std::uint32_t>( scene ) & 0xffffu );
}

} // namespace


void SessionScheduler::publishColumns() noexcept
{
	std::array<std::uint32_t, PublishedColumns> packed{};
	for( const auto& slot : m_active )
	{
		if( slot.track < 0 || slot.track >= PublishedColumns ) { continue; }
		const std::uint32_t value = packColumn( slot.state.phase, slot.scene );
		// A column can hold a playing cell and a pending one (a launch at the next bar):
		// the pending one is what the grid must show as about to happen, so it wins.
		if( value != 0 && ( packed[static_cast<std::size_t>( slot.track )] == 0
			|| slot.state.phase == SlotPhase::LaunchPending ) )
		{
			packed[static_cast<std::size_t>( slot.track )] = value;
		}
	}
	for( std::size_t column = 0; column < packed.size(); ++column )
	{
		m_columnStates[column].store( packed[column], std::memory_order_relaxed );
	}
}


SessionScheduler::ColumnState SessionScheduler::columnState( int track ) const noexcept
{
	ColumnState state;
	if( track < 0 || track >= PublishedColumns ) { return state; }
	const std::uint32_t value = m_columnStates[static_cast<std::size_t>( track )].load( std::memory_order_relaxed );
	if( value == 0 ) { return state; }
	state.phase = static_cast<SlotPhase>( value >> 16 );
	state.scene = static_cast<int>( value & 0xffffu );
	return state;
}

} // namespace lmms
