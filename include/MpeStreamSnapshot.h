/*
 * MpeStreamSnapshot.h - the immutable, reference-counted snapshot a note's
 *                       MPE expression stream is published through
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

#ifndef LMMS_MPE_STREAM_SNAPSHOT_H
#define LMMS_MPE_STREAM_SNAPSHOT_H

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "MpeExpression.h"

namespace lmms
{

/*! Q050-016 round 3: how a note's expression stream crosses from the editing
 *  thread to the audio thread.
 *
 *  A note (Note::m_mpeStream) holds an atomic pointer to one of these. A
 *  snapshot is never changed after create(): an edit builds a NEW snapshot on
 *  the editing (UI/control) thread and swaps the note's pointer to it. Copying
 *  a note - which is how the audio thread builds a NotePlayHandle - takes a
 *  counted reference with acquire(). Every operation the audio thread can
 *  reach (acquire, release, the stream reads) is a few lock-free atomic
 *  operations: no allocation, no free, no lock, no wait.
 *
 *  Reclamation. The last release() frees the snapshot only when it runs off
 *  the realtime path (RealtimeScope inactive) and no acquire() is in flight;
 *  otherwise the snapshot is pushed onto an intrusive lock-free retire list
 *  (no allocation: the link lives in the snapshot) and reclaimRetired() frees
 *  it later on a non-realtime thread - the audio engine's control-thread
 *  timer, the stream command after an edit, and the engine's destructor.
 *  A NotePlayHandle always retires (Note::retireMpeStreamDeferred), so the
 *  handle's final release on the realtime teardown path never frees.
 *
 *  Memory ordering - why a copy can never touch a freed snapshot:
 *   1. acquire() raises s_copiesInFlight (seq_cst) BEFORE it loads the slot
 *      (seq_cst), and only then takes a reference with an increment that
 *      refuses a count of zero, so a snapshot whose count reached zero is
 *      never revived. It lowers the counter (release) after.
 *   2. A snapshot's count reaches zero only after every slot that held it has
 *      swapped it out (each slot owns one reference, and its swap - seq_cst
 *      exchange - is sequenced before its release, whose acq_rel decrement
 *      continues the release sequence to the final one). So that swap
 *      happens-before the freeing thread's seq_cst read of s_copiesInFlight.
 *   3. If that read sees 0, any acquire() that loaded this snapshot had
 *      already lowered the counter (its decrement is in the value read, so its
 *      reference-count reads happen-before the free), and any acquire() that
 *      raises the counter later is after the swap in the single seq_cst
 *      order, so its slot load returns the new pointer, not this one. If the
 *      read sees a copy in flight, the snapshot is retired instead and
 *      reclaimRetired() repeats the same check before freeing.
 *  The pointer publication is the seq_cst exchange; its release half makes
 *  the snapshot's contents (written by create() before publication) visible
 *  to every thread whose acquire-or-stronger load returns the pointer.
 *
 *  Both atomic types are required to be lock-free on every target
 *  (static_assert in MpeStreamSnapshot.cpp), so none of this can fall back to
 *  the library's lock table. */
class MpeStreamSnapshot
{
public:
	MpeStreamSnapshot(const MpeStreamSnapshot&) = delete;
	MpeStreamSnapshot& operator=(const MpeStreamSnapshot&) = delete;

	//! A snapshot of \a stream holding one reference (the caller's). This
	//! allocates: editing threads only, never the audio thread.
	static const MpeStreamSnapshot* create(const MpeExpressionStream& stream);

	/*! A counted reference to the snapshot \a slot holds now, or nullptr.
	 *  Lock-free and allocation-free - the audio thread's note copy runs it.
	 *  A slot that is empty when first read touches no shared counter, so a
	 *  note without a stream costs one atomic load. */
	static const MpeStreamSnapshot* acquire(const std::atomic<const MpeStreamSnapshot*>& slot) noexcept;

	/*! Drop one reference (nullptr is ignored). The last one frees the
	 *  snapshot here only when \a deferred is false, the calling thread is not
	 *  in a RealtimeScope, and no acquire() is in flight; otherwise it is
	 *  retired for reclaimRetired(). Never frees on the realtime path. */
	static void release(const MpeStreamSnapshot* snapshot, bool deferred) noexcept;

	/*! Free the retired snapshots that no copy can still reach. Does nothing
	 *  on the realtime path (a RealtimeScope is active) or while an acquire()
	 *  is in flight - the list is kept for the next call. Returns how many it
	 *  freed. */
	static std::size_t reclaimRetired() noexcept;

	//! Snapshots retired and not yet reclaimed (for tests and diagnostics).
	static std::size_t retiredCount() noexcept;
	//! Snapshots alive now, retired ones included (for tests and diagnostics).
	static std::size_t liveCount() noexcept;

	const MpeExpressionStream& stream() const { return m_stream; }

private:
	explicit MpeStreamSnapshot(const MpeExpressionStream& stream);
	~MpeStreamSnapshot();

	//! Increment the count unless it is zero; false means the snapshot is
	//! already on its way to reclamation and must not be used.
	bool tryAddReference() const noexcept;
	static void retire(const MpeStreamSnapshot* snapshot) noexcept;
	static void destroy(const MpeStreamSnapshot* snapshot) noexcept;

	mutable std::atomic<std::uint32_t> m_refs{1};
	//! The retire list's link: written only while the snapshot is unreachable.
	mutable const MpeStreamSnapshot* m_nextRetired = nullptr;
	const MpeExpressionStream m_stream;
};

} // namespace lmms

#endif // LMMS_MPE_STREAM_SNAPSHOT_H
