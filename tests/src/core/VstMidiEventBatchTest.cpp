/*
 * VstMidiEventBatchTest.cpp - the VST remote client's per-period MIDI helpers
 *                             do not allocate and keep their bounds
 *
 * The client itself is an out-of-process executable that cannot run inside a
 * test, so the two properties the client relies on are pinned here on the header
 * it includes (plugins/VstBase/VstMidiEventBatch.h): the sort is chronological,
 * stable and allocation-free, and a full queue drops the newest event and counts
 * it. A control case shows the probe does see the allocation std::stable_sort
 * makes, which is the defect the sort replaces.
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

#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../../../plugins/VstBase/VstMidiEventBatch.h"

#include "AllocationProbe.h"

namespace
{

struct Event
{
	int deltaFrames;
	int tag; // arrival index, to see stability
};

//! The allocations \a body makes on this thread, counted by the probe.
template<typename Body>
auto countAllocations(Body&& body) -> std::uint64_t
{
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	body();
	lmms::test::tlCountAllocations = false;
	return lmms::test::tlAllocationCount;
}

//! Deterministic, unsorted events with many equal deltas (so stability matters).
auto makeEvents(std::size_t count) -> std::vector<Event>
{
	std::vector<Event> events;
	events.reserve(count);
	std::uint32_t state = 12345u;
	for (std::size_t i = 0; i < count; ++i)
	{
		state = state * 1664525u + 1013904223u;
		events.push_back(Event{static_cast<int>((state >> 16) % 64u), static_cast<int>(i)});
	}
	return events;
}

} // namespace

class VstMidiEventBatchTest : public QObject
{
	Q_OBJECT

private slots:
	void sortIsChronologicalAndStable()
	{
		auto events = makeEvents(1024);
		auto reference = events;
		std::stable_sort(reference.begin(), reference.end(),
			[](const Event& a, const Event& b) { return a.deltaFrames < b.deltaFrames; });

		lmms::vst::sortByDeltaFrames(events.data(), events.size());

		for (std::size_t i = 0; i < events.size(); ++i)
		{
			QCOMPARE(events[i].deltaFrames, reference[i].deltaFrames);
			QCOMPARE(events[i].tag, reference[i].tag);
		}
	}

	void sortAllocatesNothing()
	{
		auto events = makeEvents(1024);

		const auto allocations = countAllocations([&] {
			lmms::vst::sortByDeltaFrames(events.data(), events.size());
		});
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! The defect the sort replaces: std::stable_sort allocates its merge buffer on every call.
	//! If the probe did not see this, sortAllocatesNothing would prove nothing.
	void controlStableSortAllocates()
	{
		auto events = makeEvents(1024);

		const auto allocations = countAllocations([&] {
			std::stable_sort(events.begin(), events.end(),
				[](const Event& a, const Event& b) { return a.deltaFrames < b.deltaFrames; });
		});
		QVERIFY2(allocations > 0, "the allocation probe did not see std::stable_sort's buffer");
	}

	void queueCapsAndCountsDrops()
	{
		constexpr std::size_t capacity = 8;
		std::vector<Event> queue;
		queue.reserve(capacity);
		std::size_t dropped = 0;

		const auto allocations = countAllocations([&] {
			for (int i = 0; i < 12; ++i)
			{
				lmms::vst::appendBounded(queue, Event{i, i}, capacity, dropped);
			}
		});

		QCOMPARE(queue.size(), capacity);
		QCOMPARE(dropped, std::size_t{4});
		// The newest events are the ones dropped: the first capacity arrivals are kept.
		QCOMPARE(queue.back().tag, static_cast<int>(capacity - 1));
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

QTEST_APPLESS_MAIN(VstMidiEventBatchTest)

#include "VstMidiEventBatchTest.moc"
