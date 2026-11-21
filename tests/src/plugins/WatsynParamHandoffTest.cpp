/*
 * WatsynParamHandoffTest.cpp - the real-time-safe hand-off of Watsyn's
 * oscillator parameters.
 *
 * The hand-off is what keeps the editing thread and the audio thread apart:
 * the editing thread publishes an immutable set of gains and frequencies, the
 * audio thread takes the published set once per rendered period. The plain
 * float arrays the upstream code used were written by one thread and read by
 * the other with nothing between them - a data race.
 *
 * The regression is deterministic: the publication and the claim are staged
 * from the inside (two test seams in the hand-off - one between the write of a
 * set and its publication, one inside the claim window between observing the
 * published set and pinning it), so each reader sees exactly one thing, every
 * time. The independent-reader case scripts two threads with condition
 * variables: no clock, no sleep, no timing, and the publisher cannot run ahead
 * of the paused reader.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "../../../plugins/Watsyn/WatsynParamHandoff.h"

#include <QTest>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

using Handoff = lmms::WatsynParamHandoff<4>;
using Values = Handoff::Values;

namespace
{

//! What a reader that was taken inside a publication window saw.
struct PublicationWindowState
{
	const Handoff* handoff = nullptr;
	//! The set the reader took inside the window.
	Values seen{};
	bool readSucceeded = false;
	bool called = false;
};

/*!
 * A reader paused inside the claim window: it has already observed the
 * published set, but it has not pinned it yet. That is exactly the window in
 * which the editing thread must not recycle the observed set.
 *
 * The publisher waits for `inWindow`, retires the observed set (by
 * publishing another one) and then tries to recycle it. It records what
 * reclamation did while the reader was still in the window, and only then
 * sets `released` so the reader can finish.
 */
struct ClaimWindowState
{
	std::mutex mutex;
	std::condition_variable signal;
	//! The set the paused reader observed, or null if it never entered.
	const void* observed = nullptr;
	bool inWindow = false;
	bool released = false;
	//! What reclamation did while the reader was paused: true when the
	//! observed set had been put back in the free list.
	bool observedSetWasRecycled = false;
	//! How many sets were retired while the reader was paused.
	std::size_t retiredWhilePaused = 0;
	//! The retired set's holder count while the reader was paused.
	int pinsWhilePaused = -1;
	//! The published set while the reader was paused: it must not be the
	//! observed set, or the reader's buffer was refilled under it.
	const void* publishedWhilePaused = nullptr;
};

//! Takes the published set from inside the publication window. It runs on the
//! publishing thread, between the write of the new set and its publication.
void readInsideThePublicationWindow( void* _context )
{
	auto* state = static_cast<PublicationWindowState*>( _context );
	state->seen = state->handoff->read();
	state->readSucceeded = true;
	state->called = true;
}

//! Runs on the reading thread, inside the claim window: after the reader has
//! observed the published pointer and before it pins the set. It holds the
//! reader there until the publisher has retired the observed set and tried
//! to recycle it.
void pauseInsideTheClaimWindow( void* _context, const void* _observedSet )
{
	auto* state = static_cast<ClaimWindowState*>( _context );
	std::unique_lock lock{ state->mutex };
	state->observed = _observedSet;
	state->inWindow = true;
	state->signal.notify_all();
	state->signal.wait( lock, [state] { return state->released; } );
}

//! A set whose every value says which set it belongs to.
Values setOf( float _base )
{
	Values values{};
	for ( int i = 0; i < 4; ++i )
	{
		values.lvol[i] = _base + i;
		values.rvol[i] = _base + 10.0f + i;
		values.lfreq[i] = _base + 20.0f + i;
		values.rfreq[i] = _base + 30.0f + i;
	}
	return values;
}

