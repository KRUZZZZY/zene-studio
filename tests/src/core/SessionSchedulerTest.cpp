/*
 * SessionSchedulerTest.cpp - Session View launch scheduler + quantisation
 *                             engine (task #595)
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

//! The launch decision is pure, so it is tested exhaustively here with no
//! audio state, no engine and no threads. The engine half is tested for the
//! two properties that are not visible from a single thread: commands cross
//! from the model thread to the audio thread without loss, and the audio-thread
//! path allocates nothing (AllocationProbe.h - the same gold-standard pattern
//! the recording and WASM lanes use).
//!
//! A scheduler you cannot test is a scheduler nobody can trust, so every rule
//! this file pins is a rule the engine actually implements:
//!   * quantisation set, rounding direction and on-boundary behaviour;
//!   * Trigger / Gate / Toggle / Repeat, each with a test that fails if the
//!     mode behaved like either of the modes it is closest to.

#include <QtTest>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "AllocationProbe.h"
#include "SessionModel.h"
#include "SessionScheduler.h"

using namespace lmms;

namespace
{

//! One bar of a 4/4 project: TimePos::DefaultTicksPerBar (include/TimePos.h:38).
constexpr tick_t kTicksPerBar = 192;
//! 4 ticks per period - the smallest useful clock step for a boundary test.
constexpr tick_t kTickStep = 4;
constexpr f_cnt_t kFramesPerPeriod = 16;
constexpr float kFramesPerTick = 4.0f;

SessionClockContext clock( tick_t ticks, tick_t ticksPerBar = kTicksPerBar )
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = ticksPerBar;
	ctx.framesPerTick = kFramesPerTick;
	ctx.transportRunning = true;
	return ctx;
}

//! Drives one slot the way the audio thread does: commands land whenever they
//! land, the clock then advances one period at a time.
struct SlotDriver
{
	SlotDriver( LaunchMode launchMode, LaunchQuantisation quant = LaunchQuantisation::Bar ) :
		mode( launchMode ), quantisation( quant )
	{
	}

	void send( LaunchCommandType type, tick_t atTicks )
	{
		applyLaunchCommand( state, mode, quantisation, type, clock( atTicks ) );
	}

	//! Advances from `from` to `to` inclusive, one period (4 ticks) at a time.
	void run( tick_t from, tick_t to )
	{
		for( tick_t position = from; position <= to; position += kTickStep )
		{
			const LaunchEvent event = advanceLaunchState( state, mode, clock( position ) );
			if( event != LaunchEvent::None )
			{
				events.push_back( event );
			}
		}
	}

	int countEvent( LaunchEvent event ) const
	{
		int count = 0;
		for( const LaunchEvent recorded : events )
		{
			if( recorded == event )
			{
				++count;
			}
		}
		return count;
	}

	LaunchMode mode;
	LaunchQuantisation quantisation;
	SlotLaunchState state;
	std::vector<LaunchEvent> events;
};

} // namespace


class SessionSchedulerTest : public QObject
{
	Q_OBJECT

private slots:
	// ---- quantisation (pure) -------------------------------------------

	//! The engine implements the set the data layer persists (SessionModel.h:70):
	//! None / 1 bar / 2 bars / 4 bars, with Global deferred to the session
	//! default. No other widths exist.
	void quantisationTicksCoversExactlyThePersistedSet()
	{
		QCOMPARE( quantisationTicks( LaunchQuantisation::None, kTicksPerBar ), tick_t( 0 ) );
		QCOMPARE( quantisationTicks( LaunchQuantisation::Bar, kTicksPerBar ), tick_t( 192 ) );
		QCOMPARE( quantisationTicks( LaunchQuantisation::TwoBars, kTicksPerBar ), tick_t( 384 ) );
		QCOMPARE( quantisationTicks( LaunchQuantisation::FourBars, kTicksPerBar ), tick_t( 768 ) );
		// Global is not a width: unresolved, it falls back to the session
		// default's own default of one bar (SessionModel.h:238).
		QCOMPARE( quantisationTicks( LaunchQuantisation::Global, kTicksPerBar ), tick_t( 192 ) );
		QCOMPARE( SessionModel::DefaultLaunchQuantisation, LaunchQuantisation::Bar );
	}

	void resolveQuantisationUsesTheSessionDefault()
	{
		QCOMPARE( resolveQuantisation( LaunchQuantisation::Global, LaunchQuantisation::FourBars ),
			LaunchQuantisation::FourBars );
		QCOMPARE( resolveQuantisation( LaunchQuantisation::Global, LaunchQuantisation::Global ),
			LaunchQuantisation::Bar );
		// A per-clip choice always wins over the session default.
		QCOMPARE( resolveQuantisation( LaunchQuantisation::None, LaunchQuantisation::FourBars ),
			LaunchQuantisation::None );
		QCOMPARE( resolveQuantisation( LaunchQuantisation::TwoBars, LaunchQuantisation::Bar ),
			LaunchQuantisation::TwoBars );
	}

	//! Rounding is "up to the next grid line", and a position already on a line
	//! launches at that line rather than one line later.
	void launchTickRoundsUpToTheGrid()
	{
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 0 ) ), tick_t( 0 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 1 ) ), tick_t( 192 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 191 ) ), tick_t( 192 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 192 ) ), tick_t( 192 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 193 ) ), tick_t( 384 ) );

		QCOMPARE( launchTickAt( LaunchQuantisation::TwoBars, clock( 100 ) ), tick_t( 384 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::TwoBars, clock( 384 ) ), tick_t( 384 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::TwoBars, clock( 385 ) ), tick_t( 768 ) );

		QCOMPARE( launchTickAt( LaunchQuantisation::FourBars, clock( 1 ) ), tick_t( 768 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::FourBars, clock( 768 ) ), tick_t( 768 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::FourBars, clock( 769 ) ), tick_t( 1536 ) );

		// None waits for no musical boundary at all.
		QCOMPARE( launchTickAt( LaunchQuantisation::None, clock( 500 ) ), tick_t( 500 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::None, clock( 0 ) ), tick_t( 0 ) );
	}

	//! The grid is the project's, not a hard-coded 4/4: a 7/8 bar is 168 ticks
	//! (TimePos::ticksPerBar(sig) = DefaultTicksPerBar * 7 / 8).
	void launchTickFollowsTheTimeSignature()
	{
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 100, 168 ) ), tick_t( 168 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 168, 168 ) ), tick_t( 168 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::TwoBars, clock( 169, 168 ) ), tick_t( 336 ) );
		// A degenerate signature must not divide by zero.
		QCOMPARE( quantisationTicks( LaunchQuantisation::Bar, 0 ), tick_t( 0 ) );
		QCOMPARE( launchTickAt( LaunchQuantisation::Bar, clock( 50, 0 ) ), tick_t( 50 ) );
	}

	// ---- LaunchMode semantics (pure) -----------------------------------

	//! Trigger: starts at the boundary and stays - a release is not a stop, and
	//! a second press re-launches from the next boundary.
	void triggerPlaysUntilStoppedAndIgnoresRelease()
	{
		SlotDriver slot( LaunchMode::Trigger );
		slot.send( LaunchCommandType::Press, 0 );
		slot.run( 0, 383 );
		QCOMPARE( slot.countEvent( LaunchEvent::Started ), 1 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Playing ) );

		// The release a Gate would stop on is ignored here.
		slot.send( LaunchCommandType::Release, 200 );
		slot.run( 384, 767 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Playing ) );
		QCOMPARE( slot.countEvent( LaunchEvent::Stopped ), 0 );

		// A second press re-launches at the next grid line.
		slot.send( LaunchCommandType::Press, 500 );
		slot.run( 768, 1151 );
		QCOMPARE( slot.countEvent( LaunchEvent::Started ), 2 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Playing ) );

		// Only an explicit stop takes it out.
		slot.send( LaunchCommandType::Stop, 1200 );
		slot.run( 1152, 1400 );   // the stop lands at the 1344 grid line
		QCOMPARE( slot.countEvent( LaunchEvent::Stopped ), 1 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Idle ) );
		QVERIFY( slot.state.startCount == 2u );
	}

	//! Toggle: the second press stops. The contrast case (same input, Trigger
	//! mode) is asserted inside the same test so the two cannot drift apart.
	void toggleSecondPressStopsWhileTriggerKeepsPlaying()
	{
		SlotDriver toggle( LaunchMode::Toggle );
		SlotDriver trigger( LaunchMode::Trigger );
		for( SlotDriver* slot : { &toggle, &trigger } )
		{
			slot->send( LaunchCommandType::Press, 0 );
			slot->run( 0, 191 );      // started at 0
			slot->send( LaunchCommandType::Release, 100 ); // ignored by both
			slot->send( LaunchCommandType::Press, 200 );   // the decisive press
			slot->run( 192, 575 );
		}
		QCOMPARE( int( toggle.state.phase ), int( SlotPhase::Idle ) );
		QCOMPARE( toggle.countEvent( LaunchEvent::Stopped ), 1 );
		QCOMPARE( int( trigger.state.phase ), int( SlotPhase::Playing ) );
		QCOMPARE( trigger.countEvent( LaunchEvent::Stopped ), 0 );
	}

	//! Toggle pressing again while a launch is pending cancels it; Trigger
	//! pressing again just re-arms. This is the "press again cancels" rule and
	//! it is what separates Toggle from Trigger before anything has played.
	void toggleSecondPressCancelsAPendingLaunch()
	{
		SlotDriver toggle( LaunchMode::Toggle );
		SlotDriver trigger( LaunchMode::Trigger );
		for( SlotDriver* slot : { &toggle, &trigger } )
		{
			slot->send( LaunchCommandType::Press, 10 );  // pending for tick 192
			slot->send( LaunchCommandType::Press, 20 );  // the decisive press
			slot->run( 0, 400 );
		}
		QCOMPARE( int( toggle.state.phase ), int( SlotPhase::Idle ) );
		QCOMPARE( toggle.countEvent( LaunchEvent::Started ), 0 );
		QCOMPARE( trigger.countEvent( LaunchEvent::Started ), 1 );
		QCOMPARE( int( trigger.state.phase ), int( SlotPhase::Playing ) );
	}

	//! Gate: plays only while the trigger is held, and a release that lands
	//! before the boundary means the clip never starts at all.
	void gatePlaysOnlyWhileHeld()
	{
		SlotDriver slot( LaunchMode::Gate );
		slot.send( LaunchCommandType::Press, 0 );
		slot.send( LaunchCommandType::Release, 50 );   // released before tick 192
		slot.run( 0, 767 );
		QCOMPARE( slot.countEvent( LaunchEvent::Started ), 0 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Idle ) );

		// Held across the boundary: one start, then the release stops it at the
		// next boundary.
		SlotDriver held( LaunchMode::Gate );
		held.send( LaunchCommandType::Press, 0 );
		held.run( 0, 191 );
		QCOMPARE( held.countEvent( LaunchEvent::Started ), 1 );
		held.send( LaunchCommandType::Release, 200 );
		held.run( 192, 575 );
		QCOMPARE( held.countEvent( LaunchEvent::Stopped ), 1 );
		QCOMPARE( int( held.state.phase ), int( SlotPhase::Idle ) );
		// Gate never re-triggers on its own while it is held.
		QCOMPARE( held.countEvent( LaunchEvent::Retriggered ), 0 );
		QVERIFY( held.state.startCount == 1u );
	}

	//! Repeat: while held it starts over at every boundary. Gate, driven with
	//! exactly the same commands, launches once - so this test fails if Repeat
	//! behaves like Gate.
	void repeatRetriggersOnEveryBoundaryWhileHeld()
	{
		SlotDriver repeat( LaunchMode::Repeat );
		SlotDriver gate( LaunchMode::Gate );
		for( SlotDriver* slot : { &repeat, &gate } )
		{
			slot->send( LaunchCommandType::Press, 0 );
			slot->run( 0, 767 );   // held for four bars
		}
		// Boundaries at 0, 192, 384, 576 -> started once + re-triggered 3x.
		QCOMPARE( repeat.countEvent( LaunchEvent::Started ), 1 );
		QCOMPARE( repeat.countEvent( LaunchEvent::Retriggered ), 3 );
		QVERIFY( repeat.state.startCount == 4u );

		QCOMPARE( gate.countEvent( LaunchEvent::Retriggered ), 0 );
		QVERIFY( gate.state.startCount == 1u );

		// Releasing stops it at the next boundary and it stays stopped.
		repeat.send( LaunchCommandType::Release, 800 );
		repeat.run( 768, 1343 );
		QCOMPARE( repeat.countEvent( LaunchEvent::Stopped ), 1 );
		QCOMPARE( int( repeat.state.phase ), int( SlotPhase::Idle ) );
		QVERIFY( repeat.state.startCount == 4u );
	}

	//! A Repeat slot launched with None quantisation has no grid to restart on,
	//! so it launches once and holds - documented, not accidental.
	void repeatWithoutAGridDoesNotRetrigger()
	{
		SlotDriver slot( LaunchMode::Repeat, LaunchQuantisation::None );
		slot.send( LaunchCommandType::Press, 37 );
		slot.run( 37, 1200 );
		QCOMPARE( slot.countEvent( LaunchEvent::Started ), 1 );
		QCOMPARE( slot.countEvent( LaunchEvent::Retriggered ), 0 );
	}

	//! None quantisation fires on the very next period instead of waiting for a
	//! bar line - the one setting whose whole point is not to wait.
	void noneQuantisationLaunchesImmediately()
	{
		SlotDriver slot( LaunchMode::Trigger, LaunchQuantisation::None );
		slot.send( LaunchCommandType::Press, 137 );
		slot.run( 137, 140 );
		QCOMPARE( slot.countEvent( LaunchEvent::Started ), 1 );
		QCOMPARE( int( slot.state.phase ), int( SlotPhase::Playing ) );
	}

	// ---- the engine: hand-off and realtime contract --------------------

	//! The whole point of the queue: a launch asked for on the model thread
	//! arrives on the audio thread, and nothing is lost.
	void engineLaunchesAcrossTheThreadBoundary()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.requestLaunch( 2, 3, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
		QCOMPARE( scheduler.activeSlotCount(), 0 );  // nothing until the audio thread runs
		QCOMPARE( int( scheduler.slotPhase( 2, 3 ) ), int( SlotPhase::Idle ) );

		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.slotPhase( 2, 3 ) ), int( SlotPhase::Playing ) );
		QVERIFY2( scheduler.completedLaunches() == 1u, "the launch did not reach the audio side" );
		QVERIFY( scheduler.trackIsSessionActive( 2 ) );
		QVERIFY( !scheduler.trackIsSessionActive( 3 ) );

		scheduler.requestStop( 2, 3, LaunchMode::Trigger, LaunchQuantisation::Bar );
		scheduler.processAudio( clock( 100 ), kFramesPerPeriod );
		QVERIFY( scheduler.trackIsSessionActive( 2 ) );  // stop is pending, not done
		scheduler.processAudio( clock( 192 ), kFramesPerPeriod );
		QVERIFY( !scheduler.trackIsSessionActive( 2 ) );
		QCOMPARE( scheduler.activeSlotCount(), 0 );
	}

	//! A model-thread reset (project change) drops everything on the next
	//! audio period, and is safe to call from anywhere.
	void resetDropsEveryLaunchedSlot()
	{
		SessionScheduler scheduler;
		scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar );
		scheduler.requestLaunch( 1, 0, LaunchMode::Trigger, LaunchQuantisation::Bar );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QCOMPARE( scheduler.activeSlotCount(), 2 );

		scheduler.reset();
		scheduler.processAudio( clock( 192 ), kFramesPerPeriod );
		QCOMPARE( scheduler.activeSlotCount(), 0 );
		QVERIFY( !scheduler.trackIsSessionActive( 0 ) );
	}

	//! The session clock is its own domain (SPEC A2): with the transport
	//! stopped it free-runs, so a launch can still reach its grid line.
	void sessionClockFreeRunsWhileTheTransportIsStopped()
	{
		SessionScheduler scheduler;
		SessionClockContext stopped = clock( 0 );
		stopped.transportRunning = false;

		scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar );
		// 192 ticks at 4 frames/tick = 768 frames; 48 periods of 16 frames.
		for( int period = 0; period < 47; ++period )
		{
			scheduler.processAudio( stopped, kFramesPerPeriod );
		}
		QCOMPARE( int( scheduler.slotPhase( 0, 0 ) ), int( SlotPhase::LaunchPending ) );
		scheduler.processAudio( stopped, kFramesPerPeriod );
		QCOMPARE( int( scheduler.slotPhase( 0, 0 ) ), int( SlotPhase::Playing ) );
		QCOMPARE( scheduler.positionTicks(), tick_t( 192 ) );
	}

	//! While the transport runs, the session clock follows it rather than
	//! counting on its own.
	void sessionClockFollowsTheTransport()
	{
		SessionScheduler scheduler;
		scheduler.processAudio( clock( 960 ), kFramesPerPeriod );
		QCOMPARE( scheduler.positionTicks(), tick_t( 960 ) );
	}

	//! The realtime contract, measured rather than asserted in prose: the
	//! audio-thread entry point allocates nothing, with launches in flight.
	void audioThreadPathDoesNotAllocate()
	{
		SessionScheduler scheduler;
		for( int i = 0; i < 8; ++i )
		{
			scheduler.requestLaunch( i, 0, LaunchMode::Trigger, LaunchQuantisation::Bar );
		}
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );

		SessionClockContext ctx = clock( 0 );
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for( int period = 0; period < 20000; ++period )
		{
			ctx.positionTicks = period * kTickStep;
			scheduler.processAudio( ctx, kFramesPerPeriod );
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2( allocations == 0,
			qPrintable( QStringLiteral( "audio-thread path allocated %1 times" )
				.arg( allocations ) ) );
		QCOMPARE( scheduler.activeSlotCount(), 8 );
	}

	//! ...and so does the model-side request path: queueing a launch must not
	//! allocate either, or the audio thread would inherit the allocator's lock.
	void controlThreadPathDoesNotAllocate()
	{
		SessionScheduler scheduler;
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for( int i = 0; i < 1000; ++i )
		{
			scheduler.requestLaunch( i % 8, i % 4, LaunchMode::Trigger, LaunchQuantisation::Bar );
			scheduler.requestRelease( i % 8, i % 4, LaunchMode::Gate, LaunchQuantisation::Bar );
			scheduler.requestStop( i % 8, i % 4, LaunchMode::Trigger, LaunchQuantisation::Bar );
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2( allocations == 0,
			qPrintable( QStringLiteral( "model-thread request path allocated %1 times" )
				.arg( allocations ) ) );
	}

	//! Bounded, not growing: a full queue refuses rather than allocating, and
	//! the refusal is counted.
	void aFullCommandQueueRefusesInsteadOfGrowing()
	{
		SessionScheduler scheduler;
		std::size_t accepted = 0;
		for( std::size_t i = 0; i < SessionScheduler::CommandQueueCapacity * 2; ++i )
		{
			if( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) )
			{
				++accepted;
			}
		}
		QCOMPARE( int( accepted ), int( SessionScheduler::CommandQueueCapacity ) );
		QVERIFY2( scheduler.droppedCommands() == SessionScheduler::CommandQueueCapacity,
			"a refused command was not counted as dropped" );

		// Draining makes room again.
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
	}

	/*! The producer/consumer contract with two real threads: the model thread
	 *  keeps pushing while the audio thread drains, nothing is lost, nothing is
	 *  dropped, and the audio thread still allocates nothing while it runs. */
	void commandsCrossThreadsWithoutLoss()
	{
		SessionScheduler scheduler;
		constexpr int kCommands = 4000;
		constexpr int kTracks = 32;

		std::atomic<bool> producerDone{ false };
		std::thread producer( [&] {
			for( int i = 0; i < kCommands; ++i )
			{
				while( !scheduler.requestLaunch( i % kTracks, 0, LaunchMode::Trigger,
					LaunchQuantisation::Bar ) )
				{
					std::this_thread::yield();
				}
			}
			producerDone.store( true );
		} );

		SessionClockContext ctx = clock( 0 );
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		int periods = 0;
		// Every command the model thread got into the queue must come out the
		// other side: the hand-off is the property under test here, not how
		// many of the presses survive as a launch (repeated presses on one
		// slot collapse into one scheduled start, which is correct).
		while( scheduler.processedCommands() < std::uint64_t( kCommands ) && periods < 200000 )
		{
			ctx.positionTicks += kTickStep;
			scheduler.processAudio( ctx, kFramesPerPeriod );
			++periods;
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		const std::uint64_t processed = scheduler.processedCommands();
		lmms::test::tlCountAllocations = false;
		producer.join();

		QVERIFY2( producerDone.load(), "producer stalled - the queue never drained" );
		QVERIFY2( processed == std::uint64_t( kCommands ),
			qPrintable( QStringLiteral( "audio thread consumed %1 of %2 accepted commands" )
				.arg( processed ).arg( kCommands ) ) );
		// Refusals are expected here: the producer is faster than the audio
		// period, so it hits the bounded queue and retries. What must hold is
		// that a refusal is *counted* and that nothing accepted is lost - the
		// assertion above is the loss half, this is the counting half.
		QVERIFY2( scheduler.droppedCommands() > 0u,
			"a full queue was never reported - the producer was not actually faster" );
		QVERIFY2( scheduler.completedLaunches() > 0u, "no launch ever completed" );
		QVERIFY2( allocations == 0,
			qPrintable( QStringLiteral( "audio thread allocated %1 times while draining" )
				.arg( allocations ) ) );
	}
};

QTEST_GUILESS_MAIN( SessionSchedulerTest )
#include "SessionSchedulerTest.moc"
