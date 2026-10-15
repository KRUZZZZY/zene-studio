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
#include <QDomDocument>

#include <chrono>
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

	~Fixture() { m_second.clear(); m_clips.clear(); }  // before the models they point at and the track that owns their list

	//! Gives every model a second clip that points at it too: two targets per model.
	void addSecondTargets()
	{
		for (std::size_t i = 0; i < m_models.size(); ++i)
		{
			auto clip = std::make_unique<AutomationClip>(&m_track);
			clip->addObject(m_models[i].get());
			m_second.push_back(std::move(clip));
		}
	}

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
	std::vector<std::unique_ptr<AutomationClip>> m_second;
};

//! ONE flat clip (value 40) whose targets are added one by one, past the scratch's reserve.
class ManyTargets
{
public:
	ManyTargets(Song* song, int targets) : m_song(song), m_track(song)
	{
		makeClip();
		for (int i = 0; i < targets; ++i)
		{
			m_models.push_back(std::make_unique<FloatModel>(0.0f, 0.0f, 100.0f, 1.0f));
			m_clip->addObject(m_models.back().get(), false);  // distinct models: skip the O(n) duplicate search
		}
	}

	~ManyTargets() { m_copy.reset(); m_clip.reset(); }

	AutomationClip& clip() { return *m_clip; }

	//! What undo, redo and project load do: save the clip's state, drop it, build a fresh one from the state.
	void restoreClipFromSavedState()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("clip"));
		doc.appendChild(root);
		m_clip->saveSettings(doc, root);
		m_clip.reset();
		m_clip = std::make_unique<AutomationClip>(&m_track);
		m_clip->loadSettings(root);
		AutomationClip::resolveAllIDs();  // the second half of every load and undo: ids -> targets
	}

	void addCopyOfClip() { m_copy.reset(m_clip->clone()); }

	void verifyApplied() const
	{
		int wrong = 0;
		for (const auto& model : m_models) { if (std::fabs(model->value() - 40.0f) > 0.5f) { ++wrong; } }
		QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 of %2 targets were not applied").arg(wrong).arg(m_models.size())));
	}

	void verifyAllEqual() const
	{
		const float first = m_models.front()->value();
		int different = 0;
		for (const auto& model : m_models) { if (model->value() != first) { ++different; } }
		QVERIFY2(different == 0, qPrintable(QStringLiteral("%1 targets differ from the first").arg(different)));
	}

