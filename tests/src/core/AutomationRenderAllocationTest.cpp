/*
 * AutomationRenderAllocationTest.cpp - the audio thread's automation read allocates nothing
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

// Song::processNextBuffer reads the automation of every tick on the audio thread. That read built a
// std::vector<Track*>, a std::vector<Clip*> and a QMap on each call, and RealtimeSanitizer reported all of
// them (hosted run 38707621703: RecordingLatencyTest, DummyPacingTest, SessionSchedulerRenderTest). This
// plays a song with absolute and relative automation, and a project with none, and counts the allocations
// the period makes after a warm-up. It also pins that the answer did not change: the flat scratch the audio
// thread uses and the QMap Song::automatedValuesAt hands the control thread come from the same code.

#include <QtTest>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationRenderScratch.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

class AutomationRenderAllocationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! A song with no automation at all: the read still builds its track and value lists, and must not allocate.
	void aPeriodWithNoAutomationAllocatesNothing()
	{
		Song* song = Engine::getSong();
		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 8), 0ull);
		song->stop();
	}

	//! An absolute clip drives a model, a relative clip trims another: the values are set, and no period allocates.
	void aPeriodWithAbsoluteAndRelativeAutomationAllocatesNothing()
	{
		Song* song = Engine::getSong();
		FloatModel absolute{0.0f, 0.0f, 1.0f, 0.001f};
		FloatModel relative{0.5f, 0.0f, 1.0f, 0.001f};

		AutomationTrack track(song);
		AutomationClip absoluteClip(&track);
		absoluteClip.setProgressionType(AutomationClip::ProgressionType::Linear);
		absoluteClip.putValue(0, 0.0, false);
		absoluteClip.putValue(4000, 1.0, false);
		absoluteClip.movePosition(0);
		absoluteClip.addObject(&absolute);

		AutomationClip relativeClip(&track);
		relativeClip.setProgressionType(AutomationClip::ProgressionType::Linear);
		relativeClip.putValue(0, 0.5, false);
		relativeClip.putValue(4000, 0.5, false);
		relativeClip.movePosition(0);
		relativeClip.setRelative(true);
		relativeClip.addObject(&relative);

		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 8), 0ull);

		// The clips were read: the absolute model left its starting value.
		QVERIFY2(absolute.value() > 0.0f, "the automation clip was never applied");
		song->stop();
	}

	//! The audio thread's scratch and the control thread's QMap are one read, so they agree.
	void theScratchAndTheMapAreTheSameRead()
	{
		Song* song = Engine::getSong();
		FloatModel model{0.0f, 0.0f, 1.0f, 0.001f};

		AutomationTrack track(song);
		AutomationClip clip(&track);
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(0, 0.0, false);
		clip.putValue(100, 1.0, false);
		clip.movePosition(0);
		clip.addObject(&model);

		AutomationRenderScratch level(AutomationRenderScratch::RenderCapacity);
		AutomationRenderScratch inner(AutomationRenderScratch::RenderCapacity);
		song->fillAutomatedValues(level, &inner, TimePos(50));
		QCOMPARE(static_cast<int>(level.size()), 1);
		QVERIFY(level.contains(&model));
		QCOMPARE(*level.find(&model), 0.5f);
		QCOMPARE(song->automatedValuesAt(TimePos(50)).value(&model), 0.5f);
		QCOMPARE(static_cast<qulonglong>(level.overflows()), 0ull);
	}

	//! A scratch with no room drops what does not fit and says so, instead of growing on the audio thread.
	void aFullScratchDropsAndCountsInsteadOfGrowing()
	{
		FloatModel a;
		FloatModel b;
		AutomationRenderScratch scratch(1);
		QVERIFY(scratch.set(&a, 1.0f));
		QVERIFY(scratch.set(&a, 2.0f));  // the same model replaces its value and needs no room
		QCOMPARE(*scratch.find(&a), 2.0f);
		QVERIFY2(!scratch.set(&b, 3.0f), "a full bounded scratch grew");
		QCOMPARE(static_cast<int>(scratch.size()), 1);
		QCOMPARE(static_cast<qulonglong>(scratch.takeOverflows()), 1ull);

		AutomationRenderScratch unbounded;
		QVERIFY(unbounded.set(&a, 1.0f));
		QVERIFY(unbounded.set(&b, 2.0f));
		QCOMPARE(static_cast<int>(unbounded.size()), 2);
		QCOMPARE(static_cast<qulonglong>(unbounded.overflows()), 0ull);
	}

private:
	//! Plays \a periods buffers after a warm-up (the first reads fill lazily created state) and returns the
	//! allocations the counted ones made on this thread.
	static unsigned long long allocationsOverPeriods(Song* song, int periods)
	{
		for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < periods; ++i) { song->processNextBuffer(); }
		test::tlCountAllocations = false;
		return test::tlAllocationCount;
	}
};

QTEST_GUILESS_MAIN(AutomationRenderAllocationTest)
#include "AutomationRenderAllocationTest.moc"
