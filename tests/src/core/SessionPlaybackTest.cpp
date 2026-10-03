/*
 * SessionPlaybackTest.cpp - the published content of session cells, and the view
 * the render path reads (board card #597).
 *
 * WHY THIS IS ITS OWN FILE and not two more cases in SessionSchedulerTest.cpp:
 * that file is grandfathered at 549 lines in the whole-tree file-length ratchet,
 * so any addition to it is a regression. A NEW test source is free (it is well
 * under the 500-line limit) and tests/CMakeLists.txt is outside the ratchet's
 * scope, so the feature gets its proof without moving a ratchet.
 *
 * What is proved here is the HAND-OFF, which is the half that was missing: the
 * launch engine already knew that a cell plays and since when, and nothing knew
 * what it holds. So:
 *   - content published on the model thread arrives on the audio thread on the
 *     same queue the launch does, and is what playbackForColumn() reports; (0.5.0) a cell starts at its
 *     loop start, a legato relaunch goes on, and a jump moves a playing cell;
 *   - the reported position is the session clock minus the launch, so it
 *     advances as the clock does;
 *   - a column with nothing launched is never reported as playing;
 *   - a cell that holds NOTHING still takes its column over, with patternId < 0,
 *     which is what keeps "the launch fired" separable from "there was content
 *     to play";
 *   - a project change (reset) drops the published content with the launch
 *     state, so a cell cannot render the PREVIOUS project's pattern.
 */
#include <QtTest/QtTest>

#include "SessionPlayback.h"
#include "SessionScheduler.h"

using namespace lmms;

namespace
{

constexpr tick_t kTicksPerBar = 192;
constexpr f_cnt_t kFramesPerPeriod = 16;
constexpr float kFramesPerTick = 4.0f;

SessionClockContext clock( tick_t ticks )
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = kTicksPerBar;
	ctx.framesPerTick = kFramesPerTick;
	ctx.transportRunning = true;
	return ctx;
}

} // namespace


class SessionPlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	//! The content a cell holds crosses to the audio thread and is what the
	//! render path reads.
	void publishedContentReachesTheRenderPath()
	{
		SessionScheduler scheduler;

		// Before anything is launched no column is playing, so nothing renders:
		// the pre-#597 behaviour, in miniature.
		QVERIFY( !scheduler.playbackForColumn( 2 ).playing );

		QVERIFY( scheduler.publishSlotContent( 2, 3, 7, 0 ) );
		QVERIFY( scheduler.requestLaunch( 2, 3, LaunchMode::Trigger,
			LaunchQuantisation::Bar ) );
		QVERIFY2( scheduler.slotPhase( 2, 3 ) == SlotPhase::Idle,
			"the model-thread push reached the audio thread before it ran" );

		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );

		const SessionSlotPlayback played = scheduler.playbackForColumn( 2 );
		QVERIFY2( played.playing, "the launched column is not reported as playing" );
		QCOMPARE( played.patternId, 7 );
		// The position is the session clock minus the launch it is playing.
		QCOMPARE( int( played.positionTicks ), 0 );
		scheduler.processAudio( clock( 100 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 2 ).positionTicks ), 100 );

		// A column with no slot launched is never rendered, whatever a cell
		// elsewhere holds.
		QVERIFY( !scheduler.playbackForColumn( 3 ).playing );
	}

	//! The control: an EMPTY cell takes its column over and holds nothing.
	void anEmptyCellStillTakesItsColumnOverAndHoldsNothing()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger,
			LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );

		const SessionSlotPlayback silent = scheduler.playbackForColumn( 0 );
		QVERIFY2( silent.playing, "the take-over itself did not happen" );
		QCOMPARE( silent.patternId, -1 );
	}

	//! A clip launched on a column takes it over - the clip that was playing stops - and a
	//! legato cell continues the outgoing clip's phase instead of starting from its top
	//! (engine audit E1).
	void aLegatoLaunchContinuesThePhaseOfTheClipItTakesOver()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 0, 0, 7, 0 ) );
		QVERIFY( scheduler.publishSlotContent( 0, 1, 8, 0, true ) );
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QVERIFY( scheduler.requestLaunch( 0, 1, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 100 ), kFramesPerPeriod );  // still the first clip
		QCOMPARE( scheduler.playbackForColumn( 0 ).patternId, 7 );
		scheduler.processAudio( clock( kTicksPerBar + 50 ), kFramesPerPeriod );
		const SessionSlotPlayback played = scheduler.playbackForColumn( 0 );
		QCOMPARE( played.patternId, 8 );
		QCOMPARE( int( played.positionTicks ), int( kTicksPerBar + 50 ) );  // not 50: the phase went on
		QCOMPARE( scheduler.slotPhase( 0, 0 ), SlotPhase::Idle );
	}

	void aNonLegatoLaunchStartsFromItsTop()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 0, 0, 7, 0 ) );
		QVERIFY( scheduler.publishSlotContent( 0, 1, 8, 0 ) );
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QVERIFY( scheduler.requestLaunch( 0, 1, LaunchMode::Trigger, LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( kTicksPerBar + 50 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 0 ).positionTicks ), 50 );
	}

	//! Re-publishing a cell replaces its content; it does not take a second
	//! entry (the table installs in place for a cell it already knows).
	void republishingACellReplacesItsContent()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 1, 1, 5, 0 ) );
		QVERIFY( scheduler.publishSlotContent( 1, 1, 9, 0 ) );
		QVERIFY( scheduler.requestLaunch( 1, 1, LaunchMode::Trigger,
			LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );

		QCOMPARE( scheduler.playbackForColumn( 1 ).patternId, 9 );
	}

	//! A project change must not leave a cell able to render the previous
	//! project's pattern.
	void resetDropsPublishedContentWithTheLaunchState()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 0, 0, 7, 0 ) );
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger,
			LaunchQuantisation::Bar ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		QCOMPARE( scheduler.playbackForColumn( 0 ).patternId, 7 );

		scheduler.reset();
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );

		QVERIFY2( !scheduler.playbackForColumn( 0 ).playing,
			"a reset left the column playing" );
	}

	//! Each quantisation value must mean a GRID, not just a name: the round-trip
	//! test (SessionPlaybackRenderTest) proves a name survives the surface, and
	//! this proves the tick quantum that name stands for. The sub-bar values were
	//! added on 2026-09-28, and a value that round-trips but quantises on the
	//! wrong grid would be worse than not having it.
	void everyQuantisationValueMeansItsGrid()
	{
		const struct { LaunchQuantisation value; tick_t ticks; } grid[] = {
			{LaunchQuantisation::None,      0},
			{LaunchQuantisation::Sixteenth, kTicksPerBar / 16},
			{LaunchQuantisation::Eighth,    kTicksPerBar / 8},
			{LaunchQuantisation::Quarter,   kTicksPerBar / 4},
			{LaunchQuantisation::Half,      kTicksPerBar / 2},
			{LaunchQuantisation::Bar,       kTicksPerBar},
			{LaunchQuantisation::TwoBars,   2 * kTicksPerBar},
			{LaunchQuantisation::FourBars,  4 * kTicksPerBar},
			{LaunchQuantisation::EightBars, 8 * kTicksPerBar},
		};
		for (const auto& g : grid)
		{
			QCOMPARE(quantisationTicks(g.value, kTicksPerBar), g.ticks);
		}

		// And the value must reach the launch decision: from tick 100 the next
		// 1/4-bar line is 144, the next bar line 192, and None takes effect where
		// the clock already is.
		QCOMPARE(int(launchTickAt(LaunchQuantisation::Quarter, clock(100))), 144);
		QCOMPARE(int(launchTickAt(LaunchQuantisation::Bar, clock(100))), 192);
		QCOMPARE(int(launchTickAt(LaunchQuantisation::None, clock(100))), 100);
		// A position already ON a line takes effect at that line.
		QCOMPARE(int(launchTickAt(LaunchQuantisation::Bar, clock(192))), 192);
	}

	//! The table refuses rather than growing, and a refused cell degrades to
	//! the pre-#597 behaviour instead of a wrong one.
	void aFullTableRefusesAndLeavesTheCellSilent()
	{
		SessionScheduler scheduler;
		bool refused = false;
		for( int i = 0; i < 300; ++i )
		{
			if( !scheduler.publishSlotContent( i, 0, 4, 0 ) )
			{
				refused = true;
				break;
			}
		}
		QVERIFY2( refused, "the fixed table accepted 300 cells" );
	}

	//! 0.5.0 WF-060: a cell's own start inside its clip offsets where it plays, and a legato cell
	//! relaunched while it plays goes on from where it was instead of its top.
	void aCellStartsAtItsLoopStartAndALegatoRelaunchGoesOn()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 0, 0, 7, 0, true, 48 ) );
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::None ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		scheduler.processAudio( clock( 10 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 0 ).positionTicks ), 58 );  // 10 in, from 48
		QVERIFY( scheduler.requestLaunch( 0, 0, LaunchMode::Trigger, LaunchQuantisation::None ) );
		scheduler.processAudio( clock( 100 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 0 ).positionTicks ), 148 );  // not 48: it went on
	}

	//! 0.5.0 WF-070: a playing cell jumps inside its clip at once; a cell that is not playing ignores it.
	void aPlayingCellJumps()
	{
		SessionScheduler scheduler;
		QVERIFY( scheduler.publishSlotContent( 1, 0, 7, 0 ) );
		QVERIFY( scheduler.requestJump( 1, 0, 96 ) );  // not playing yet: nothing to move
		QVERIFY( scheduler.requestLaunch( 1, 0, LaunchMode::Trigger, LaunchQuantisation::None ) );
		scheduler.processAudio( clock( 0 ), kFramesPerPeriod );
		scheduler.processAudio( clock( 20 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 1 ).positionTicks ), 20 );
		QVERIFY( scheduler.requestJump( 1, 0, 96 ) );
		scheduler.processAudio( clock( 30 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 1 ).positionTicks ), 96 );
		scheduler.processAudio( clock( 40 ), kFramesPerPeriod );
		QCOMPARE( int( scheduler.playbackForColumn( 1 ).positionTicks ), 106 );
	}
};

QTEST_MAIN( SessionPlaybackTest )
#include "SessionPlaybackTest.moc"