private:
	void makeClip()
	{
		m_clip = std::make_unique<AutomationClip>(&m_track);
		m_clip->setProgressionType(AutomationClip::ProgressionType::Discrete);
		m_clip->putValue(0, 40.0, false);
		m_clip->putValue(4000, 40.0, false);
		m_clip->movePosition(0);
	}

	Song* m_song;
	AutomationTrack m_track;
	std::vector<std::unique_ptr<FloatModel>> m_models;
	std::unique_ptr<AutomationClip> m_clip;
	std::unique_ptr<AutomationClip> m_copy;
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

	//! Last resort only: a read that does overflow (forced here) is redone and still applies every clip.
	void anOverflowingReadIsRedoneAndStillAppliesEveryClip()
	{
		Song* song = Engine::getSong();
		Fixture project(song, 200);
		song->playSong();
		song->resizeAutomationScratchForTest(8);
		const auto overflowsBefore = song->automationScratchOverflows();
		for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }
		QVERIFY2(song->automationScratchOverflows() > overflowsBefore, "the test never overflowed the scratch");
		project.verifyApplied();
		song->stop();
		song->resizeAutomationScratchForTest(AutomationRenderScratch::RenderCapacity);
	}

	//! Targets added to an EXISTING clip past the scratch's capacity: the real edit path, no manual resize. The
	//! next periods apply every target, allocate nothing inside the RealtimeScope, and never take the redo.
	void targetsAddedToAnExistingClipPastTheCapacityAreAllApplied()
	{
		Song* song = Engine::getSong();
		const auto overflowsBefore = song->automationScratchOverflows();
		ManyTargets project(song, AutomationRenderScratch::RenderCapacity + 700);
		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 6), 0ull);
		project.verifyApplied();
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflowsBefore));
		song->stop();
	}

	//! The restore path undo, redo and project load run (a clip rebuilt from its saved state) grows it the same way.
	void aClipRestoredFromItsSavedStateIsAppliedInFull()
	{
		Song* song = Engine::getSong();
		const auto overflowsBefore = song->automationScratchOverflows();
		ManyTargets project(song, AutomationRenderScratch::RenderCapacity + 1200);
		project.restoreClipFromSavedState();
		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 6), 0ull);
		project.verifyApplied();
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflowsBefore));
		song->stop();
	}

	//! A copy of a clip carries its targets: copying a big clip must size the scratch for both.
	void aCopiedClipDoublesItsTargetsAndStillFits()
	{
		Song* song = Engine::getSong();
		const auto overflowsBefore = song->automationScratchOverflows();
		ManyTargets project(song, AutomationRenderScratch::RenderCapacity / 2 + 200);
		project.addCopyOfClip();
		song->playSong();
		QCOMPARE(allocationsOverPeriods(song, 6), 0ull);
		project.verifyApplied();
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflowsBefore));
		song->stop();
	}

	//! After automation recording ran over a project with this many targets, the following periods still apply
	//! every target without allocating. (The recording pass itself writes nodes into the clip, which is outside
	//! this read and is not counted.)
	void afterAutomationRecordingThePeriodsStillFit()
	{
		Song* song = Engine::getSong();
		const auto overflowsBefore = song->automationScratchOverflows();
		ManyTargets project(song, AutomationRenderScratch::RenderCapacity + 300);
		song->playSong();
		project.clip().setRecording(true);
		for (int i = 0; i < 4; ++i) { song->processNextBuffer(); }
		project.clip().setRecording(false);
		QCOMPARE(allocationsOverPeriods(song, 6), 0ull);
		project.verifyAllEqual();
		QCOMPARE(static_cast<qulonglong>(song->automationScratchOverflows()), static_cast<qulonglong>(overflowsBefore));
		song->stop();
	}

	//! Cost of the bookkeeping on a project of ~10k clips / ~20k targets: the per-edit fast path, and the one
	//! exact recount that runs once per doubling. Printed; the bounds only catch a return to O(project) per edit.
	void theBookkeepingCostsMicrosecondsPerEdit()
	{
		Song* song = Engine::getSong();
		Fixture base(song, 10000);
		base.addSecondTargets();  // ~20k targets in total
		using Clock = std::chrono::steady_clock;

		song->ensureAutomationScratchCapacity();  // settle: reserve for the project as it is
		const auto fastStart = Clock::now();
		constexpr int Calls = 200000;
		for (int i = 0; i < Calls; ++i) { song->noteAutomationGrowth(i & 1, (i >> 1) & 1); }
		const double fastNs = std::chrono::duration<double, std::nano>(Clock::now() - fastStart).count() / Calls;

		const auto recountStart = Clock::now();
		song->ensureAutomationScratchCapacity();
		const double recountMs = std::chrono::duration<double, std::milli>(Clock::now() - recountStart).count();

		std::size_t clips = 0;
		std::size_t targets = 0;
		song->countAutomationForTest(&clips, &targets);
		qInfo("HOOK_TIMING project=%zu clips / %zu targets: noteAutomationGrowth %.1f ns per edit (fast path), "
			"exact recount %.3f ms (once per doubling)", clips, targets, fastNs, recountMs);
		QVERIFY2(fastNs < 5000.0, qPrintable(QString::number(fastNs)));
		QVERIFY2(recountMs < 200.0, qPrintable(QString::number(recountMs)));
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
