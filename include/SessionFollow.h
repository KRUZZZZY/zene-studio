/*
 * SessionFollow.h - Follow Action evaluation for the Session View's engine
 *                   (SPEC-zene-studio A3, SPEC §4.1; board task #641, the
 *                   #596 half).
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

/* WHY THIS FILE EXISTS. `FollowAction` has been part of the session data layer
 * since #594 (include/SessionModel.h:80) and is persisted per slot
 * (SessionClip.cpp's `followactions` element), but NOTHING evaluated it: the
 * chain round-tripped through save/reload and no launch ever consulted it.
 * `docs/FEATURE-LIST-0.3.0.md` row 1 names that as one of the two gaps the
 * 0.3.0 scope contract still owed. This is the evaluation.
 *
 * THE SHAPE, and why it is a header. The decision is a PURE FUNCTION over a
 * fixed-size plan and an evaluation window: no globals, no threads, no audio
 * state, no allocation - so a test drives every one of the ten action types
 * exhaustively, and the one place that calls it (SessionScheduler, on the audio
 * thread) adds nothing but the clock and an RNG. Inline in the header because
 * there is no state to encapsulate: the storage lives in the caller's
 * `ActiveSlot` (see SessionScheduler.h), which is what keeps an evaluation
 * allocation-free on the audio path (AGENTS.md rule 4).
 *
 * WHERE IT RUNS. SPEC A3 asks for evaluation "by a Scheduler on the UI-side
 * engine clock ... delivered to the audio path via the existing lock-free
 * command-queue pattern". This engine takes the other half of that same
 * pattern and says so plainly: the CHAIN is delivered from the model thread
 * through the existing SPSC command queue (`LaunchCommandType::Follow`, a POD
 * payload - no allocation, no second queue), and the EVALUATION happens where
 * the launch state already lives, on the audio thread, against the same
 * SessionClockContext the launches use. Reason: a follow action's whole content
 * is "this slot's own playback state, one clock step later" - the state it
 * reads and writes is the audio thread's, and moving it to the model thread
 * would need that state published and a second thread's worth of ordering to
 * argue about, for a decision that is quantise-and-fire, not sample-critical.
 *
 * WHAT A FOLLOW ACTION DOES HERE, in the tree's own terms: it changes which
 * CELL of one track column is playing, or stops it, or restarts it. It does not
 * render audio - there is no session-clip playback path in 0.3.0 (#597) - so
 * "Previous/Next/Jump" resolve to the cell that the engine takes the track over
 * with. That is exactly the observable the launch engine already has
 * (`trackIsSessionActive`, SessionScheduler.h), and the fire is read back
 * through `session.follow_get_state`.
 */

#ifndef LMMS_SESSION_FOLLOW_H
#define LMMS_SESSION_FOLLOW_H

#include <cstdint>

#include "LmmsTypes.h"
#include "SessionModel.h"

