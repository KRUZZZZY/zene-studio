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

#include <QFile>

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

	//! cluster new-p17-148: a period that cannot fetch audio must silence the
	//! caller's output planes, not hand back the caller's stale samples.
	void silenceZeroesOutputPlanes()
	{
		constexpr std::size_t frames = 96;
		std::vector<float> out(3 * frames, 7.0f);
		// Stale samples: without silencing the caller would hear the previous period.
		QVERIFY(out[0] != 0.0f);

		const auto allocations = countAllocations([&] {
			lmms::vst::silenceOutputPlanes(out.data(), 3, frames);
		});

		for (const auto s : out)
		{
			QCOMPARE(s, 0.0f);
		}
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! A cached count of zero, a null buffer, or zero frames must touch nothing;
	//! a silent zeroing of unbacked planes would corrupt the input block next to it.
	void silenceIgnoresDegenerateInputs()
	{
		constexpr std::size_t frames = 16;
		std::vector<float> out(frames, 5.0f);
		lmms::vst::silenceOutputPlanes(out.data(), 0, frames);
		QCOMPARE(out[0], 5.0f);
		lmms::vst::silenceOutputPlanes(out.data(), -1, frames);
		QCOMPARE(out[0], 5.0f);
		lmms::vst::silenceOutputPlanes(out.data(), 1, 0);
		QCOMPARE(out[0], 5.0f);
		lmms::vst::silenceOutputPlanes(nullptr, 2, frames);
		// Only the requested planes are silenced; the rest of the block is untouched.
		std::vector<float> withTail(4 * frames, 9.0f);
		lmms::vst::silenceOutputPlanes(withTail.data(), 2, frames);
		QCOMPARE(withTail[0], 0.0f);
		QCOMPARE(withTail[2 * frames], 9.0f);
	}

	//! The integration check for new-p17-148: the early returns in
	//! RemoteVstPlugin::process() must call the silence helper. The client
	//! cannot run in-process, so this pins that both paths clear the output
	//! block (the helper above is what they call).
	void processEarlyReturnsSilenceOutputs()
	{
		QFile f(QStringLiteral("plugins/VstBase/RemoteVstPlugin.cpp"));
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		QVERIFY2(f.exists(), "RemoteVstPlugin.cpp not found from the test working directory");
		QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
		const auto text = QString::fromUtf8(f.readAll());
		// isolate process(): from its signature to the next top-level method.
		const auto begin = text.indexOf(QStringLiteral("void RemoteVstPlugin::process"));
		QVERIFY(begin >= 0);
		const auto end = text.indexOf(QStringLiteral("void RemoteVstPlugin::processMidiEvent"), begin);
		QVERIFY(end > begin);
		const auto body = text.mid(begin, end - begin);
		// Both early returns (tryLockShm, isShmValid) must silence with the cached count.
		QVERIFY2(body.count(QStringLiteral("silenceOutputPlanes")) >= 2,
			"process() must silence outputs on both early returns");
		QVERIFY2(body.count(QStringLiteral("RemotePluginClient::outputCount")) >= 2,
			"silencing must use the cached output count, not the live plugin count");
	}

	//! cluster 3434: bind the tested allocation-free sort to the client's period path.
	void processUsesAllocationFreeMidiSort()
	{
		QFile f(QStringLiteral("plugins/VstBase/RemoteVstPlugin.cpp"));
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		QVERIFY2(f.exists(), "RemoteVstPlugin.cpp not found from the test working directory");
		QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
		const auto text = QString::fromUtf8(f.readAll());
		const auto begin = text.indexOf(QStringLiteral("void RemoteVstPlugin::process"));
		QVERIFY(begin >= 0);
		const auto end = text.indexOf(QStringLiteral("void RemoteVstPlugin::processMidiEvent"), begin);
		QVERIFY(end > begin);
		const auto body = text.mid(begin, end - begin);
		QVERIFY2(body.contains(QStringLiteral("vst::sortByDeltaFrames")),
			"process() must use the tested allocation-free MIDI sort");
		QVERIFY2(!body.contains(QStringLiteral("stable_sort")),
			"process() must not allocate a stable_sort buffer per period");
	}

	//! cluster new-p14-240: the dispatch scratch must be per-instance, not a
	//! function-local static shared by every RemoteVstPlugin instance.
	void midiScratchIsPerInstance()
	{
		// Two per-instance scratch buffers never alias, so parallel processing
		// cannot make one plugin's dispatch overwrite the other's events.
		struct Scratch
		{
			char eventsBuffer[64];
			Event scratch[8];
		};
		Scratch a{};
		Scratch b{};
		QVERIFY(+a.eventsBuffer != +b.eventsBuffer);
		QVERIFY(&a.scratch[0] != &b.scratch[0]);
		a.scratch[0].tag = 99;
		a.eventsBuffer[0] = 1;
		QVERIFY(b.scratch[0].tag != 99);
		QVERIFY(b.eventsBuffer[0] != 1);

		// The product check: process() must not declare shared static scratch.
		QFile f(QStringLiteral("plugins/VstBase/RemoteVstPlugin.cpp"));
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		if (!f.exists())
		{
			f.setFileName(QStringLiteral("../../../plugins/VstBase/RemoteVstPlugin.cpp"));
		}
		QVERIFY2(f.exists(), "RemoteVstPlugin.cpp not found from the test working directory");
		QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
		const auto text = QString::fromUtf8(f.readAll());
		const auto begin = text.indexOf(QStringLiteral("void RemoteVstPlugin::process"));
		QVERIFY(begin >= 0);
		const auto end = text.indexOf(QStringLiteral("void RemoteVstPlugin::processMidiEvent"), begin);
		QVERIFY(end > begin);
		const auto body = text.mid(begin, end - begin);
		QVERIFY2(!body.contains(QStringLiteral("static char eventsBuffer")),
			"process() must not use a shared static events buffer");
		QVERIFY2(!body.contains(QStringLiteral("static VstMidiEvent vme")),
			"process() must not use shared static MIDI scratch");
	}
};

QTEST_APPLESS_MAIN(VstMidiEventBatchTest)

#include "VstMidiEventBatchTest.moc"
