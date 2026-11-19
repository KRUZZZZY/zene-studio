/*
 * SampleClipPoolExhaustionTest.cpp - clip starts beyond the play-handle pool are not lost, the audio
 * thread does not allocate while they wait, and a later pass does not start a clip a second time.
 *
 * The engine's play-handle list holds PlayHandle::MaxNumber (1024) handles. 20 tracks of 60 clips
 * give 1200 clip starts at tick 0. The clip on track 0 is one tick of source (ceil(framesPerTick),
 * so its window is at least one tick) and ends within two periods, so it would be over before a
 * 100 ms control-thread timer fires. Every pass runs the real SampleTrack::play under the allocation
 * probe; only renderNextPeriod stands in for the engine's own per-period service and retry.
 *
 *   P1  play(TimePos(0)) on all tracks: 1200 starts. The list takes 1024; 1200 - 1024 = 176 are deferred.
 *       Starts = live + deferred = 1024 + 176 = TrackCount * ClipsPerTrack = 1200. The one-tick clip is scheduled.
 *   R1  renderNextPeriod: the retry finds no room (no growth), so the 176 stay deferred; one period renders.
 *   P2  play(TimePos(1)) on all tracks, one tick later, inside every long clip's span: each clip is
 *       still playing or its window is spent, so 0 new starts (per-track handle counts unchanged).
 *   topUp: the control thread's service step (the 100 ms timer), not a pass.
 *   R3  renderNextPeriod: the 176 deferred starts land, allocation-free.
 *   R4  renderNextPeriod: the one-tick clip has finished within its two periods.
 *   Live = 1200 - 1 = 1199: track 0 holds 60 - 1 = 59 handles, each other track 60.
 */

#include <QtTest>

#include <cmath>
#include <cstdint>
#include <memory>
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
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int TrackCount = 20;
// Under SampleTrack's per-pass clip limit (64), so every clip of a track reaches the scheduler in one pass.
constexpr int ClipsPerTrack = 60;

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

	void deferredClipStartsLandOnTheNextPassWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();

		std::vector<SampleFrame> longData(static_cast<std::size_t>(2 * rate), SampleFrame(0.25f, 0.25f));
		const auto longBuffer = std::make_shared<SampleBuffer>(longData.data(), longData.size(), rate);
		// The one-tick clip: ceil(framesPerTick) frames, so windowTicksFor is at least 1, and it must end within two periods.
		const auto tickFrames = static_cast<f_cnt_t>(std::ceil(Engine::framesPerTick(rate)));
		QVERIFY2(tickFrames <= 2 * frames, "one tick of source must end within about two periods");
		std::vector<SampleFrame> shortData(static_cast<std::size_t>(tickFrames), SampleFrame(0.5f, 0.5f));
		const auto shortBuffer = std::make_shared<SampleBuffer>(shortData.data(), shortData.size(), rate);

		std::vector<std::unique_ptr<SampleTrack>> tracks;
		HandleSweep sweep{&tracks};
		SampleClip* shortClip = nullptr;
		for (int t = 0; t < TrackCount; ++t)
		{
			tracks.push_back(std::make_unique<SampleTrack>(Engine::getSong()));
			for (int c = 0; c < ClipsPerTrack; ++c)
			{
				auto* clip = dynamic_cast<SampleClip*>(tracks.back()->createClip(TimePos(0)));
				QVERIFY(clip != nullptr);
				const bool isShort = t == 0 && c == 0;
				clip->setSampleBuffer(isShort ? shortBuffer : longBuffer);
				if (isShort) { shortClip = clip; }
			}
		}
		QVERIFY(shortClip != nullptr);

		// One play() on every track at `position`, with the probe armed for the play() calls only.
		auto armedPass = [&](TimePos position) {
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			for (auto& track : tracks) { track->play(position, frames, 0); }
			lmms::test::tlCountAllocations = false;
			return lmms::test::tlAllocationCount;
		};
		// The engine's own per-period service: the deferred retry, then one period rendered.
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
			for (const auto& track : tracks) { counts.push_back(handlesFromTrack(*track)); }
			return counts;
		};

		// P1: every clip starts once. The list takes 1024 and the surplus 176 are deferred, not dropped.
		const std::uint64_t deferredBeforeP1 = Engine::audioEngine()->capacityDeferredPlayHandles();
		const std::uint64_t allocationsP1 = armedPass(TimePos(0));
		const int deferredP1 = static_cast<int>(Engine::audioEngine()->capacityDeferredPlayHandles() - deferredBeforeP1);
		const int startedP1 = liveSamplePlayHandles() + deferredP1;
		QVERIFY2(shortClip->isPlaying(), "the one-tick clip was not scheduled");
		QCOMPARE(startedP1, TrackCount * ClipsPerTrack);
		QCOMPARE(allocationsP1, std::uint64_t{0});

		// R1: the retry finds no room, so the deferred starts wait for the control thread.
		const std::uint64_t allocationsR1 = armedRender();
		QCOMPARE(allocationsR1, std::uint64_t{0});

		// P2: one tick later, still inside every long clip's span. No clip starts a second time.
		const std::vector<int> beforeP2 = perTrackHandles();
		const std::uint64_t deferredBeforeP2 = Engine::audioEngine()->capacityDeferredPlayHandles();
		const std::uint64_t allocationsP2 = armedPass(TimePos(1));
		const std::vector<int> afterP2 = perTrackHandles();
		QCOMPARE(Engine::audioEngine()->capacityDeferredPlayHandles() - deferredBeforeP2, std::uint64_t{0});
		for (std::size_t t = 0; t < beforeP2.size(); ++t) { QCOMPARE(afterP2[t], beforeP2[t]); }
		QCOMPARE(allocationsP2, std::uint64_t{0});

		// The control thread's service step (the 100 ms timer), then the engine's next period lands the deferred starts.
		Engine::audioEngine()->topUpPlayHandleCapacityForTest();
		const std::uint64_t allocationsR3 = armedRender();
		QCOMPARE(allocationsR3, std::uint64_t{0});

		// R4: the one-tick clip has finished within its two periods.
		const std::uint64_t allocationsR4 = armedRender();
		QCOMPARE(allocationsR4, std::uint64_t{0});

		// 1200 starts minus the one-tick clip, which has ended: 1199 live. Track 0 holds 59, the others 60.
		const std::vector<int> finalCounts = perTrackHandles();
		QCOMPARE(liveSamplePlayHandles(), TrackCount * ClipsPerTrack - 1);
		QCOMPARE(finalCounts[0], ClipsPerTrack - 1);
		for (std::size_t t = 1; t < finalCounts.size(); ++t) { QCOMPARE(finalCounts[t], ClipsPerTrack); }
	}
};

QTEST_GUILESS_MAIN(SampleClipPoolExhaustionTest)
#include "SampleClipPoolExhaustionTest.moc"
