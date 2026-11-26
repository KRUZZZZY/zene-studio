/*
 * SamplePlayHandleRetireTest.cpp - the engine's disposal of a heap-backed sample play handle
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

/*! Cluster new-p15-181. The engine disposes a finished play handle ON THE AUDIO THREAD:
 *  AudioEngine::renderStageEffects (src/core/AudioEngine.cpp:1151) walks its list inside
 *  renderNextPeriod's RealtimeScope (src/core/AudioEngine.cpp:1228) and calls disposePlayHandle()
 *  (src/core/AudioEngine.cpp:453), whose last line is `delete handle` (src/core/AudioEngine.cpp:468)
 *  for every handle that is neither pooled nor a note. Destroying a SamplePlayHandle is not free:
 *  a comped clip's slipped spans own a CompSlipReaders, which CompSlipReaders::create() allocates
 *  (src/core/CompSlip.cpp:44), and a pre-faded or looped clip an optional Sample snapshot
 *  (include/SamplePlayHandle.h:329,339) whose buffer the handle may be the last owner of. Handing
 *  that memory back to the allocator on the audio thread can take the allocator's lock.
 *
 *  The engine's own contract for this is PlayHandle::handOverToOwner() (include/PlayHandle.h:91,
 *  "the handle's owner frees it: the engine then drops its reference without deleting it"), which
 *  disposePlayHandle() asks before it deletes (src/core/AudioEngine.cpp:463).
 *
 *  RED before the fix: handOverToOwner() is the base's false, so the engine's `delete` frees the
 *  readers and the handle on the render path. The test uses no new API on purpose, so the reverted
 *  tree still compiles; and it is self-validating: with the fix in place the first assertion can
 *  only hold if the handle really owned heap-backed state, because an ordinary handle is still
 *  deleted there (the last check).
 */

#include <QtTest>

#include <QTemporaryDir>
#include <QVector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "RealtimeScope.h"
#include "Sample.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"
#include "TakeLaneTestSupport.h"   // the shared take-lane fixtures
#include "Track.h"

using namespace lmms;

class SamplePlayHandleRetireTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! A comped take clip's slipped span gives its handle a CompSlipReaders (heap). Where the engine
	//! disposes a finished handle - the render path, inside a RealtimeScope - nothing may reach the
	//! allocator; the control-thread service point then frees it.
	void disposingASlippedCompHandleDoesNotFreeOnTheRenderPath()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVector<SampleClip*> clips;
		SampleTrack* const track = taketest::makeTakeTrack(dir.path(), 1, &clips);
		QVERIFY(track != nullptr);
		QCOMPARE(static_cast<int>(clips.size()), 1);
		SampleClip* const clip = clips.front();

		// A composite that names this clip's own lane over the clip's whole span, slipped by two ticks:
		// the handle's snapshotComp() then creates its CompSlipReaders (SamplePlayHandle.cpp:382).
		TakeLaneModel& comp = track->takeLanes();
		const int lane = comp.addLane(QStringLiteral("Slipped"));
		QVERIFY(lane >= 0);
		clip->setLaneIndex(lane);
		QVERIFY(comp.selectSegment(clip->startPosition().getTicks(), clip->endPosition().getTicks(), lane, 2));

		auto* handle = new SamplePlayHandle(clip, clip->sampleWindow());

		// The engine's disposal, run exactly where the engine runs it: hand the handle over, else delete
		// it (AudioEngine::disposePlayHandle's own two lines, src/core/AudioEngine.cpp:463-468).
		std::uint64_t freesOnRenderPath = 0;
		{
			const RealtimeScope realtime;
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			if (!handle->handOverToOwner()) { delete handle; }
			freesOnRenderPath = lmms::test::tlFreeCount;
			lmms::test::tlCountAllocations = false;
		}
		QVERIFY2(freesOnRenderPath == 0,
			"the engine's disposal handed the handle's heap state back to the allocator on the audio thread");

		// The control-thread service point (src/core/AudioEngine.cpp:1628; the engine's 100 ms timer
		// calls it) frees what the render path retired.
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		Engine::audioEngine()->serviceAudioBusHandlesOnControlThread();
		const std::uint64_t freesOnControlThread = lmms::test::tlFreeCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(freesOnControlThread > 0, "the retired handle was never freed: its state leaked");

		// An ordinary handle owns nothing heap-backed: the engine still deletes it where it always did, so
		// the common path (a clip with no comp slip, no pre-fades and no audio loop) is unchanged.
		SampleFrame frames[4] = {};
		Sample plainSample(frames, 4, Engine::audioEngine()->baseSampleRate());
		SamplePlayHandle plain(&plainSample, false);
		bool handedOver = true;
		{
			const RealtimeScope realtime;
			handedOver = plain.handOverToOwner();
		}
		QVERIFY2(!handedOver,
			"an ordinary handle was retired too: every plain sample handle would now be freed a timer tick later");
	}
};

QTEST_GUILESS_MAIN(SamplePlayHandleRetireTest)
#include "SamplePlayHandleRetireTest.moc"
