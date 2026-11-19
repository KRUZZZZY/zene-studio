/*
 * SampleClipPoolExhaustionTest.cpp - clip starts beyond the play-handle pool are not lost, the audio
 * thread does not allocate while they wait, and a later pass does not start a clip a second time.
 *
 * The engine's play-handle list holds PlayHandle::MaxNumber (1024) handles. 20 tracks of 60 clips
 * give 1200 clip starts at tick 0. play() stages each start; the engine's per-period service
 * (renderNextPeriod) drains the staged starts into the list, runs the deferred retry, and renders.
 * A start that finds no room is deferred. Track 0's first clip is one tick of source, ceil(framesPerTick):
 * 500 frames at 48 kHz and 120 BPM. With a 256-frame period, R1 renders 256 frames and 244 remain, so
 * the clip is still playing after R1 and ends during R2.
 * Every pass runs the real SampleTrack::play under the allocation probe. renderNextPeriod is not used
 * in place of play().
 *
 *   P1  play(TimePos(0)) on all tracks, then R1. The list takes 1024 starts. The surplus 176 are requested
 *       (1200 = TrackCount * ClipsPerTrack) and wait; the new code defers them and counts them, the old code
 *       does not, so only live = 1024 is asserted here. The one-tick clip is scheduled (isPlaying).
 *   P2  play(TimePos(1)) on all tracks, one tick later and inside every long clip's span. Each clip has
 *       started already, so the pass adds no handle and defers nothing. R2 renders the one-tick clip's last
 *       244 frames and it ends. A start that play() had staged again would find the list full and be deferred,
 *       which the deferred counter would show.
 *   topUp: the control thread's service step (the 100 ms timer), not a pass. The slot the ended clip freed
 *       admits one deferred start.
 *   R3, R4: renderNextPeriod, allocation-free. The long clips run 2 s, so no more slots open in this test.
 *   Final: at most 1199 can be live (1200 starts minus the ended one-tick clip). Track 0 holds 60 - 1 = 59.
 *       No track holds more than 60. Only these bounds are asserted; the exact total depends on how the
 *       list holds its surplus (see P1).
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
// PlayHandle::MaxNumber: the engine's play-handle list holds this many starts.
constexpr int ListCapacity = 1024;

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
		// The one-tick clip: ceil(framesPerTick) frames, so windowTicksFor is at least 1. It must outlive
		// the first period and end within two periods.
		const auto tickFrames = static_cast<f_cnt_t>(std::ceil(Engine::framesPerTick(rate)));
		QVERIFY2(tickFrames > frames && tickFrames <= 2 * frames, "one tick of source must outlive a period and end within two");
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
		// The engine's own per-period service: the drain of staged starts into the list, the deferred retry, a render.
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
		auto capacityDeferred = []() { return Engine::audioEngine()->capacityDeferredPlayHandles(); };

		// P1 and R1: every clip starts once. R1 drains the staged starts, so both counts are read after it. Nothing has
		// ended yet, so no subtraction. The list takes ListCapacity starts. The rest are not lost, but on the old code
		// they are not visible through the list or the deferred counter (observed there: live 1024, deferred 0).
		const std::uint64_t deferredBeforeP1 = capacityDeferred();
		const std::uint64_t allocationsP1 = armedPass(TimePos(0));
		const std::uint64_t allocationsR1 = armedRender();
		const int deferredP1 = static_cast<int>(capacityDeferred() - deferredBeforeP1);
		const int liveR1 = liveSamplePlayHandles();
		QVERIFY2(shortClip->isPlaying(), "the one-tick clip was not scheduled");
		QVERIFY(TrackCount * ClipsPerTrack > ListCapacity); // the requests exceed the list, so the exhaustion is real
		QCOMPARE(liveR1, ListCapacity);                      // the list takes exactly its capacity
		QVERIFY2(liveR1 + deferredP1 <= TrackCount * ClipsPerTrack, "more starts counted than were requested");

		// P2 and R2: play() again one tick later, inside every long clip's span. Each clip has already started,
		// so the pass adds no handle and defers nothing: per-track counts and the deferred counter hold.
		const std::vector<int> beforeP2 = perTrackHandles();
		const std::uint64_t deferredBeforeP2 = capacityDeferred();
		const std::uint64_t allocationsP2 = armedPass(TimePos(1));
		QVERIFY2(perTrackHandles() == beforeP2, "play() added a handle for a clip that had already started");
		QCOMPARE(capacityDeferred() - deferredBeforeP2, std::uint64_t{0});

		// R2 renders the one-tick clip's last 244 frames and it ends. A start staged again by P2 would find the
		// list full and be deferred here, so the counter would move.
		const std::uint64_t allocationsR2 = armedRender();
		const std::vector<int> afterR2 = perTrackHandles();
		QCOMPARE(capacityDeferred() - deferredBeforeP2, std::uint64_t{0});
		for (int count : afterR2) { QVERIFY2(count <= ClipsPerTrack, "a clip is in the list twice"); }

		// topUp: the control thread's service step (the 100 ms timer), not a pass. It admits the deferred start
		// that the ended clip's slot allows. Then R3 and R4 render, allocation-free.
		Engine::audioEngine()->topUpPlayHandleCapacityForTest();
		const std::uint64_t allocationsR3 = armedRender();
		const std::uint64_t allocationsR4 = armedRender();

		// Final: 1200 starts, and the one-tick clip has ended, so at most 1199 are live. Track 0 holds its 59 long clips.
		// The other tracks hold at most 60 each. Only these bounds are asserted: the list stays at ListCapacity until
		// room opens, and the exact live total depends on how the surplus is held (see the P1 comment).
		const std::vector<int> finalCounts = perTrackHandles();
		QCOMPARE(finalCounts[0], ClipsPerTrack - 1);
		for (int count : finalCounts) { QVERIFY2(count <= ClipsPerTrack, "a track holds more than its clips"); }
		QVERIFY2(liveSamplePlayHandles() <= TrackCount * ClipsPerTrack - 1, "more clips live than starts minus the ended one");

		// Allocation checks come last, so every functional check above runs first.
		QCOMPARE(allocationsP1, std::uint64_t{0});
		QCOMPARE(allocationsR1, std::uint64_t{0});
		QCOMPARE(allocationsP2, std::uint64_t{0});
		QCOMPARE(allocationsR2, std::uint64_t{0});
		QCOMPARE(allocationsR3, std::uint64_t{0});
		QCOMPARE(allocationsR4, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(SampleClipPoolExhaustionTest)
#include "SampleClipPoolExhaustionTest.moc"