//! True when both sets carry the same values.
bool sameSet( const Values& _left, const Values& _right )
{
	for ( int i = 0; i < 4; ++i )
	{
		if ( _left.lvol[i] != _right.lvol[i] || _left.rvol[i] != _right.rvol[i]
			|| _left.lfreq[i] != _right.lfreq[i] || _left.rfreq[i] != _right.rfreq[i] )
		{
			return false;
		}
	}
	return true;
}

}


class WatsynParamHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void aPublishedSetIsTakenAsItWasPublished();
	void aReaderDuringAPublicationGetsThePreviousCompleteSet();
	void aNoteThatStartsDuringAPublicationGetsACompleteSet();
	void anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive();
};

void WatsynParamHandoffTest::aPublishedSetIsTakenAsItWasPublished()
{
	Handoff handoff;
	const auto first = setOf( 1.0f );
	const auto second = setOf( 100.0f );

	handoff.publish( first );
	QVERIFY( sameSet( handoff.read(), first ) );

	handoff.publish( second );
	QVERIFY( sameSet( handoff.read(), second ) );
}


void WatsynParamHandoffTest::aReaderDuringAPublicationGetsThePreviousCompleteSet()
{
	Handoff handoff;
	const auto first = setOf( 1.0f );
	const auto second = setOf( 100.0f );
	handoff.publish( first );

	// The seam opens the publication window between the write of the new set
	// and its publication: the new set is written, but the published set is
	// still the old one.
	PublicationWindowState state;
	state.handoff = &handoff;
	handoff.setPublishHook( &readInsideThePublicationWindow, &state );
	handoff.publish( second );
	handoff.setPublishHook( nullptr, nullptr );

	QVERIFY2( state.called, "the publication window was not entered" );
	QVERIFY2( state.readSucceeded, "a reader inside a publication did not get a set at all" );

	// The reader got the previous set, complete: every one of its sixteen
	// values comes from one publication, never from two.
	QVERIFY2( sameSet( state.seen, first ),
		"a reader inside a publication did not get the previous complete set" );
	QVERIFY2( !sameSet( state.seen, second ),
		"a reader inside a publication got the set that was not published yet" );

	// After the publication the new set is complete and is taken as it was
	// published.
	QVERIFY( sameSet( handoff.read(), second ) );
}


void WatsynParamHandoffTest::aNoteThatStartsDuringAPublicationGetsACompleteSet()
{
	Handoff handoff;
	const auto first = setOf( 1.0f );
	const auto second = setOf( 100.0f );
	handoff.publish( first );

	// A note that starts while a publication is in progress takes the
	// published set - the same read the note's render does. It never gets a
	// mixture of two publications and it never gets nothing: the published
	// set is always there, whatever the editing thread is doing.
	PublicationWindowState state;
	state.handoff = &handoff;
	handoff.setPublishHook( &readInsideThePublicationWindow, &state );
	handoff.publish( second );
	handoff.setPublishHook( nullptr, nullptr );

	QVERIFY2( state.called, "the publication window was not entered" );
	QVERIFY2( state.readSucceeded, "a note that started during a publication got no set" );
	QVERIFY2( sameSet( state.seen, first ),
		"a note that started during a publication got a set that mixes two publications" );
}


/*!
 * The reader and the publisher are two independent threads, and the test
 * scripts their interleaving with condition variables - no sleep, no race,
 * no timing.
 *
 * The reader is paused INSIDE the claim window: it has already observed the
 * published set, but it has not pinned it yet. That is exactly the window the
 * reclamation protocol protects. The publisher then retires the observed set
 * (by publishing a newer one) and tries to recycle it, while the reader is
 * still in the window.
 *
 * What the regression proves:
 *   - the retired set is NOT put back into the free list while a reader is
 *     still inside the claim window for it (a reader that could observe a
 *     retired set would copy a set the editing thread is filling again);
 *   - the reader gets the set it observed, complete;
 *   - once the reader leaves the window, the set is safe to reuse: the next
 *     publication fills it again and says so through the published identity.
 */
