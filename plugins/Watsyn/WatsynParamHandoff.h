/*
 * WatsynParamHandoff.h - the real-time-safe hand-off of Watsyn's oscillator
 * parameters from the thread that edits them to the thread that renders them.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef WATSYN_PARAM_HANDOFF_H
#define WATSYN_PARAM_HANDOFF_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lmms
{

//! The audio thread must not take a lock to read a parameter.
static_assert( std::atomic<float>::is_always_lock_free,
	"a Watsyn parameter is read on the audio thread, so its storage must be lock free" );

/*!
 * The published set of Watsyn's oscillator parameters.
 *
 * The thread that owns the instrument computes the gains and the frequencies
 * of the oscillators and publishes them here; the audio thread takes the
 * published set once per rendered period.
 *
 * WHY NOT A PLAIN ARRAY. The upstream code kept the gains and the frequencies
 * in plain float arrays that the editing thread wrote and the audio thread
 * read, with nothing between them. Moving a knob while a note sounded was a
 * data race, and the audio thread could read a value that was being written.
 *
 * WHY AN IMMUTABLE SET AND NOT A COUNTER AROUND THE FIELDS. A set is written
 * once, by the editing thread, while it is still private to that thread; it
 * is published with a release store and it is never written again. A reader
 * takes the published pointer with an acquire load, which synchronises with
 * that store, so the reader sees a completely written set. Because the set is
 * never written again, the copy a reader takes can never be a mixture of two
 * publications - and because the pointer is always valid, a reader never
 * fails and never has to keep a set of its own. There is exactly one atomic
 * in the protocol (the pointer), so there is no ordering question between
 * the payload and a version counter: the payload and the pointer are one
 * object.
 *
 * THE ORDERING PROTOCOL, IN FULL.
 *
 *   editing thread                        audio thread
 *   ---------------                        -----------
 *   fresh->values = ...   (plain writes,
 *                          private to the
 *                          editing thread)
 *   m_published.exchange(fresh,
 *                        seq_cst)  ---->   m_published.load(seq_cst)
 *                                            synchronises with the exchange's
 *                                            release half, so every write
 *                                            above is visible to the reader
 *                                            (happens-before)
 *   old->pins--                          set->pins++    (the reader holds it)
 *   retire old                           copy set->values
 *                                         set->pins--
 *
 * The pointer exchange, the pointer load and the claimer counter are all
 * seq_cst, so they share ONE total order, and that order is what makes the
 * reclamation safe:
 *
 *   - If the exchange precedes the reader's load in that order, the load
 *     reads the NEW pointer. A load that is only an acquire may read a
 *     STALE pointer - a set the editing thread has already retired and may
 *     be filling again - and then the reader would copy a set that is being
 *     written under it. The seq_cst load cannot do that.
 *   - If the reader's claim precedes the editing thread's claimer check, the
 *     editing thread sees a claimer and recycles nothing while the reader
 *     is in the window.
 *
 * Between them the two cases cover every interleaving: a reader can never
 * observe a retired set, and a retired set is never filled again while a
 * reader could still observe it. This is the same protocol the wavetable
 * snapshot beside it uses (WatsynWave::claimWave and
 * WatsynInstrument::recycleWaves).
 */
template<int OscCount>
class WatsynParamHandoff
{
public:
	//! The gains and the frequencies of the oscillators, as one set.
	struct Values
	{
		float lvol[OscCount];
		float rvol[OscCount];
		float lfreq[OscCount];
		float rfreq[OscCount];
	};

	/*!
	 * One published set.
	 *
	 * It is written once, by the editing thread, while it is still private
	 * to that thread, and it is never written again. The pin count lives on
	 * its own cache line, because the readers raise it while the set's
	 * values are read.
	 */
	struct Set
	{
		alignas(64) std::atomic<int> pins{1};
		Values values;
	};

	/*!
	 * A test seam: the publishing thread calls it between the write of a
	 * set and its publication, so that a test can look at what a reader
	 * sees in that window. It is null in production, and the audio thread
	 * never calls it.
	 */
	using PublishHook = void (*)(void* context);

	/*!
	 * A test seam: the reading thread calls it inside the claim window -
	 * after it has observed the published set and before it pins it. That is
	 * the one window in which the editing thread must not recycle the set
	 * the reader is about to take, so a test can pause a reader there while
	 * the editing thread retires the observed set and tries to recycle it.
	 *
	 * Test builds only (the same guard the wavetable hand-off's sibling
	 * seams use): the shipped plugin does not contain this branch or these
	 * members at all, so the audio-thread read is unchanged in production.
	 */
#ifdef LMMS_TESTING
	using ClaimHook = void (*)(void* context, const void* observedSet);
#endif

	WatsynParamHandoff() = default;

	~WatsynParamHandoff()
	{
		// The notes are gone (the track silences them before it deletes the
		// instrument), so every set can be freed here.
		for ( auto* set : m_freeSets ) { delete set; }
		for ( auto* set : m_retiredSets ) { delete set; }
		delete m_published.exchange( nullptr );
	}

	//! Installs the test seam. The editing thread only.
	void setPublishHook( PublishHook hook, void* context ) noexcept
	{
		m_publishHook = hook;
		m_publishHookContext = context;
	}

	//! Installs the claim-window test seam. The reading thread that installs
	//! it only; it is set before any reader runs.
#ifdef LMMS_TESTING
	void setClaimHook( ClaimHook hook, void* context ) noexcept
	{
		m_claimHook = hook;
		m_claimHookContext = context;
	}
#endif

