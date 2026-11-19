/*
 * SampleClipStartAllocationTest.cpp - a sample clip's start, through SampleTrack::play, does not
 * allocate on the audio thread.
 *
 * SampleTrack::play is the path the audio thread runs when a pass schedules clips. It builds each
 * SamplePlayHandle with `new` there (SampleTrack.cpp, "new SamplePlayHandle"), so a clip start is a
 * heap allocation in the render. Three slots, each on its own track, drive the real play() under
 * RealtimeScope with the allocation probe armed:
 *   (a) a plain clip;
 *   (b) a comped clip whose segment carries a slip (a non-zero sourceOffset), so that
 *       CompSlipReaders::create builds readers (CompSlip.cpp:43-54);
 *   (c) a PreservePitch clip with a rate change (warp markers), so the stretcher is prepared
 *       (SamplePlayHandle.cpp:164, 183).
 * Each slot asserts, in this order: play() scheduled the clip; exactly one SamplePlayHandle from its
 * track is in the engine's list; and only then that the pass allocated nothing.
 */

#include <QtTest>

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
#include "TakeLane.h"
#include "TimePos.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

//! Two seconds of a constant level: long enough that the started handles are still alive after a period.
std::shared_ptr<const SampleBuffer> makeBuffer(int rate)
{
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate), SampleFrame(0.25f, 0.25f));
	return std::make_shared<SampleBuffer>(data.data(), data.size(), rate);
}

//! Removes the track's sample handles through the engine's synchronous sweep when it goes out of scope,
//! so no handle outlives the track it points at, whether the test passes or fails.
struct HandleSweep
{
	Track* track;
	~HandleSweep()
	{
		Engine::audioEngine()->removePlayHandlesOfTypes(track, PlayHandle::Types{PlayHandle::Type::SamplePlayHandle});
	}
};

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

//! The slip on the comped clip's segment, in ticks. A non-zero sourceOffset is what CompSlipReaders::create needs.
constexpr int SlipTicks = 2;

struct Pass
{
	bool played = false;
	std::uint64_t allocations = 0;
};

//! One period of `track` at tick 0. The allocation probe is armed for the play() call only.
Pass armedPlay(SampleTrack& track)
{
	Pass pass;
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	{
		RealtimeScope realtime;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		pass.played = track.play(TimePos(0), frames, 0);
		lmms::test::tlCountAllocations = false;
		pass.allocations = lmms::test::tlAllocationCount;
	}
	return pass;
}

} // namespace

class SampleClipStartAllocationTest : public QObject
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

	void playStartsPlainClipWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		HandleSweep sweep{&track};

		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(makeBuffer(rate));

		const Pass pass = armedPlay(track);
		const std::uint64_t allocations = pass.allocations;
		QVERIFY2(pass.played, "SampleTrack::play scheduled no plain clip");
		QCOMPARE(handlesFromTrack(track), 1);
		QCOMPARE(allocations, std::uint64_t{0});
	}

	void playStartsCompedClipWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		HandleSweep sweep{&track};

		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(makeBuffer(rate));
		clip->setLaneIndex(0);

		// The comp selects the clip's lane across its whole span, with a slip: CompSlipReaders::create
		// returns readers only for a segment on the clip's lane whose sourceOffset is non-zero.
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(QStringLiteral("take 1"));
		QVERIFY(comp.selectSegment(0, clip->length().getTicks(), 0, SlipTicks));

		const Pass pass = armedPlay(track);
		const std::uint64_t allocations = pass.allocations;
		QVERIFY2(pass.played, "SampleTrack::play scheduled no comped clip");
		QCOMPARE(handlesFromTrack(track), 1);
		QCOMPARE(allocations, std::uint64_t{0});
	}

	void playStartsPreservePitchClipWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		HandleSweep sweep{&track};

		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(makeBuffer(rate));
		// A rate change: half a second of source across two beats (96 ticks). Warp markers make the clip
		// not rendersLinearly(), so its stretcher is prepared (SamplePlayHandle.cpp:164, 183).
		const WarpMarker markers[] = {{0, 0}, {static_cast<f_cnt_t>(rate / 2), 96}};
		QVERIFY(clip->setWarpMarkers(markers));
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);

		const Pass pass = armedPlay(track);
		const std::uint64_t allocations = pass.allocations;
		QVERIFY2(pass.played, "SampleTrack::play scheduled no PreservePitch clip");
		QCOMPARE(handlesFromTrack(track), 1);
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(SampleClipStartAllocationTest)
#include "SampleClipStartAllocationTest.moc"
