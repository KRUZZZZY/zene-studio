/*
 * SampleClipPoolExhaustionTest.cpp - clip starts beyond the play-handle pool are not lost, and the
 * audio thread does not allocate while they wait.
 *
 * The engine's play-handle list holds PlayHandle::MaxNumber (1024) handles. A pass that starts more
 * clips than that defers the surplus; the control thread tops the list's capacity up, and the next
 * deterministic pass lands the deferred starts. This test starts 1200 clips across 20 tracks
 * through the real SampleTrack::play, one clip of which is shorter than a period, and asserts:
 *   - the audio-thread allocation probe reads zero across every play() and render pass;
 *   - after the control-thread service step and the next pass, every clip that should sound has a
 *     live SamplePlayHandle (the short clip has ended within its period, so it is not live).
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
		std::vector<SampleFrame> longData(static_cast<std::size_t>(2 * rate), SampleFrame(0.25f, 0.25f));
		const auto longBuffer = std::make_shared<SampleBuffer>(longData.data(), longData.size(), rate);
		// A short clip: its window ends within one period, so it is not live after the pass that starts it.
		std::vector<SampleFrame> shortData(64, SampleFrame(0.5f, 0.5f));
		const auto shortBuffer = std::make_shared<SampleBuffer>(shortData.data(), shortData.size(), rate);

		std::vector<std::unique_ptr<SampleTrack>> tracks;
		HandleSweep sweep{&tracks};
		for (int t = 0; t < TrackCount; ++t)
		{
			tracks.push_back(std::make_unique<SampleTrack>(Engine::getSong()));
			for (int c = 0; c < ClipsPerTrack; ++c)
			{
				auto* clip = dynamic_cast<SampleClip*>(tracks.back()->createClip(TimePos(0)));
				QVERIFY(clip != nullptr);
				clip->setSampleBuffer(t == 0 && c == 0 ? shortBuffer : longBuffer);
			}
		}

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		{
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			for (auto& track : tracks) { track->play(TimePos(0), frames, 0); }
			lmms::test::tlCountAllocations = false;
		}
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});

		// Pass 1: the list is at its capacity, so the surplus starts are deferred, not dropped.
		{
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			Engine::audioEngine()->renderNextPeriod();
			lmms::test::tlCountAllocations = false;
		}
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});

		// The control-thread service step grows the list; the next deterministic pass lands the rest.
		Engine::audioEngine()->topUpPlayHandleCapacityForTest();
		{
			RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			Engine::audioEngine()->renderNextPeriod();
			lmms::test::tlCountAllocations = false;
		}
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});

		// Every long clip that should sound is live; the short clip has ended and is not counted.
		const int expectedLive = TrackCount * ClipsPerTrack - 1;
		QCOMPARE(liveSamplePlayHandles(), expectedLive);
	}
};

QTEST_GUILESS_MAIN(SampleClipPoolExhaustionTest)
#include "SampleClipPoolExhaustionTest.moc"