	//! Publishes a new set of values. The editing thread only.
	void publish( const Values& _values )
	{
		Set* fresh = nullptr;
		if ( m_freeSets.empty() )
		{
			fresh = new Set();
		}
		else
		{
			fresh = m_freeSets.back();
			m_freeSets.pop_back();
		}

		// The set is private to this thread until it is published, so these
		// writes are not visible to a reader yet.
		fresh->values = _values;
		fresh->pins.store( 1, std::memory_order_relaxed );

		// The seam opens between the write and the publication, so a test
		// can look at the state a reader sees in that window.
		if ( m_publishHook != nullptr ) { m_publishHook( m_publishHookContext ); }

		// The release half of the exchange publishes the written set: a
		// reader's load synchronises with it and sees the complete set.
		// The exchange is seq_cst because the reclamation depends on it and
		// the reader's load sharing one total order (see the note above).
		Set* old = m_published.exchange( fresh, std::memory_order_seq_cst );
		if ( old != nullptr )
		{
			// The instrument gives up its own reference. The set stays alive
			// until the readers that took it are done with it.
			old->pins.fetch_sub( 1, std::memory_order_acq_rel );
			m_retiredSets.push_back( old );
		}

		recycle();
	}

	/*!
	 * Takes the published set. The audio thread: no allocation, no lock, no
	 * waiting, and never a mixture of two publications.
	 */
	Values read() const noexcept
	{
		// The claim announces itself, so that the editing thread can tell
		// whether a reader is about to take the set it is about to retire:
		// the count is zero exactly when no reader is inside this window.
		m_claimers.fetch_add( 1, std::memory_order_seq_cst );
		// The load is seq_cst on purpose (see the note above): a load that
		// is only an acquire may read a STALE pointer, and a stale pointer
		// is a set the editing thread may already have retired and be
		// filling again.
		Set* set = m_published.load( std::memory_order_seq_cst );

		// The seam opens inside the claim window, after the reader has
		// observed the published set and before it pins it. Test builds
		// only: the shipped plugin has neither the hook nor this branch.
#ifdef LMMS_TESTING
		if ( m_claimHook != nullptr )
		{
			m_claimHook( m_claimHookContext, set );
		}
#endif

		set->pins.fetch_add( 1, std::memory_order_relaxed );
		m_claimers.fetch_sub( 1, std::memory_order_seq_cst );

		// The set is never written again, so this copy is one complete
		// publication.
		const Values values = set->values;

		set->pins.fetch_sub( 1, std::memory_order_release );
		return values;
	}

	/*!
	 * The address of the published set. A test seam: it lets a test see
	 * that a retired set was filled again, and that a reader took the set
	 * that is published and not one that was retired.
	 */
	const void* publishedSetIdentityForTesting() const noexcept
	{
		return m_published.load( std::memory_order_seq_cst );
	}

	//! True when the set may be filled again. A test seam: the editing
	//! thread only, and it is how a test sees that a set a reader is inside
	//! the claim window for was NOT put back into the free list.
	bool isFreeSetForTesting( const void* _set ) const noexcept
	{
		for ( const auto* set : m_freeSets )
		{
			if ( set == _set ) { return true; }
		}
		return false;
	}

	//! How many sets wait for their last holder. A test seam: the editing
	//! thread only.
	std::size_t retiredSetCountForTesting() const noexcept
	{
		return m_retiredSets.size();
	}

	//! The holder count of a retired set, or -1 when it is not retired. A
	//! test seam: the editing thread only. It is how a test shows that the
	//! only thing keeping a retired set out of the free list was a reader
	//! inside the claim window.
	int retiredSetPinsForTesting( const void* _set ) const noexcept
	{
		for ( const auto* set : m_retiredSets )
		{
			if ( set == _set ) { return set->pins.load( std::memory_order_acquire ); }
		}
		return -1;
	}

	//! Runs the editing thread's reclamation now. A test seam: it is how a
	//! test shows that a set a reader was inside the claim window for is
	//! reclaimed only after the reader leaves the window.
	void recycleForTesting() { recycle(); }

private:
	//! Moves the sets no reader holds any more back into the free list.
	//! The editing thread only.
	void recycle()
	{
		// A set that is about to be filled again must not be one a reader is
		// on its way to take: the claimer count has to be zero as well.
		if ( m_claimers.load( std::memory_order_seq_cst ) != 0 ) { return; }

		for ( auto it = m_retiredSets.begin(); it != m_retiredSets.end(); )
		{
			if ( ( *it )->pins.load( std::memory_order_acquire ) == 0 )
			{
				m_freeSets.push_back( *it );
				it = m_retiredSets.erase( it );
			}
			else
			{
				++it;
			}
		}
	}

	//! The published set. It is never null once the instrument has published
	//! its first set, which it does in its constructor.
	mutable std::atomic<Set*> m_published{nullptr};

	//! How many readers are inside the window in which a reader takes the
	//! published set. The editing thread reads it to learn that no reader
	//! is about to take the set it just retired.
	mutable std::atomic<int> m_claimers{0};

	//! Sets that may be filled again (editing thread only).
	std::vector<Set*> m_freeSets;
	//! Sets waiting for their last holder (editing thread only).
	std::vector<Set*> m_retiredSets;

	//! The test seam, null in production.
	PublishHook m_publishHook{nullptr};
	void* m_publishHookContext{nullptr};

#ifdef LMMS_TESTING
	//! The claim-window test seam. Test builds only.
	ClaimHook m_claimHook{nullptr};
	void* m_claimHookContext{nullptr};
#endif
};

} // namespace lmms

#endif // WATSYN_PARAM_HANDOFF_H
