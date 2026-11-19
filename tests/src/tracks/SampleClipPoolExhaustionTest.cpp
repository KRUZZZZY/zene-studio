/*
 * SampleClipPoolExhaustionTest.cpp - 1200 clip starts across two groups, more than the nominal 1024-entry play-handle
 * list. Every start lands exactly once (none lost, none started twice), and the audio thread does not allocate.
 * The forced-refusal path is covered by the second slot (added next), not by this one.
 *
 * Capacity: the play-handle list's nominal size is PlayHandle::MaxNumber (1024). The engine grows the list ahead of
 * demand, so all 1199 live starts are admitted with no deferral (see the state sequence). A push into the staging list
 * beyond 1024 disposes the handle (AudioEngine::addPlayHandle, AudioEngine.cpp:1497-1509; staging list sized at
 * AudioEngine.cpp:508). That is an existing engine limit. This test never stages more than 1000 starts in one pass, so
 * the limit is not hit and no start is dropped (asserted as droppedPlayHandles() unchanged).
 *
 * Clips: tracks 0..19 (TrackCount 20) each hold 60 clips of 2 s source. Group A is 50 clips per track at TimePos(0):
 * 1000 in all. Group B is 10 clips per track at TimePos(1): 200 in all. Track 0's first clip is the one-tick clip:
 * ceil(framesPerTick) frames of source, 500 at 48 kHz and 120 BPM. It outlives the first 256-frame period and ends
 * within two periods, at tick 1. 1200 clips start and 1 ends, so 1199 must be live at the end: more than 1024.
 *
 * "live" is the SamplePlayHandles in the engine list. "def" is capacityDeferredPlayHandles() as a delta from the start
 * of the test. "latched" is the clips whose isPlaying() is set.
 *
 * State sequence (step: what runs -> expected counts, with the arithmetic). The engine admitted every start without
 * deferring: def stays 0 at every step, so no retry is needed.
 *   P1  play(TimePos(0)) on tracks 0..19, armed. Stages the 1000 group A starts. The one-tick clip isPlaying().
 *   R1  renderNextPeriod, armed. The drain admits all 1000: live 1000, def 0. latched 1000 (every latched clip has
 *       its handle). Track 0 holds 50 handles, including the one-tick clip.
 *   P2  play(TimePos(1)) on tracks 0..19, armed. Group A is latched already, so it starts nothing again. Group B
 *       stages 200 starts. The one-tick clip's span ends at tick 1, so its latch clears: latched 1000 - 1 + 200 = 1199.
 *       Its handle stays in the list until R2, so live is still 1000 and each track holds 50.
 *   R2  renderNextPeriod, armed. The drain admits the 200 staged starts, def 0. The one-tick clip ends and its handle
 *       is removed: live 1000 + 200 - 1 = 1199. That is more than the 1024 nominal list size, and no start is dropped.
 *   TU  topUpPlayHandleCapacityForTest, control thread, not armed. No start is waiting, so no count changes.
 *   The bounded loop after TU runs no render: live already equals 1199 after R2.
 *   Final: live 1199 = 1200 started - 1 ended. Track 0 holds 49 + 10 = 59 (group A without the one-tick clip, and
 *       all 10 group B). Tracks 1..19 hold 50 + 10 = 60 each. 59 + 19 * 60 = 1199. def 0.
 *
 * Every play() and render pass runs under the allocation probe, and the allocation checks run last. renderNextPeriod
 * is the engine's own service step. It is not used in place of play().
 */

