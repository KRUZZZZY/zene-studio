/*
 * CarlaMidiHandoffTest.cpp - the Carla render boundary stages ordered MIDI without waiting or allocating
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "CarlaMidiHandoff.h"
#include "../core/AllocationProbe.h"

namespace
{

constexpr std::uint32_t RenderFrames = 32;
constexpr std::size_t MidiCapacity = 512;

#if CARLA_VERSION_HEX >= 0x030000
using CarlaInputBufferList = const float**;
#else
using CarlaInputBufferList = float**;
#endif

//! Compile-time stand-in for Carla's NativePluginDescriptor::process callback.
struct FakeCarlaPlugin
{
	std::array<NativeMidiEvent, 8> seen{};
	std::uint32_t seenCount = 0;
	std::uint32_t processCalls = 0;

	static void process(NativePluginHandle handle, CarlaInputBufferList, float** output, std::uint32_t frames,
		const NativeMidiEvent* events, std::uint32_t eventCount)
	{
		auto& plugin = *static_cast<FakeCarlaPlugin*>(handle);
		++plugin.processCalls;
		plugin.seenCount = eventCount;
		for (std::uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
		{
			plugin.seen[eventIndex] = events[eventIndex];
		}

		bool noteActive = false;
		std::uint32_t nextEvent = 0;
		for (std::uint32_t frame = 0; frame < frames; ++frame)
		{
			while (nextEvent < eventCount && events[nextEvent].time <= frame)
			{
				const std::uint8_t status = events[nextEvent].data[0] & 0xf0;
				if (status == lmms::MidiNoteOn) { noteActive = events[nextEvent].data[2] != 0; }
				if (status == lmms::MidiNoteOff) { noteActive = false; }
				++nextEvent;
			}
			const float sample = noteActive ? 0.5f : 0.0f;
			output[0][frame] = sample;
			output[1][frame] = sample;
		}
	}
};

struct StressProbe
{
	std::array<std::uint32_t, 4> lastIndex{};
	std::array<std::uint64_t, 4> received{};
	std::array<bool, 4> hasLast{};
	bool ordered = true;

	static void process(NativePluginHandle handle, CarlaInputBufferList, float**, std::uint32_t,
		const NativeMidiEvent* events, std::uint32_t eventCount)
	{
		auto& probe = *static_cast<StressProbe*>(handle);
		for (std::uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
		{
			const NativeMidiEvent& event = events[eventIndex];
			if (event.size != 3 || (event.data[0] & 0xf0) != lmms::MidiNoteOn || event.data[2] == 0) { continue; }
			const auto channel = static_cast<std::size_t>(event.data[0] & 0x0f);
			if (channel >= probe.received.size()) { probe.ordered = false; continue; }
			const std::uint32_t index = event.data[1] + (static_cast<std::uint32_t>(event.data[2] - 1) << 7);
			if (probe.hasLast[channel] && index <= probe.lastIndex[channel]) { probe.ordered = false; }
			probe.lastIndex[channel] = index;
			probe.hasLast[channel] = true;
			++probe.received[channel];
		}
	}
};

struct ConcurrentStressRun
{
	static constexpr std::uint32_t ProducerCount = 4;
	static constexpr std::uint32_t EventsPerProducer = 5000;

	lmms::CarlaMidiHandoff<MidiCapacity> handoff;
	StressProbe probe;
	NativePluginDescriptor descriptor{};
	std::array<std::atomic<std::uint64_t>, ProducerCount> accepted{};
	std::array<std::atomic<std::uint64_t>, ProducerCount> producerAllocations{};
	std::array<std::atomic<std::uint64_t>, ProducerCount> refused{};
	std::array<std::atomic<std::uint32_t>, ProducerCount> maxPushAttempts{};
	std::atomic<std::uint32_t> finished{0};
	std::atomic<std::uint32_t> activeProducers{0};
	std::atomic<bool> renderStarted{false};
	std::atomic<std::uint64_t> periodsWithProducerInput{0};
	std::vector<std::thread> producers;
	std::array<float, 1> inputSample{};
	std::array<float, 1> outputSample{};
#if CARLA_VERSION_HEX >= 0x030000
	std::array<const float*, 2> input{inputSample.data(), inputSample.data()};
#else
	std::array<float*, 2> input{inputSample.data(), inputSample.data()};
#endif
	std::array<float*, 2> output{outputSample.data(), outputSample.data()};
	std::uint32_t periods = 0;
	std::uint32_t drainPeriods = 0;
	std::uint64_t totalAccepted = 0;
	std::uint64_t totalReceived = 0;

	ConcurrentStressRun()
	{
		descriptor.process = &StressProbe::process;
	}

	void startProducers()
	{
		producers.reserve(ProducerCount);
		for (std::uint32_t producer = 0; producer < ProducerCount; ++producer)
		{
			producers.emplace_back([this, producer] { produce(producer); });
		}
	}

	void produce(std::uint32_t producer)
	{
		while (!renderStarted.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		activeProducers.fetch_add(1, std::memory_order_release);
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		std::uint64_t refusedCount = 0;
		std::uint32_t greatestAttemptCount = 0;
		const auto noQueueInterference = [](std::uint32_t) noexcept {};
		const auto noStampInterference = [](std::atomic<std::uint64_t>&, std::uint64_t, std::uint64_t,
			std::uint32_t) noexcept {};
		for (std::uint32_t index = 0; index < EventsPerProducer; ++index)
		{
			const auto key = static_cast<std::int16_t>(index & 0x7f);
			const auto velocity = static_cast<std::int16_t>(1 + (index >> 7));
			std::uint32_t attempts = 0;
			if (handoff.enqueueWithTestHooks(lmms::MidiEvent(lmms::MidiNoteOn,
				static_cast<std::int8_t>(producer), key, velocity), 0, &attempts, nullptr,
				noQueueInterference, noStampInterference))
			{
				accepted[producer].fetch_add(1, std::memory_order_relaxed);
			}
			else { ++refusedCount; }
			if (attempts > greatestAttemptCount) { greatestAttemptCount = attempts; }
		}
		lmms::test::tlCountAllocations = false;
		producerAllocations[producer].store(lmms::test::tlAllocationCount, std::memory_order_relaxed);
		refused[producer].store(refusedCount, std::memory_order_relaxed);
		maxPushAttempts[producer].store(greatestAttemptCount, std::memory_order_relaxed);
		activeProducers.fetch_sub(1, std::memory_order_release);
		finished.fetch_add(1, std::memory_order_release);
	}

	void renderPeriod()
	{
		if (activeProducers.load(std::memory_order_acquire) != 0)
		{
			periodsWithProducerInput.fetch_add(1, std::memory_order_relaxed);
		}
		handoff.processPeriod([this](const NativeMidiEvent* events, std::uint32_t eventCount)
		{
			descriptor.process(&probe, input.data(), output.data(), 1, events, eventCount);
			renderStarted.store(true, std::memory_order_release);
		});
	}

	void renderWhileProducersRun()
	{
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		while (periods < 100000 && finished.load(std::memory_order_acquire) != ProducerCount)
		{
			renderPeriod();
			++periods;
		}
		lmms::test::tlCountAllocations = false;
	}

	void joinProducers()
	{
		for (auto& producer : producers) { producer.join(); }
	}

	auto acceptedCount() const -> std::uint64_t
	{
		std::uint64_t total = 0;
		for (const auto& count : accepted) { total += count.load(std::memory_order_relaxed); }
		return total;
	}

	auto refusedCount() const -> std::uint64_t
	{
		std::uint64_t total = 0;
		for (const auto& count : refused) { total += count.load(std::memory_order_relaxed); }
		return total;
	}

	auto greatestPushAttemptCount() const -> std::uint32_t
	{
		std::uint32_t greatest = 0;
		for (const auto& count : maxPushAttempts)
		{
			const auto observed = count.load(std::memory_order_relaxed);
			if (observed > greatest) { greatest = observed; }
		}
		return greatest;
	}

	auto receivedCount() const -> std::uint64_t
	{
		std::uint64_t total = 0;
		for (const auto count : probe.received) { total += count; }
		return total;
	}

	void drainAcceptedEvents()
	{
		totalAccepted = acceptedCount();
		totalReceived = receivedCount();
		lmms::test::tlCountAllocations = true;
		while (totalReceived < totalAccepted && drainPeriods < 100)
		{
			renderPeriod();
			++drainPeriods;
			totalReceived = receivedCount();
		}
		lmms::test::tlCountAllocations = false;
	}

	auto producerResultsMatch() const -> bool
	{
		for (std::uint32_t producer = 0; producer < ProducerCount; ++producer)
		{
			if (probe.received[producer] != accepted[producer].load(std::memory_order_relaxed)) { return false; }
		}
		return true;
	}

	auto producersAllocatedNothing() const -> bool
	{
		for (const auto& allocations : producerAllocations)
		{
			if (allocations.load(std::memory_order_relaxed) != 0) { return false; }
		}
		return true;
	}
};

} // namespace

class CarlaMidiHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void nativeProcessReceivesOrderedEventsAndRendersTheirOffsets()
	{
		lmms::CarlaMidiHandoff<8> handoff;
		FakeCarlaPlugin plugin;
		NativePluginDescriptor descriptor{};
		descriptor.process = &FakeCarlaPlugin::process;
		std::array<float, RenderFrames> inputLeft{};
		std::array<float, RenderFrames> inputRight{};
		std::array<float, RenderFrames> outputLeft{};
		std::array<float, RenderFrames> outputRight{};
#if CARLA_VERSION_HEX >= 0x030000
		const float* input[] = {inputLeft.data(), inputRight.data()};
#else
		float* input[] = {inputLeft.data(), inputRight.data()};
#endif
		float* output[] = {outputLeft.data(), outputRight.data()};

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		Q_UNUSED(handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOn, 2, 60, 100), 3));
		Q_UNUSED(handoff.enqueue(lmms::MidiEvent(lmms::MidiControlChange, 2, 1, 32), 8));
		Q_UNUSED(handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOff, 2, 60, 0), 17));
		handoff.processPeriod([&](const NativeMidiEvent* events, std::uint32_t eventCount)
		{
			descriptor.process(&plugin, input, output, RenderFrames, events, eventCount);
		});
		lmms::test::tlCountAllocations = false;

		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(plugin.processCalls, std::uint32_t{1});
		QCOMPARE(plugin.seenCount, std::uint32_t{3});
		QCOMPARE(plugin.seen[0].time, std::uint32_t{3});
		QCOMPARE(plugin.seen[0].size, std::uint8_t{3});
		QCOMPARE(plugin.seen[0].data[0], std::uint8_t{0x92});
		QCOMPARE(plugin.seen[0].data[1], std::uint8_t{60});
		QCOMPARE(plugin.seen[0].data[2], std::uint8_t{100});
		QCOMPARE(plugin.seen[1].time, std::uint32_t{8});
		QCOMPARE(plugin.seen[1].data[0], std::uint8_t{0xb2});
		QCOMPARE(plugin.seen[1].data[1], std::uint8_t{1});
		QCOMPARE(plugin.seen[1].data[2], std::uint8_t{32});
		QCOMPARE(plugin.seen[2].time, std::uint32_t{17});
		QCOMPARE(plugin.seen[2].data[0], std::uint8_t{0x82});
		for (std::uint32_t frame = 0; frame < RenderFrames; ++frame)
		{
			const float expected = frame >= 3 && frame < 17 ? 0.5f : 0.0f;
			QCOMPARE(outputLeft[frame], expected);
			QCOMPARE(outputRight[frame], expected);
		}
	}

	void retryExhaustionThroughEnqueueBoundsOverflowAccounting()
	{
		constexpr std::size_t Capacity = 8;
		lmms::CarlaMidiHandoff<Capacity> handoff;
		std::uint32_t competingReservations = 0;
		std::uint32_t injectedStampConflicts = 0;
		const auto reserveBeforeCompareExchange = [&](std::uint32_t)
		{
			if (handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOn, 5, 60, 90), 0)) { ++competingReservations; }
		};
		const auto contendDuringStampUpdate = [&](std::atomic<std::uint64_t>& stamp, std::uint64_t expected,
			std::uint64_t requestedStamp, std::uint32_t attempt)
		{
			if (attempt <= lmms::MidiChannelOverflow::MaxStampUpdateAttempts && expected + 1 < requestedStamp)
			{
				// Simulate another recorder advancing the stamp while keeping it below this refused event.
				if (stamp.compare_exchange_strong(expected, expected + 1, std::memory_order_relaxed))
				{
					++injectedStampConflicts;
				}
			}
		};
		std::uint32_t pushAttempts = 0;
		std::uint32_t stampAttempts = 0;
		const bool accepted = handoff.enqueueWithTestHooks(
			lmms::MidiEvent(lmms::MidiControlChange, 5, 123, 0), 0, &pushAttempts, &stampAttempts,
			reserveBeforeCompareExchange, contendDuringStampUpdate);

		QVERIFY(!accepted);
		QCOMPARE(competingReservations, static_cast<std::uint32_t>(Capacity));
		QCOMPARE(pushAttempts, lmms::MidiHandoffQueue<Capacity>::MaxPushAttempts);
		QCOMPARE(stampAttempts, lmms::MidiChannelOverflow::MaxStampUpdateAttempts);
		QCOMPARE(injectedStampConflicts, lmms::MidiChannelOverflow::MaxStampUpdateAttempts);
		QCOMPARE(handoff.droppedEvents(), std::uint64_t{1});
		QCOMPARE(handoff.recordedOverflows(), std::uint64_t{1});
		QCOMPARE(handoff.failedOverflowStampUpdates(), std::uint64_t{1});
	}

	void overflowCutReachesNativeProcessAfterAcceptedEvents()
	{
		lmms::CarlaMidiHandoff<2> handoff;
		FakeCarlaPlugin plugin;
		NativePluginDescriptor descriptor{};
		descriptor.process = &FakeCarlaPlugin::process;
		std::array<float, RenderFrames> inputLeft{};
		std::array<float, RenderFrames> inputRight{};
		std::array<float, RenderFrames> outputLeft{};
		std::array<float, RenderFrames> outputRight{};
#if CARLA_VERSION_HEX >= 0x030000
		const float* input[] = {inputLeft.data(), inputRight.data()};
#else
		float* input[] = {inputLeft.data(), inputRight.data()};
#endif
		float* output[] = {outputLeft.data(), outputRight.data()};
		QVERIFY(handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOn, 3, 60, 90), 2));
		QVERIFY(handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOff, 3, 60, 0), 9));
		QVERIFY(!handoff.enqueue(lmms::MidiEvent(lmms::MidiNoteOff, 3, 60, 0), 12));

		const auto render = [&](const NativeMidiEvent* events, std::uint32_t eventCount)
		{
			descriptor.process(&plugin, input, output, RenderFrames, events, eventCount);
		};
		handoff.processPeriod(render);
		QCOMPARE(plugin.seenCount, std::uint32_t{2});
		QCOMPARE(plugin.seen[0].data[0], std::uint8_t{0x93});
		QCOMPARE(plugin.seen[1].data[0], std::uint8_t{0x83});

		handoff.processPeriod(render);
		QCOMPARE(plugin.seenCount, std::uint32_t{1});
		QCOMPARE(plugin.seen[0].time, std::uint32_t{0});
		QCOMPARE(plugin.seen[0].size, std::uint8_t{3});
		QCOMPARE(plugin.seen[0].data[0], std::uint8_t{0xb3});
		QCOMPARE(plugin.seen[0].data[1], std::uint8_t{123});
		QCOMPARE(plugin.seen[0].data[2], std::uint8_t{0});
	}

	void concurrentMidiStressIsOrderedAndAllocationFree()
	{
		ConcurrentStressRun run;
		run.startProducers();
		run.renderWhileProducersRun();
		run.joinProducers();
		run.drainAcceptedEvents();

		QVERIFY2(run.periodsWithProducerInput.load(std::memory_order_relaxed) > 0,
			"processing did not overlap MIDI producer activity");
		QVERIFY2(run.drainPeriods < 100, "the bounded drain did not deliver every accepted event");
		QVERIFY2(run.probe.ordered, "accepted events from a MIDI producer arrived out of order");
		QVERIFY(run.producerResultsMatch());
		QVERIFY(run.producersAllocatedNothing());
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(run.totalAccepted, run.totalReceived);
		QCOMPARE(run.totalAccepted + run.refusedCount(),
			static_cast<std::uint64_t>(ConcurrentStressRun::ProducerCount) * ConcurrentStressRun::EventsPerProducer);
		QVERIFY(run.greatestPushAttemptCount() <= lmms::MidiHandoffQueue<MidiCapacity>::MaxPushAttempts);
		QCOMPARE(run.handoff.droppedEvents(), run.refusedCount());
		QCOMPARE(run.handoff.recordedOverflows(), run.refusedCount());
	}
};

QTEST_APPLESS_MAIN(CarlaMidiHandoffTest)

#include "CarlaMidiHandoffTest.moc"
