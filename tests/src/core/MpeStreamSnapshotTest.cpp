/*
 * MpeStreamSnapshotTest.cpp - the realtime half of a note's MPE expression
 *                             stream: publication, copies and reclamation
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

// Q050-016 round 3. A note's stream is an immutable MpeStreamSnapshot behind an
// atomic pointer (include/MpeStreamSnapshot.h). The audio thread copies notes
// into NotePlayHandles while the UI/control thread edits them, so these slots
// pin, with absolute counts from the allocation probe (which counts frees too):
//   - a copy (the audio thread's operation) allocates 0 and frees 0;
//   - editing or clearing the note while a copy is alive leaves the copy's
//     stream exactly as it was (the snapshot is never changed in place);
//   - the final release of a stream on the realtime path frees nothing - the
//     snapshot is retired, and a later control-thread reclaim frees it;
//   - copies racing real edits on another thread never see a torn stream and,
//     on the copying thread, allocate 0 and free 0 across every iteration.
// The realtime windows are RealtimeScopes, so the hosted RTSan job reports any
// lock, allocation or free inside them as well.

#include <QtTest>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <thread>
#include <vector>

#include "MpeExpression.h"
#include "MpeStreamSnapshot.h"
#include "Note.h"
#include "RealtimeScope.h"

#include "AllocationProbe.h"

namespace
{

using lmms::MpeExpressionPoint;
using lmms::MpeExpressionStream;
using lmms::Note;
using lmms::tick_t;

//! A stream with all three axes non-empty, every value derived from \a generation,
//! so a copy can check that the three axes came from one publication.
MpeExpressionStream generationStream(int generation)
{
	const int pitch = generation % 4800;
	const int seven = pitch % 128;
	MpeExpressionStream stream;
	stream.setPitch({{0, pitch}, {1, pitch}});
	stream.setPressure({{0, seven}});
	stream.setTimbre({{0, seven}, {2, seven}});
	return stream;
}

//! True when \a note carries a stream whose axes all come from one generationStream().
bool isWholeGenerationStream(const Note& note)
{
	const MpeExpressionStream& stream = note.mpeStream();
	if (stream.pitch().size() != 2 || stream.pressure().size() != 1 || stream.timbre().size() != 2) { return false; }
	const int pitch = stream.pitch()[0].value;
	const int seven = pitch % 128;
	return stream.pitch()[1].value == pitch && stream.pressure()[0].value == seven
		&& stream.timbre()[0].value == seven && stream.timbre()[1].value == seven;
}

//! Arms the probe on this thread for its lifetime; the counts stay readable after.
struct ProbeWindow
{
	ProbeWindow()
	{
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
	}
	~ProbeWindow() { lmms::test::tlCountAllocations = false; }
	ProbeWindow(const ProbeWindow&) = delete;
	ProbeWindow& operator=(const ProbeWindow&) = delete;
};

} // namespace

class MpeStreamSnapshotTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		// Every slot starts with nothing retired, so its counts are its own.
		lmms::MpeStreamSnapshot::reclaimRetired();
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});
	}

	//! The audio thread's operation - copying a streamed note, assigning one,
	//! destroying a copy that is not the last reference - allocates 0 and
	//! frees 0, absolutely. The control is an edit, which must allocate.
	void copyingAStreamedNoteAllocatesAndFreesNothing()
	{
		const std::size_t live = lmms::MpeStreamSnapshot::liveCount();
		Note streamed(lmms::TimePos(384), lmms::TimePos(0), 60);
		streamed.setMpeStream(generationStream(1200));
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 1);

		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		bool copiesCarryIt = false;
		{
			const lmms::RealtimeScope realtime;
			const ProbeWindow probe;
			{
				Note copy(streamed);
				Note assigned;
				assigned = streamed;
				copiesCarryIt = copy.hasMpeStream() && assigned.hasMpeStream()
					&& &copy.mpeStream() == &streamed.mpeStream()
					&& &assigned.mpeStream() == &streamed.mpeStream();
			}
			allocations = lmms::test::tlAllocationCount;
			frees = lmms::test::tlFreeCount;
		}
		QVERIFY2(copiesCarryIt, "a copy does not share the note's snapshot");
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(frees, std::uint64_t{0});
		// Copies share; nothing new was created or retired.
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 1);
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});

		// Control: the probe is armed and sees the editing thread's allocation.
		std::uint64_t editAllocations = 0;
		{
			const ProbeWindow probe;
			streamed.setMpeStream(generationStream(600));
			editAllocations = lmms::test::tlAllocationCount;
		}
		QVERIFY2(editAllocations >= 4, "the probe did not see an edit's snapshot and three axes");
	}

	//! Editing and then clearing a note while a copy of it (a NotePlayHandle's
	//! stand-in) is alive: the copy keeps exactly the stream it took, the note
	//! sees each edit, and the replaced snapshots are freed - off the realtime
	//! path, so immediately - once nothing holds them.
	void editingOrClearingWhileACopyIsActiveLeavesTheCopyIntact()
	{
		const std::size_t live = lmms::MpeStreamSnapshot::liveCount();
		Note note(lmms::TimePos(384), lmms::TimePos(0), 60);
		note.setMpeStream(generationStream(100));

		auto* copy = new Note(note);
		const MpeExpressionStream* taken = &copy->mpeStream();

		note.setMpeStream(generationStream(2400));
		QVERIFY(&copy->mpeStream() == taken);
		QCOMPARE(copy->mpeStream().pitch()[0].value, 100);
		QCOMPARE(copy->mpeStream().pressure()[0].value, 100);
		QCOMPARE(note.mpeStream().pitch()[0].value, 2400);
		QCOMPARE(note.mpeStream().pressure()[0].value, 2400 % 128);
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 2);

		note.clearMpeStream();
		QVERIFY(!note.hasMpeStream());
		QVERIFY(copy->hasMpeStream());
		QVERIFY(isWholeGenerationStream(*copy));
		QCOMPARE(copy->mpeStream().pitch()[0].value, 100);
		// The 2400 snapshot had no other holder and this is not the realtime path.
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 1);
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});

		delete copy;
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live);
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});
	}

	//! The final reference released on the realtime path (a handle finishing
	//! after its note was edited): 0 frees and 0 allocations there, the
	//! snapshot is retired - still alive - and reclaimRetired() refuses to run
	//! inside the realtime scope. Off it, the reclaim frees the snapshot and
	//! its three axis buffers.
	void finalReleaseOnTheRealtimePathRetiresAndAControlThreadReclaimFrees()
	{
		const std::size_t live = lmms::MpeStreamSnapshot::liveCount();
		Note note(lmms::TimePos(384), lmms::TimePos(0), 60);
		note.setMpeStream(generationStream(700));

		alignas(Note) unsigned char storage[sizeof(Note)];
		Note* copy = new (storage) Note(note);
		note.clearMpeStream();  // the copy now holds the last reference
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 1);

		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		std::size_t reclaimedInScope = 1;
		{
			const lmms::RealtimeScope realtime;
			const ProbeWindow probe;
			copy->~Note();
			reclaimedInScope = lmms::MpeStreamSnapshot::reclaimRetired();
			allocations = lmms::test::tlAllocationCount;
			frees = lmms::test::tlFreeCount;
		}
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(frees, std::uint64_t{0});
		QCOMPARE(reclaimedInScope, std::size_t{0});
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{1});
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live + 1);

		std::size_t reclaimed = 0;
		std::uint64_t reclaimFrees = 0;
		{
			const ProbeWindow probe;
			reclaimed = lmms::MpeStreamSnapshot::reclaimRetired();
			reclaimFrees = lmms::test::tlFreeCount;
		}
		QCOMPARE(reclaimed, std::size_t{1});
		// The snapshot object and its pitch, pressure and timbre buffers.
		QCOMPARE(reclaimFrees, std::uint64_t{4});
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live);
	}

	//! Real concurrency: one thread publishes and clears streams as fast as it
	//! can (and reclaims), another copies the note inside a RealtimeScope - the
	//! audio thread's pattern. Every copy holds one whole publication (never a
	//! mix of two), and the copying thread allocates 0 and frees 0 in total.
	//! Without TSan this is evidence, not proof; the proof is the ordering
	//! argument in MpeStreamSnapshot.h.
	void copiesRacingEditsNeverTearAndNeverAllocateOrFree()
	{
		const std::size_t live = lmms::MpeStreamSnapshot::liveCount();
		Note note(lmms::TimePos(384), lmms::TimePos(0), 60);
		note.setMpeStream(generationStream(0));

		constexpr int Publications = 40000;
		std::atomic<bool> writerDone{false};
		std::atomic<bool> readerStarted{false};
		std::uint64_t readerAllocations = 0;
		std::uint64_t readerFrees = 0;
		std::uint64_t copies = 0;
		std::uint64_t streamedCopies = 0;
		std::uint64_t torn = 0;

		std::thread reader([&] {
			const lmms::RealtimeScope realtime;
			const ProbeWindow probe;
			readerStarted.store(true, std::memory_order_release);
			while (!writerDone.load(std::memory_order_acquire) || copies < 1000)
			{
				const Note copy(note);
				++copies;
				if (copy.hasMpeStream())
				{
					++streamedCopies;
					if (!isWholeGenerationStream(copy)) { ++torn; }
				}
			}
			readerAllocations = lmms::test::tlAllocationCount;
			readerFrees = lmms::test::tlFreeCount;
		});

		while (!readerStarted.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		for (int generation = 1; generation <= Publications; ++generation)
		{
			if (generation % 7 == 0) { note.clearMpeStream(); }
			else { note.setMpeStream(generationStream(generation)); }
			if (generation % 64 == 0) { lmms::MpeStreamSnapshot::reclaimRetired(); }
		}
		writerDone.store(true, std::memory_order_release);
		reader.join();

		std::fprintf(stdout, "MPE_EVIDENCE snapshot race copies=%llu streamed=%llu torn=%llu "
			"reader_allocations=%llu reader_frees=%llu retired_before_reclaim=%llu\n",
			static_cast<unsigned long long>(copies), static_cast<unsigned long long>(streamedCopies),
			static_cast<unsigned long long>(torn), static_cast<unsigned long long>(readerAllocations),
			static_cast<unsigned long long>(readerFrees),
			static_cast<unsigned long long>(lmms::MpeStreamSnapshot::retiredCount()));
		std::fflush(stdout);

		QCOMPARE(torn, std::uint64_t{0});
		QVERIFY2(streamedCopies > 0, "the reader never copied a streamed note - the race was not run");
		QCOMPARE(readerAllocations, std::uint64_t{0});
		QCOMPARE(readerFrees, std::uint64_t{0});

		note.clearMpeStream();
		lmms::MpeStreamSnapshot::reclaimRetired();
		QCOMPARE(lmms::MpeStreamSnapshot::retiredCount(), std::size_t{0});
		QCOMPARE(lmms::MpeStreamSnapshot::liveCount(), live);
	}
};

QTEST_GUILESS_MAIN(MpeStreamSnapshotTest)

#include "MpeStreamSnapshotTest.moc"