void WatsynParamHandoffTest::anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive()
{
	Handoff handoff;
	const auto first = setOf( 1.0f );
	const auto second = setOf( 100.0f );

	// The first set is the one the reader will observe and the publisher will
	// retire while the reader is still in the claim window.
	handoff.publish( first );
	const void* firstSet = handoff.publishedSetIdentityForTesting();

	ClaimWindowState claim;
	handoff.setClaimHook( &pauseInsideTheClaimWindow, &claim );

	Values taken{};
	std::thread reader([&] { taken = handoff.read(); });

	// Wait until the reader is inside the claim window, on the observed set.
	{
		std::unique_lock lock{ claim.mutex };
		claim.signal.wait( lock, [&claim] { return claim.inWindow; } );
	}
	QVERIFY2( claim.observed == firstSet,
		"the paused reader observed a set other than the published one" );

	// The reader is paused with its observed set unpinned, so the pin count
	// alone would let the editing thread fill it again. The publisher retires
	// the observed set and publishes once more: with the claim window
	// honoured, the retired set is neither recycled nor refilled, and the
	// reader's copy stays the set it observed. With the window broken, the
	// retired set is reused here and the reader copies the newer values.
	handoff.publish( second );
	handoff.recycleForTesting();
	{
		std::unique_lock lock{ claim.mutex };
		claim.observedSetWasRecycled = handoff.isFreeSetForTesting( firstSet );
		claim.retiredWhilePaused = handoff.retiredSetCountForTesting();
		claim.pinsWhilePaused = handoff.retiredSetPinsForTesting( firstSet );
		claim.publishedWhilePaused = handoff.publishedSetIdentityForTesting();
		claim.released = true;
		claim.signal.notify_all();
	}

	reader.join();
	handoff.setClaimHook( nullptr, nullptr );

	QVERIFY2( !claim.observedSetWasRecycled,
		"the set a reader had observed inside the claim window was recycled while the reader "
		"could still copy it: the editing thread may fill it again under the reader" );
	QVERIFY2( claim.retiredWhilePaused >= 1,
		"the publisher did not retire the observed set while the reader was in the claim window, "
		"so the reclamation path was not exercised" );
	// The retired set had no holder left: the pin count alone would have let
	// the editing thread fill it again. It was the reader's claim that kept
	// it out of the free list, which is the window this test is about.
	QCOMPARE( claim.pinsWhilePaused, 0 );
	QVERIFY2( claim.publishedWhilePaused != firstSet,
		"the set a reader was inside the claim window for was refilled and published under it" );

	// The reader finished with the set it observed, complete - not with the
	// newer publication that could have reused its buffer.
	QVERIFY2( sameSet( taken, first ),
		"a reader paused inside the claim window did not get the set it observed" );

	// Released: now that no reader is inside the claim window for it, the
	// retired set is reclaimable, and the next publications reuse it. The
	// free list is LIFO, so the exact publication that takes it is not the
	// point - that it is taken and carries a complete set is.
	handoff.recycleForTesting();
	QVERIFY2( handoff.isFreeSetForTesting( firstSet ),
		"the retired set was not reclaimable after the reader left the claim window" );
	bool reused = false;
	Values reusedValues{};
	Values reusedPublication{};
	for ( int i = 0; i < 4 && !reused; ++i )
	{
		const auto published = setOf( 5000.0f + static_cast<float>( i ) );
		handoff.publish( published );
		if ( handoff.publishedSetIdentityForTesting() == firstSet )
		{
			reused = true;
			reusedPublication = published;
			reusedValues = handoff.read();
		}
	}
	QVERIFY2( reused,
		"the set the reader had observed was not safely reused after the reader left the window" );
	QVERIFY2( sameSet( reusedValues, reusedPublication ),
		"the reused set does not carry the complete publication that filled it" );
}


QTEST_MAIN( WatsynParamHandoffTest )

#include "WatsynParamHandoffTest.moc"
