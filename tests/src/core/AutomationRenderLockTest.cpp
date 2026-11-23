/*
 * AutomationRenderLockTest.cpp - the audio thread never waits on an automation clip's lock
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

// Cluster new-p12-248: Song::buildAutomationRamps() and the per-tick automation
// read run once per audio block, and each used to take every automation clip's
// own mutex with a blocking lock. A control-thread edit of that clip (a node
// put, a drag, a removal) holds the same mutex, so playback waited for the
// edit. The render now asks for the lock without waiting and leaves a busy
// clip out of that block or tick.
//
// The edit is REAL, not a seam: AutomationClip::putValue() emits dataChanged()
// inside its own m_clipMutex scope, and a direct slot parks the editing thread
// there, so the lock is held exactly the way an editor holds it. The render
// read runs on its own thread; the only bound in this file is the escape hatch
// for the unfixed build, whose read never returns while the edit holds the
// lock. With the fix the read does not wait at all, so the result does not
// depend on timing.

#include <QtTest>

#include <chrono>
#include <future>
#include <thread>

#include "AudioEngine.h"
#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "AutomationTrack.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr float kInit = 0.5f;
constexpr float kNodeA = 0.1f;   // tick 0
constexpr float kNodeB = 0.9f;   // tick 200

thread_local bool t_editingThread = false;

//! A curve clip on @a track driving @a model: a linear ramp from tick 0 to tick 200.
AutomationClip* curveClip(AutomationTrack& track, AutomatableModel& model)
{
	auto* clip = new AutomationClip(&track);
	clip->setProgressionType(AutomationClip::ProgressionType::Linear);
	clip->movePosition(0);
	clip->addObject(&model);
	clip->putValue(TimePos(0), kNodeA, false, true);
	clip->putValue(TimePos(200), kNodeB, false, true);
	clip->setSampleAccurate(true);
	return clip;
}

//! A control-thread edit of @a clip, parked while it holds the clip's lock.
class EditHoldingTheClipLock
{
public:
	explicit EditHoldingTheClipLock(AutomationClip* clip)
		: m_released(m_release.get_future().share())
	{
		std::future<void> holding = m_holding.get_future();
		m_connection = QObject::connect(clip, &Model::dataChanged, clip, [this] {
			if (!t_editingThread) { return; }
			m_holding.set_value();
			m_released.wait();
		}, Qt::DirectConnection);
		// The same value at an existing node: the edit holds the lock, the curve stays as it is.
		m_thread = std::thread([clip] {
			t_editingThread = true;
			clip->putValue(TimePos(200), kNodeB, false, true);
		});
		holding.wait();
	}
	~EditHoldingTheClipLock() { release(); }

	void release()
	{
		if (!m_thread.joinable()) { return; }
		m_release.set_value();
		m_thread.join();
		QObject::disconnect(m_connection);
	}

private:
	std::promise<void> m_holding;
	std::promise<void> m_release;
	std::shared_future<void> m_released;
	QMetaObject::Connection m_connection;
	std::thread m_thread;
};

/*! Runs @a read on a render-side thread while @a edit holds the clip's lock and
 *  reports whether it returned before the edit let go. The bound only rescues
 *  the unfixed build, whose read waits until release(); a fixed read never waits. */
template<class Read>
bool returnsWhileTheEditHolds(EditHoldingTheClipLock& edit, Read read)
{
	std::promise<void> finished;
	std::future<void> done = finished.get_future();
	std::thread reader([&read, &finished] {
		read();
		finished.set_value();
	});
	const bool returned = done.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
	edit.release();
	reader.join();
	return returned;
}

} // namespace


class AutomationRenderLockTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The tests drive the periods themselves; a real device would advance on its own.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! The finding itself: the per-block ramp publish skips the busy clip instead of waiting.
	//!
	//! The test drives the publish through an explicit three-state sequence, one period
	//! per state (the model's published ramp is only valid for the period it was built in):
	//!   1. baseline publish  - no edit, the block publishes the ramp;
	//!   2. edit-held skip    - a control-thread edit holds the clip lock, the publish
	//!                          returns without waiting and leaves NO ramp for the period;
	//!   3. released publish  - the edit is gone, the same block publishes the ramp again.
	void theRampPublishDoesNotWaitForAnEditedClip()
	{
		Song* song = Engine::getSong();
		FloatModel model{kInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = curveClip(track, model);
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();

		// State 1 of 3: baseline publish. With no edit, the block publishes the ramp.
		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(60), frames, 0);
		QVERIFY2(model.automationRamp() != nullptr,
			"state 1 (baseline publish): no ramp was published with no edit in progress");

		// State 2 of 3: edit-held skip. A real putValue() edit holds the clip's lock; the
		// publish must return without waiting and leave the period with no ramp.
		AutomatableModel::incrementPeriodCounter();
		{
			EditHoldingTheClipLock edit(clip);
			QVERIFY2(returnsWhileTheEditHolds(edit,
				[song, frames] { song->buildAutomationRamps(song->tracks(), TimePos(60), frames, 0); }),
				"state 2 (edit-held skip): buildAutomationRamps waited for the clip lock an edit held (audio-thread wait)");
		}
		QVERIFY2(model.automationRamp() == nullptr,
			"state 2 (edit-held skip): a ramp was published from a clip whose lock the render did not hold");

		// State 3 of 3: released publish. With the edit released, the same call publishes the ramp.
		AutomatableModel::incrementPeriodCounter();
		song->buildAutomationRamps(song->tracks(), TimePos(60), frames, 0);
		QVERIFY2(model.automationRamp() != nullptr,
			"state 3 (released publish): no ramp after the edit released");
	}

	//! The per-tick read of the same block (TrackContainer::collectAutomatedValues).
	void thePerTickReadDoesNotWaitForAnEditedClip()
	{
		Song* song = Engine::getSong();
		FloatModel model{kInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = curveClip(track, model);

		AutomatedValueMap held;
		{
			EditHoldingTheClipLock edit(clip);
			QVERIFY2(returnsWhileTheEditHolds(edit, [song, &held] { held = song->automatedValuesAt(TimePos(100)); }),
				"the per-tick automation read waited for the clip lock an edit held");
		}
		QVERIFY2(!held.contains(&model), "the busy clip was read without its lock");
		QVERIFY2(song->automatedValuesAt(TimePos(100)).contains(&model),
			"no value without an edit: the test is vacuous");
	}

	//! The relative (trim) clips' pass reads the curve and the range separately.
	void aRelativeClipReadDoesNotWaitForAnEditedClip()
	{
		Song* song = Engine::getSong();
		FloatModel model{kInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = curveClip(track, model);
		clip->setRelative(true);

		AutomatedValueMap held;
		{
			EditHoldingTheClipLock edit(clip);
			QVERIFY2(returnsWhileTheEditHolds(edit, [song, &held] { held = song->automatedValuesAt(TimePos(100)); }),
				"the relative-clip pass waited for the clip lock an edit held");
		}
		QVERIFY2(!held.contains(&model), "the busy relative clip was read without its lock");
		QVERIFY2(song->automatedValuesAt(TimePos(100)).contains(&model),
			"no relative value without an edit: the test is vacuous");
	}

	//! A whole period, the recording scan included, renders while an edit holds a clip it plays.
	void aWholePeriodDoesNotWaitForAnEditedClip()
	{
		Song* song = Engine::getSong();
		FloatModel model{kInit, 0.0f, 1.0f, 0.001f};
		AutomationTrack track(song);
		AutomationClip* clip = curveClip(track, model);

		song->playSong();
		song->getTimeline().setTicks(100);
		AutomatableModel::incrementPeriodCounter();
		bool returned = false;
		{
			EditHoldingTheClipLock edit(clip);
			returned = returnsWhileTheEditHolds(edit, [song] { song->processNextBuffer(); });
		}
		song->stop();
		QVERIFY2(returned, "a render period waited for the clip lock an edit held");
	}
};

QTEST_GUILESS_MAIN(AutomationRenderLockTest)
#include "AutomationRenderLockTest.moc"