#include <QDebug>
#include <QtTest>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "NotePlayHandle.h"
#include "PlayHandle.h"
#include "RealtimeScope.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "SampleWindow.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int TrackCount = 20;                                   // tracks 0..19
constexpr int GroupAPerTrack = 50;                               // long clips at TimePos(0)
constexpr int GroupBPerTrack = 10;                               // long clips at TimePos(1)
constexpr int ClipsPerTrack = GroupAPerTrack + GroupBPerTrack;   // 60; under SampleTrack's per-pass clip limit (64)
constexpr int GroupAClips = TrackCount * GroupAPerTrack;         // 1000
constexpr int GroupBClips = TrackCount * GroupBPerTrack;         // 200
constexpr int StartedClips = GroupAClips + GroupBClips;          // 1200
constexpr int EndedClips = 1;                                    // the one-tick clip
constexpr int ExpectedLive = StartedClips - EndedClips;          // 1199
// PlayHandle::MaxNumber: the play-handle list and the staging list each hold this many before a top-up.
constexpr int ListCapacity = 1024;
// Renders allowed after the top-up before the deferred starts must be admitted.
constexpr int MaxRendersAfterTopUp = 6;

//! One point of the state sequence: the live handle count and the deferred-counter delta at that point.
struct SequenceStep
{
	std::string name;
	int live;
	std::uint64_t deferredDelta;
};

//! Prints the state sequence when the test has failed. A passing run prints nothing.
struct SequenceReport
{
	const std::vector<SequenceStep>* steps;
	~SequenceReport()
	{
		if (!QTest::currentTestFailed()) { return; }
		for (const SequenceStep& step : *steps)
		{
			qDebug("step %-4s live=%d deferredDelta=%llu", step.name.c_str(), step.live,
				static_cast<unsigned long long>(step.deferredDelta));
		}
	}
};

//! Removes a track's handles through the engine's synchronous sweep when the test leaves scope.
struct HandleSweep
{
	std::vector<std::unique_ptr<SampleTrack>>* tracks;
	~HandleSweep()
	{
		for (auto& track : *tracks)
		{
			Engine::audioEngine()->removePlayHandlesOfTypes(track.get(), PlayHandle::Types{PlayHandle::Type::SamplePlayHandle});
		}
	}
};

int liveSamplePlayHandles()
{
	int count = 0;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		if (handle->type() == PlayHandle::Type::SamplePlayHandle) { ++count; }
	}
	return count;
}

//! SamplePlayHandles in the engine's list that belong to `track`.
int handlesFromTrack(const Track& track)
{
	int count = 0;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		if (handle->type() == PlayHandle::Type::SamplePlayHandle && handle->isFromTrack(&track)) { ++count; }
	}
	return count;
}

//! Clips across `tracks` whose isPlaying() latch is set.
int latchedClips(const std::vector<std::unique_ptr<SampleTrack>>& tracks)
{
	int count = 0;
	for (const auto& track : tracks)
	{
		for (Clip* clip : track->getClips())
		{
			auto* sampleClip = dynamic_cast<SampleClip*>(clip);
			if (sampleClip != nullptr && sampleClip->isPlaying()) { ++count; }
		}
	}
	return count;
}

} // namespace

class SampleClipPoolExhaustionTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void startsBeyondTheListCapacityAllSoundWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();

		std::vector<SampleFrame> longData(static_cast<std::size_t>(2 * rate), SampleFrame(0.25f, 0.25f));
		const auto longBuffer = std::make_shared<SampleBuffer>(longData.data(), longData.size(), rate);
		// The one-tick clip: ceil(framesPerTick) frames. It must outlive the first period and end within two periods.
		const auto tickFrames = static_cast<f_cnt_t>(std::ceil(Engine::framesPerTick(rate)));
		QVERIFY2(tickFrames > frames && tickFrames <= 2 * frames, "one tick of source must outlive a period and end within two");
		std::vector<SampleFrame> shortData(static_cast<std::size_t>(tickFrames), SampleFrame(0.5f, 0.5f));
		const auto shortBuffer = std::make_shared<SampleBuffer>(shortData.data(), shortData.size(), rate);

		std::vector<std::unique_ptr<SampleTrack>> tracks;
		HandleSweep sweep{&tracks};
		std::vector<SequenceStep> sequence;
		SequenceReport report{&sequence};
		SampleClip* shortClip = nullptr;
		for (int t = 0; t < TrackCount; ++t)
		{
			tracks.push_back(std::make_unique<SampleTrack>(Engine::getSong()));
			for (int c = 0; c < GroupAPerTrack + GroupBPerTrack; ++c)
			{
				const TimePos position = c < GroupAPerTrack ? TimePos(0) : TimePos(1);
				auto* clip = dynamic_cast<SampleClip*>(tracks.back()->createClip(position));
				QVERIFY(clip != nullptr);
				const bool isShort = t == 0 && c == 0;
				clip->setSampleBuffer(isShort ? shortBuffer : longBuffer);
				if (isShort) { shortClip = clip; }
			}
		}
		QVERIFY(shortClip != nullptr);

		auto trackAt = [&](int t) -> SampleTrack& { return *tracks[static_cast<std::size_t>(t)]; };
		// play() on tracks [first, last) at `position`, with the probe armed for those calls only.
		auto armedPlay = [&](int first, int last, TimePos position) {
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			for (int t = first; t < last; ++t) { trackAt(t).play(position, frames, 0); }
			lmms::test::tlCountAllocations = false;
			return lmms::test::tlAllocationCount;
		};
		// The engine's own per-period service: the retry of deferred starts, the drain of staged starts, a render.
		auto armedRender = [&]() {
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			Engine::audioEngine()->renderNextPeriod();
			lmms::test::tlCountAllocations = false;
			return lmms::test::tlAllocationCount;
		};
		auto perTrackHandles = [&]() {
			std::vector<int> counts;
			for (int t = 0; t < TrackCount; ++t) { counts.push_back(handlesFromTrack(trackAt(t))); }
			return counts;
		};
		auto capacityDeferred = []() { return Engine::audioEngine()->capacityDeferredPlayHandles(); };
		auto latched = [&]() { return latchedClips(tracks); };

		const std::uint64_t deferredStart = capacityDeferred();
		const std::uint64_t droppedStart = Engine::audioEngine()->droppedPlayHandles();
		auto deferredDelta = [&]() { return capacityDeferred() - deferredStart; };
		// Records one point of the state sequence. Called outside the probes only.
		auto record = [&](const std::string& name) {
			sequence.push_back(SequenceStep{name, liveSamplePlayHandles(), deferredDelta()});
		};

		// The pools are sized from the clip count (reserveForClips), so no sample handle is refused in this slot.
		QVERIFY2(ExpectedLive > ListCapacity, "the started total must exceed the list capacity, or the exhaustion path is not exercised");

		// P1: the 1000 group A starts are staged on all tracks. The one-tick clip is scheduled.
		const std::uint64_t allocP1 = armedPlay(0, TrackCount, TimePos(0));
		record("P1");
		QVERIFY2(shortClip->isPlaying(), "the one-tick clip was not scheduled");

		// R1: the drain admits all 1000. The list has 1024 free slots, so none is deferred.
		const std::uint64_t allocR1 = armedRender();
		record("R1");
		const int liveR1 = liveSamplePlayHandles();
		QCOMPARE(liveR1, GroupAClips);                               // 1000 admitted
		QCOMPARE(deferredDelta(), std::uint64_t{0});                 // none deferred
		QCOMPARE(latched(), liveR1);                                 // every latched clip has its handle
		QCOMPARE(handlesFromTrack(trackAt(0)), GroupAPerTrack);      // 50, including the one-tick clip

		// P2: group B's 200 starts are staged. Group A is latched already, so it starts nothing again.
		// The one-tick clip's span ends at tick 1, so its latch clears here. Its handle stays until R2.
		const std::uint64_t allocP2 = armedPlay(0, TrackCount, TimePos(1));
		record("P2");
		QVERIFY2(!shortClip->isPlaying(), "the one-tick clip's latch did not clear at the end of its span");
		QCOMPARE(latched(), GroupAClips - EndedClips + GroupBClips); // 1000 - 1 + 200 = 1199
		QCOMPARE(liveSamplePlayHandles(), GroupAClips);              // the list is unchanged until R2: 1000
		const std::vector<int> countsP2 = perTrackHandles();
		for (int t = 0; t < TrackCount; ++t)
		{
			QCOMPARE(countsP2[static_cast<std::size_t>(t)], GroupAPerTrack); // 50 each: no clip started a second time
		}

		// R2: the drain admits the 200 staged starts. The one-tick clip ends and its handle is removed.
		const std::uint64_t allocR2 = armedRender();
		record("R2");
		const int liveR2 = liveSamplePlayHandles();
		// The deferred counter is monotonic, so its delta is never negative. A backwards move would wrap the unsigned
		// delta to a huge value, which reads as negative when viewed as a signed 64-bit number.
		const std::uint64_t defR2Delta = deferredDelta();
		QVERIFY2(static_cast<std::int64_t>(defR2Delta) >= 0, "the deferred counter moved backwards");
		const int defR2 = static_cast<int>(defR2Delta);
		// stillDeferredR2 counts latched clips without a handle. In the observed run it and defR2 are both 0, so the check
		// holds trivially (0 = 0). It would catch a latched clip that has no handle.
		const int stillDeferredR2 = latched() - liveR2;              // latched clips without a handle yet
		QCOMPARE(stillDeferredR2, defR2);                            // every latched clip without a handle is a deferred start
		// Live + deferred = 1199 = 1200 started - 1 ended. This holds only if the one-tick handle is gone.
		QCOMPARE(liveR2 + defR2, ExpectedLive);
		// Track 0 holds its 49 long group A clips, plus the group B starts admitted so far (0..10).
		const int track0R2 = handlesFromTrack(trackAt(0));
		QVERIFY2(track0R2 >= GroupAPerTrack - EndedClips && track0R2 <= GroupAPerTrack - EndedClips + GroupBPerTrack,
			"track 0 must hold 49 group A clips plus the group B starts admitted so far");
		QCOMPARE(Engine::audioEngine()->droppedPlayHandles(), droppedStart);                // no start was dropped by the engine

		// TU: models the control thread's service step, not armed. Nothing is waiting at this point (R2 admitted every
		// staged start), so no count changes.
		Engine::audioEngine()->topUpPlayHandleCapacityForTest();
		record("TU");

		// R3 onward: a bounded loop. When live already equals 1199 after R2 it runs no render. Otherwise it gives
		// deferred starts a fixed number of renders (MaxRendersAfterTopUp) to be admitted.
		std::vector<std::uint64_t> allocLater;
		for (int i = 0; i < MaxRendersAfterTopUp && liveSamplePlayHandles() != ExpectedLive; ++i)
		{
			allocLater.push_back(armedRender());
			record("R" + std::to_string(3 + i));
		}
		QCOMPARE(liveSamplePlayHandles(), ExpectedLive);             // 1199: every started clip except the one that ended
		QCOMPARE(deferredDelta(), static_cast<std::uint64_t>(defR2)); // no new deferral after the top-up

		// Final: track 0 holds 49 group A + 10 group B = 59. Tracks 1..19 hold 50 + 10 = 60 each.
		const std::vector<int> finalCounts = perTrackHandles();
		QCOMPARE(finalCounts[0], GroupAPerTrack - EndedClips + GroupBPerTrack);  // 59
		for (int t = 1; t < TrackCount; ++t)
		{
			QCOMPARE(finalCounts[static_cast<std::size_t>(t)], ClipsPerTrack);   // 60
		}
		QCOMPARE(Engine::audioEngine()->droppedPlayHandles(), droppedStart);

		// The deferred counter never moves backwards across the recorded sequence.
		for (std::size_t i = 1; i < sequence.size(); ++i)
		{
			QVERIFY2(sequence[i].deferredDelta >= sequence[i - 1].deferredDelta, "the deferred counter went backwards");
		}

		// Allocation checks come last, so every functional check above runs first.
		QCOMPARE(allocP1, std::uint64_t{0});
		QCOMPARE(allocR1, std::uint64_t{0});
		QCOMPARE(allocP2, std::uint64_t{0});
		QCOMPARE(allocR2, std::uint64_t{0});
		for (std::uint64_t alloc : allocLater) { QCOMPARE(alloc, std::uint64_t{0}); }
	}

	// Forced refusal: the handle pool is drained, so the start is refused. The control thread then grows the pool by
	// one block, and the retry at the same position lands.
	void aRefusedStartIsDeferredThenLandsAfterTheControlThreadGrowsThePool()
	{
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const int rate = Engine::audioEngine()->outputSampleRate();

		std::vector<SampleFrame> longData(static_cast<std::size_t>(2 * rate), SampleFrame(0.25f, 0.25f));
		const auto longBuffer = std::make_shared<SampleBuffer>(longData.data(), longData.size(), rate);

		std::vector<std::unique_ptr<SampleTrack>> tracks;
		HandleSweep sweep{&tracks};
		// Declared after the sweep, so the drained handles are deleted before the sweep runs and before the tracks go.
		// They are never added to the engine.
		std::vector<SamplePlayHandle*> drained;
		struct DrainedHandles
		{
			std::vector<SamplePlayHandle*>* handles;
			~DrainedHandles()
			{
				for (SamplePlayHandle* handle : *handles) { delete handle; }
			}
		} drainedGuard{&drained};

		tracks.push_back(std::make_unique<SampleTrack>(Engine::getSong()));
		SampleTrack& track = *tracks.back();
		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(longBuffer);

		// Drain the handle pool on this (non-audio) thread until tryCreate refuses. The bound keeps the loop finite.
		constexpr int DrainLimit = 100000;
		bool poolEmptied = false;
		for (int i = 0; i < DrainLimit && !poolEmptied; ++i)
		{
			SamplePlayHandle* handle = SamplePlayHandle::tryCreate(clip, clip->sampleWindow());
			if (handle == nullptr) { poolEmptied = true; }
			else { drained.push_back(handle); }
		}
		QVERIFY2(poolEmptied, "the handle pool did not run empty within the drain bound");
		QVERIFY2(!drained.empty(), "the handle pool held no blocks to drain");

		// play() at TimePos(0), armed: whether a clip started, and the allocations the call made.
		struct PlayResult
		{
			bool started;
			std::uint64_t allocations;
		};
		auto armedTrackPlay = [&]() {
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			const bool started = track.play(TimePos(0), frames, 0);
			lmms::test::tlCountAllocations = false;
			return PlayResult{started, static_cast<std::uint64_t>(lmms::test::tlAllocationCount)};
		};

		// Refused start: the pool is empty, so the start is deferred and its latch is cleared.
		const std::uint64_t deferredBefore = SampleTrack::deferredClipStarts();
		const PlayResult refused = armedTrackPlay();
		const std::uint64_t deferredAfterRefused = SampleTrack::deferredClipStarts();
		{
			RealtimeScope realtime;
			Engine::audioEngine()->renderNextPeriod();
		}
		QVERIFY2(!refused.started, "a refused start must not report that a clip started");
		QVERIFY2(!clip->isPlaying(), "the refused clip's latch was not cleared, so it could not retry");
		QCOMPARE(deferredAfterRefused - deferredBefore, std::uint64_t{1});
		QCOMPARE(handlesFromTrack(track), 0);

		// The control thread grows the pool by one block, outside any RealtimeScope. The retry at the same position lands.
		SamplePlayHandle::reserveBlocks(1);
		const PlayResult retried = armedTrackPlay();
		QVERIFY2(retried.started, "the retried start did not land after the pool grew");
		QVERIFY2(clip->isPlaying(), "the retried start did not set the latch");
		{
			RealtimeScope realtime;
			Engine::audioEngine()->renderNextPeriod();
		}
		QCOMPARE(handlesFromTrack(track), 1);

		// Allocation checks come last.
		QCOMPARE(refused.allocations, std::uint64_t{0});
		QCOMPARE(retried.allocations, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(SampleClipPoolExhaustionTest)
#include "SampleClipPoolExhaustionTest.moc"
