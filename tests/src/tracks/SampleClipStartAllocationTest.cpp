/*
 * SampleClipStartAllocationTest.cpp - a sample clip's start, through SampleTrack::play, does not
 * allocate on the audio thread.
 *
 * SampleTrack::play is the path the audio thread runs when a pass schedules clips. It used to
 * build each SamplePlayHandle with `new` there (SampleTrack.cpp, "new SamplePlayHandle"), so a
 * clip start was a heap allocation in the render. This test drives the real play() with a plain
 * clip, a comped clip and a PreservePitch clip under RealtimeScope with the allocation probe armed,
 * and asserts that no allocation happened and that a SamplePlayHandle was added.
 */

#include <QtTest>

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

int liveSamplePlayHandles()
{
	int count = 0;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		if (handle->type() == PlayHandle::Type::SamplePlayHandle) { ++count; }
	}
	return count;
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

	void playStartsPlainComppedAndPreservePitchClipsWithoutAllocating()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		const auto buffer = makeBuffer(rate);

		SampleTrack track(Engine::getSong());
		HandleSweep sweep{&track};

		auto* plain = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(plain != nullptr);
		plain->setSampleBuffer(buffer);

		auto* comped = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(comped != nullptr);
		comped->setSampleBuffer(buffer);
		comped->setLaneIndex(0);

		auto* preserved = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(preserved != nullptr);
		preserved->setSampleBuffer(buffer);
		preserved->setWarpStretchMode(WarpStretchMode::PreservePitch);

		// The comp selects the comped clip's lane across its whole span.
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(QStringLiteral("take 1"));
		QVERIFY(comp.selectSegment(0, comped->length().getTicks(), 0));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		bool played = false;
		{
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			played = track.play(TimePos(0), frames, 0);
			lmms::test::tlCountAllocations = false;
		}
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QVERIFY2(played, "SampleTrack::play scheduled no clip");

		// A SamplePlayHandle must have been added: one render period hands the list to the engine.
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY2(liveSamplePlayHandles() >= 1, "no SamplePlayHandle reached the engine's play-handle list");
	}
};

QTEST_GUILESS_MAIN(SampleClipStartAllocationTest)
#include "SampleClipStartAllocationTest.moc"
