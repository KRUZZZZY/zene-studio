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

#include <cmath>
#include <memory>
#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationRenderScratch.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "RealtimeScope.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! One automation track with a flat clip per model: model i is automated to i % 100 (a whole number, so setValue sees no rounding change).
class Fixture
{
public:
	Fixture(Song* song, int clips) : m_track(song)
	{
		for (int i = 0; i < clips; ++i)
		{
			m_models.push_back(std::make_unique<FloatModel>(0.0f, 0.0f, 100.0f, 1.0f));
			auto clip = std::make_unique<AutomationClip>(&m_track);
			clip->setProgressionType(AutomationClip::ProgressionType::Discrete);
			clip->putValue(0, expected(i), false);
			clip->putValue(4000, expected(i), false);
			clip->movePosition(0);
			clip->addObject(m_models.back().get());
			m_clips.push_back(std::move(clip));
		}
	}

	~Fixture() { m_clips.clear(); }  // before the models they point at and the track that owns their list

	void resetModels()
	{
		for (auto& model : m_models) { model->setValue(0.0f); }
	}

	void verifyApplied() const
	{
		int wrong = 0;
		for (std::size_t i = 0; i < m_models.size(); ++i)
		{
			if (std::fabs(m_models[i]->value() - expected(static_cast<int>(i))) > 0.5f) { ++wrong; }
		}
		QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 of %2 models did not get their automation value")
			.arg(wrong).arg(m_models.size())));
	}

private:
	static float expected(int i) { return static_cast<float>(i % 100); }

	AutomationTrack m_track;
	std::vector<std::unique_ptr<FloatModel>> m_models;
	std::vector<std::unique_ptr<AutomationClip>> m_clips;
};

} // namespace

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

	//! An absolute clip drives a model: the value is set, and no period allocates. The clip is flat on purpose:
	//! AutomatableModel::setValue allocates once per CHANGED value (upstream, outside this read), so a moving
	//! value would count that and not the read.
	void aPeriodWithAbsoluteAutomationAllocatesNothing()
	{
		Song* song = Engine::getSong();
		FloatModel absolute{0.0f, 0.0f, 100.0f, 1.0f};

		AutomationTrack track(song);
		AutomationClip clip(&track);
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(0, 40.0, false);
		clip.putValue(4000, 40.0, false);
		clip.movePosition(0);
		clip.addObject(&absolute);

		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 8), 0ull);
		QVERIFY2(std::fabs(absolute.value() - 40.0f) < 0.5f, qPrintable(QString::number(absolute.value())));
		song->stop();
	}

	//! A relative clip 25 above the middle of the model's 0..100 range lifts the model's own 50 to 75.
	void aRelativeClipOffsetsItsModel()
	{
		Song* song = Engine::getSong();
		FloatModel relative{50.0f, 0.0f, 100.0f, 1.0f};

		AutomationTrack track(song);
		AutomationClip clip(&track);
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(0, 75.0, false);
		clip.putValue(4000, 75.0, false);
		clip.movePosition(0);
		clip.setRelative(true);
		clip.addObject(&relative);

		song->playSong();
		song->processNextBuffer();
		QVERIFY2(std::fabs(relative.value() - 75.0f) < 0.5f, qPrintable(QString::number(relative.value())));
		QCOMPARE(song->automatedValuesAt(TimePos(10)).value(&relative), 75.0f);
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

	//! More automation clips than the scratch has room for: the values applied are still every clip's, first
	//! through the allocating redo, then - once the scratch is sized for the project - with no allocation.
	void aProjectLargerThanTheScratchStillAppliesEveryClip()
	{
		Song* song = Engine::getSong();
		constexpr int Clips = 200;
		Fixture project(song, Clips);
		song->playSong();
		song->resizeAutomationScratchForTest(8);  // last: building the fixture and starting the song size the scratch
		const auto overflowsBefore = song->automationScratchOverflows();
		for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }
		QVERIFY2(song->automationScratchOverflows() > overflowsBefore, "the test never overflowed the scratch");
		project.verifyApplied();

		// Sizing the scratch (what every engine change does) ends the redo, and then nothing allocates.
		project.resetModels();
		song->ensureAutomationScratchCapacity();
		const auto overflowsSized = song->automationScratchOverflows();
		QCOMPARE(allocationsOverPeriods(song, 8), 0ull);
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflowsSized));
		project.verifyApplied();
		song->stop();
	}

	//! The same at the size that used to be the hard cap (16384 clips): every model still gets its value.
	void aProjectPastTheOldClipCapStillAppliesEveryClip()
	{
		Song* song = Engine::getSong();
		constexpr int Clips = 16384 + 300;
		Fixture project(song, Clips);
		song->playSong();
		song->resizeAutomationScratchForTest(AutomationRenderScratch::RenderCapacity);
		for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }
		project.verifyApplied();

		song->ensureAutomationScratchCapacity();
		const auto overflows = song->automationScratchOverflows();
		project.resetModels();
		QCOMPARE(allocationsOverPeriods(song, 4), 0ull);
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflows));
		project.verifyApplied();
		song->stop();
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
		{
			// As in AudioEngine::renderNextPeriod: inside the scope a model change takes no lock and runs no
			// control-thread bookkeeping (AutomatableModel's change guard checks RealtimeScope::isActive()).
			const RealtimeScope realtime;
			for (int i = 0; i < periods; ++i) { song->processNextBuffer(); }
		}
		test::tlCountAllocations = false;
		return test::tlAllocationCount;
	}
};

QTEST_GUILESS_MAIN(AutomationRenderAllocationTest)
#include "AutomationRenderAllocationTest.moc"