namespace lmms
{

//! Entries the engine carries per slot. Live's own model is an A/B pair; the
//! data layer allows a chain, so the engine's bound is the serialised one's
//! practical maximum and a longer chain is refused by the command, not truncated.
constexpr int MaxFollowChainEntries = 8;

/*! Cells whose plan the engine will hold at once (arming is per cell, and a
 *  plan outlives the playback it was installed for - see SessionScheduler.h's
 *  InstalledFollowPlan). A cell armed beyond this bound is refused and counted,
 *  never allocated for. */
constexpr int MaxFollowPlans = 16;

/*! The evaluation plan of ONE cell: everything the audio thread needs, as a POD
 *  it can be handed through the command queue. `enabled` is the arm switch -
 *  a plan installed with it false is a plan that does nothing, which is how a
 *  cell is disarmed without a second command type. */
struct FollowPlan
{
	bool enabled = false;
	int count = 0;
	FollowAction entries[MaxFollowChainEntries];
	/*! Ticks of the clip's own length, for Linked timing (SPEC §4.1: "Linked
	 *  timing = the action fires after the clip length"). 0 means the cell has
	 *  no length the model knows about, and Linked falls back to one bar -
	 *  never to 0, which would fire on every audio period. */
	tick_t clipLengthTicks = 0;
	/*! The grid's height, published with the plan because an action addresses a
	 *  SCENE ROW and the audio thread is handed no grid object: Previous/Next
	 *  wrap around this number and a Jump outside it is refused. */
	int sceneCount = 0;
	/*! The launch mode and quantisation the switched-to cell is played with.
	 *  Filled from the cell the plan was armed on, so a column that follows
	 *  from one clip to the next keeps the behaviour the client set up rather
	 *  than falling back to a default the client never chose. */
	LaunchMode launchMode = LaunchMode::Trigger;
	LaunchQuantisation quantisation = LaunchQuantisation::Bar;
};

//! What a fire does to the slot that owns the chain.
enum class FollowOutcome : std::uint8_t
{
	None = 0,     //!< no entry selected / the chain has nothing to do
	Stop,         //!< playback ends at the action time
	Restart,      //!< the same cell starts over at the action time (PlayAgain)
	SwitchScene,  //!< the track column moves to `targetScene` and plays it
	Refused       //!< an entry was selected but its target is not addressable
	              //!< (Jump outside the grid): it fires ONCE and does nothing,
	              //!< which is a measurement rather than a silent every-period retry.
};

//! The decision. `chosenIndex` is the chain entry that produced it (-1 when
//! nothing was selected), so a caller can report WHICH action fired and not
//! only what it did.
struct FollowFire
{
	FollowOutcome outcome = FollowOutcome::None;
	int targetScene = -1;
	int chosenIndex = -1;
};

/*! Everything the pure decision reads. `rngUnit` is the caller's random draw in
 *  [0, 1) - the decision never calls a random source itself, so a test pins the
 *  chance weighting by supplying the number. */
struct FollowEval
{
	tick_t startedTick = 0;
	tick_t positionTicks = 0;
	tick_t ticksPerBar = 0;
	int scene = 0;
	int sceneCount = 0;
	double rngUnit = 0.0;
};

/*! Ticks between the slot's start and the chain's action time.
 *
 *  The chain's TIMING is its FIRST entry's, and the model is explicit about why:
 *  `linked`/`timeBars` are fields of an entry, but the fire time has to be known
 *  BEFORE an entry is selected (the selection is what the fire time schedules),
 *  so a chain whose entries disagree on timing uses the first entry's - the only
 *  rule under which "Linked" means the one thing it means. Live's own UI models
 *  a single timing per group for the same reason.
 *
 *  Linked -> the clip's own length, falling back to one bar when the cell has no
 *  model length; Unlinked -> the first entry's `timeBars`, rounded to the nearest
 *  tick and never zero. 0 is returned only for a chain that cannot fire at all
 *  (disabled, empty, or no clock), which the caller reads as "nothing to do". */
inline tick_t followActionTicks( const FollowPlan& plan, tick_t ticksPerBar ) noexcept
{
	if( !plan.enabled || plan.count <= 0 )
	{
		return 0;
	}
	const FollowAction& first = plan.entries[0];
	if( first.linked )
	{
		return plan.clipLengthTicks > 0 ? plan.clipLengthTicks : ticksPerBar;
	}
	const double ticks = first.timeBars * static_cast<double>( ticksPerBar );
	return ticks > 0.5 ? static_cast<tick_t>( ticks + 0.5 ) : 0;
}

/*! Index of the chain entry a draw selects, by Chance A/B weighting (SPEC §4.1).
 *
 *  The weights are the entries' own `chance` fields, normalised over the chain -
 *  so a single entry with chance 1 is the plain "always" action, and two entries
 *  at 0.25/0.75 behave as Live's A/B pair. A chain whose weights sum to nothing
 *  (every entry at chance 0) selects the first entry: an action chain that can
 *  never fire would be indistinguishable from a broken one. */
inline int pickFollowIndex( const FollowPlan& plan, double rngUnit ) noexcept
{
	if( !plan.enabled || plan.count <= 0 )
	{
		return -1;
	}
	double total = 0.0;
	for( int index = 0; index < plan.count; ++index )
	{
		const double weight = plan.entries[index].chance;
		total += weight > 0.0 ? weight : 0.0;
	}
	if( total <= 0.0 )
	{
		return 0;
	}
	double unit = rngUnit;
	if( unit < 0.0 ) { unit = 0.0; }
	if( unit >= 1.0 ) { unit = 0.9999999999999999; }
	double cursor = unit * total;
	for( int index = 0; index < plan.count; ++index )
	{
		const double weight = plan.entries[index].chance > 0.0 ? plan.entries[index].chance : 0.0;
		cursor -= weight;
		if( cursor < 0.0 || index == plan.count - 1 )
		{
			return index;
		}
	}
	return plan.count - 1;
}

/*! The scene row an action addresses in the slot's own track COLUMN. -1 means
 *  the action has no addressable target and the fire is refused.
 *
 *  The rules, each pinned by a test in SessionFollowTest.cpp:
 *   - Previous / Next WRAP around the scene list, which is Live's behaviour and
 *     the only one under which a column loops its own scenes;
 *   - First / Last clamp to the ends;
 *   - Any is uniform over the whole list; Other is uniform over the list MINUS
 *     the current scene (and has no answer at all on a one-scene grid);
 *   - Jump uses the entry's own `jumpTo`, and an out-of-range row is refused
 *     rather than clamped: silently playing a different scene than the one the
 *     chain names would be a wrong note, not a recovery. */
inline int followTargetScene( FollowAction::Type type, int scene, int sceneCount,
	int jumpTo, double rngUnit ) noexcept
{
	if( sceneCount <= 0 )
	{
		return -1;
	}
	int current = scene < 0 ? 0 : ( scene >= sceneCount ? sceneCount - 1 : scene );
	//! One draw, clamped into [0,1); the callers below scale it.
	double unit = rngUnit;
	if( unit < 0.0 ) { unit = 0.0; }
	if( unit >= 1.0 ) { unit = 0.9999999999999999; }

	switch( type )
	{
		case FollowAction::Type::Previous:
			return ( current + sceneCount - 1 ) % sceneCount;
		case FollowAction::Type::Next:
			return ( current + 1 ) % sceneCount;
		case FollowAction::Type::First:
			return 0;
		case FollowAction::Type::Last:
			return sceneCount - 1;
		case FollowAction::Type::Any:
			return static_cast<int>( unit * static_cast<double>( sceneCount ) );
		case FollowAction::Type::Other:
			if( sceneCount < 2 ) { return -1; }
			{
				const int offset = static_cast<int>( unit * static_cast<double>( sceneCount - 1 ) );
				const int candidate = ( current + 1 + offset ) % sceneCount;
				return candidate == current ? ( current + 1 ) % sceneCount : candidate;
			}
		case FollowAction::Type::Jump:
			return jumpTo >= 0 && jumpTo < sceneCount ? jumpTo : -1;
		default:
			break;
	}
	return -1;
}

/*! THE DECISION. Pure: reads `plan` and `eval`, writes nothing.
 *
 *  The caller owns the *when*: it fires this only once per action time (see
 *  SessionScheduler::evaluateFollow, which advances the slot's next action tick
 *  by one step per call), so a returned `None` means "this action time passed
 *  with nothing to do", not "call me again next period". */
inline FollowFire decideFollowFire( const FollowPlan& plan, const FollowEval& eval ) noexcept
{
	FollowFire fire;
	if( !plan.enabled || plan.count <= 0 )
	{
		return fire;
	}
	const tick_t due = eval.startedTick + followActionTicks( plan, eval.ticksPerBar );
	if( eval.positionTicks < due )
	{
		// Not due yet: the chain is scheduled, not fired.
		return fire;
	}
	const int index = pickFollowIndex( plan, eval.rngUnit );
	if( index < 0 )
	{
		return fire;
	}
	fire.chosenIndex = index;
	const FollowAction& action = plan.entries[index];
	switch( action.type )
	{
		case FollowAction::Type::NoAction:
			fire.outcome = FollowOutcome::None;
			return fire;
		case FollowAction::Type::Stop:
			fire.outcome = FollowOutcome::Stop;
			return fire;
		case FollowAction::Type::PlayAgain:
			fire.outcome = FollowOutcome::Restart;
			return fire;
		default:
			break;
	}
	const int target = followTargetScene( action.type, eval.scene, eval.sceneCount,
		action.jumpTo, eval.rngUnit );
	if( target < 0 )
	{
		fire.outcome = FollowOutcome::Refused;
		return fire;
	}
	fire.outcome = FollowOutcome::SwitchScene;
	fire.targetScene = target;
	return fire;
}

/*! xorshift32: the audio thread's random source. Deliberately NOT
 *  std::rand / std::mt19937 - neither may be used on an audio path, and a
 *  deterministic, seedable, allocation-free sequence is also what makes the
 *  chance weighting testable. */
inline std::uint32_t followNextRandom( std::uint32_t& state ) noexcept
{
	if( state == 0 )
	{
		state = 0x9e3779b9u;
	}
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

//! One draw in [0, 1) from the same generator, for FollowEval::rngUnit.
inline double followRandomUnit( std::uint32_t& state ) noexcept
{
	return static_cast<double>( followNextRandom( state ) ) / 4294967296.0;
}

// ---------------------------------------------------------------------------
// the published fire, packed for a model-thread reader
// ---------------------------------------------------------------------------

/*! A fire is published as ONE 64-bit word so a reader can never pair a new
 *  outcome with the previous tick: the same argument publishStart() makes for
 *  the start line (SessionScheduler.h). The tick is the action time the fire was
 *  scheduled for, not the period that noticed it. */
inline std::uint64_t packFollowFire( FollowOutcome outcome, int index, int targetScene,
	tick_t tick ) noexcept
{
	const std::uint64_t a = static_cast<std::uint64_t>( static_cast<std::uint32_t>( outcome ) & 0xfu );
	const std::uint64_t b = static_cast<std::uint64_t>( static_cast<std::uint32_t>( index + 1 ) & 0xffu );
	const std::uint64_t c = static_cast<std::uint64_t>( static_cast<std::uint32_t>( targetScene + 1 ) & 0xffffu );
	const std::uint64_t d = static_cast<std::uint64_t>( static_cast<std::uint32_t>( tick ) );
	return ( d << 32 ) | ( c << 12 ) | ( b << 4 ) | a;
}

inline FollowOutcome followFireOutcome( std::uint64_t packed ) noexcept
{
	return static_cast<FollowOutcome>( packed & 0xfu );
}

inline int followFireIndex( std::uint64_t packed ) noexcept
{
	return static_cast<int>( ( packed >> 4 ) & 0xffu ) - 1;
}

inline int followFireTargetScene( std::uint64_t packed ) noexcept
{
	return static_cast<int>( ( packed >> 12 ) & 0xffffu ) - 1;
}

inline tick_t followFireTick( std::uint64_t packed ) noexcept
{
	return static_cast<tick_t>( static_cast<std::uint32_t>( packed >> 32 ) );
}

//! Wire name of an outcome, for the read-back commands and their transcripts.
inline const char* followOutcomeName( FollowOutcome outcome ) noexcept
{
	switch( outcome )
	{
		case FollowOutcome::Stop: return "stop";
		case FollowOutcome::Restart: return "restart";
		case FollowOutcome::SwitchScene: return "switch_scene";
		case FollowOutcome::Refused: return "refused";
		case FollowOutcome::None: break;
	}
	return "none";
}

} // namespace lmms

#endif // LMMS_SESSION_FOLLOW_H
