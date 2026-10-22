/*
 * AutomationClipDisconnectTest.cpp - disconnect waits for an in-flight automation render
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QAction>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationClipView.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

AutomationClipView* viewOf(const AutomationClip* clip)
{
	for (TrackView* trackView : getGUI()->songEditor()->m_editor->trackViews())
	{
		for (AutomationClipView* view : trackView->findChildren<AutomationClipView*>())
		{
			if (view->getClip() == clip) { return view; }
		}
	}
	return nullptr;
}

template <typename ClipType>
bool removeObjectIfAvailable(ClipType& clip, AutomatableModel* model, std::atomic<bool>& available)
{
	if constexpr (requires { { clip.removeObject(model) } -> std::same_as<bool>; })
	{
		available.store(true, std::memory_order_release);
		return clip.removeObject(model);
	}
	else
	{
		available.store(false, std::memory_order_release);
		return false;
	}
}

//! The rendezvous of the target-change seam (AutomationClip's test seam).
//!
//! The seam is called twice per target-list change:
//! - Before: the thread is inside the change, before it takes the engine's
//!   change guard (the lock the render thread holds for a whole period). The
//!   probe PARKS the thread there until the test lets it go, so the test knows
//!   the thread is inside the change and not merely late.
//! - Holding: the thread is HOLDING ON - the acquisition of the engine's
//!   change guard was CONTENDED: the test thread held the same engine change
//!   lock that renderNextPeriod() holds when the acquisition was attempted.
//!   The change therefore waits for the test thread to release that lock.
//!   The report is made AT the acquisition, on the acquiring thread, so it is
//!   positive evidence that the thread really reached the guard.
//!
//! The test asserts on the OBSERVED report, not on a timeout: a thread that
//! never reached the guard reports nothing, and a change that took the guard
//! while the render held it reports nothing either - both fail the same way.
std::mutex s_probeMutex;
std::condition_variable s_probeCv;
bool s_atBefore = false;
bool s_contended = false;
bool s_allowProbe = false;

void targetChangeProbe(AutomationClip::TargetChangePoint point)
{
	std::unique_lock lock(s_probeMutex);
	if (point == AutomationClip::TargetChangePoint::Before)
	{
		s_atBefore = true;
		s_probeCv.notify_all();
		s_probeCv.wait(lock, [] { return s_allowProbe; });
		return;
	}
	s_contended = true;
	s_probeCv.notify_all();
}

void installProbe()
{
	{
		std::lock_guard lock(s_probeMutex);
		s_atBefore = false;
		s_contended = false;
		s_allowProbe = false;
	}
	AutomationClip::setTargetChangeProbeForTest(&targetChangeProbe);
}

void removeProbe()
{
	AutomationClip::setTargetChangeProbeForTest(nullptr);
	{
		std::lock_guard lock(s_probeMutex);
		s_allowProbe = true;
	}
	s_probeCv.notify_all();
}

//! Wait for the changing thread to reach the seam. The window is a DEADLOCK
//! GUARD, not the proof: the proof is the report the thread makes, and a
//! thread that never arrives simply fails the assertion below.
bool awaitBefore()
{
	std::unique_lock lock(s_probeMutex);
	return s_probeCv.wait_for(lock, std::chrono::seconds(30), [] { return s_atBefore; });
}

//! Wait for the report of a contended acquisition of the engine's change
//! guard. The test thread is holding it; the window is a deadlock guard, and
//! the report is the proof.
bool awaitContended()
{
	std::unique_lock lock(s_probeMutex);
	return s_probeCv.wait_for(lock, std::chrono::seconds(30), [] { return s_contended; });
}

//! Let the parked thread continue towards the guard.
void releaseProbe()
{
	{
		std::lock_guard lock(s_probeMutex);
		s_allowProbe = true;
	}
	s_probeCv.notify_all();
}

} // namespace

class AutomationClipDisconnectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		getGUI()->mainWindow()->show();
	}

	void cleanupTestCase() { Engine::destroy(); }

	/*! A target that is connected while the render thread is walking this clip's
	 *  target list must WAIT for that walk: the list is read through a
	 *  reference (objects() releases the clip mutex before the reader uses it),
	 *  so a push_back() that reallocates it under the reader's feet is the
	 *  defect. The engine change guard is what makes the connection wait.
	 *
	 *  The proof is an OBSERVATION, not a timeout: the test thread holds the
	 *  engine change lock also held by renderNextPeriod(), so the acquisition is
	 *  CONTENDED; the seam reports that contention at the attempt.
	 *  A connection that did not take the guard at all reports nothing and
	 *  fails here.
	 */
	void addObjectWaitsForRenderPublication()
	{
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		FloatModel target{0.5f, 0.0f, 1.0f, 0.01f};

		installProbe();
		std::atomic<bool> published = false;
		std::thread adder;
		bool reached = false;
		bool contended = false;
		{
			// The engine change lock used by renderNextPeriod(), taken here by
			// the test thread so the test can release it at the moment it chooses.
			auto renderLock = std::optional<AudioEngine::RequestChangesGuard>{};
			renderLock.emplace(Engine::audioEngine()->requestChangesGuard());
			adder = std::thread([&]
			{
				clip->addObject(&target, false);
				published.store(true, std::memory_order_release);
			});
			// The connecting thread is parked inside the change now.
			reached = awaitBefore();
			// Let it attempt the test-held engine change lock.
			releaseProbe();
			if (reached)
			{
				// It must find this test thread holding the lock: that contention
				// is reported at the acquisition.
				contended = awaitContended();
			}
			// The test thread releases the lock: the change may now finish.
			renderLock.reset();
		}
		adder.join();
		removeProbe();

		QVERIFY2(reached, "the connecting thread never reached the target change");
		QVERIFY2(contended,
			"connecting a target never reported a contended acquisition of the engine's change guard: "
			"it either went on while the render held the lock, or it never reached the guard");
		QVERIFY(published.load(std::memory_order_acquire));
		QCOMPARE(clip->objects().size(), std::size_t(1));
	}

	void removeObjectWaitsForRenderPublication()
	{
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		FloatModel target{0.5f, 0.0f, 1.0f, 0.01f};
		clip->addObject(&target, false);

		std::atomic<bool> started = false;
		std::atomic<bool> apiAvailable = false;
		std::promise<bool> removalResult;
		auto result = removalResult.get_future();
		std::thread remover;
		bool remainedPendingWhileRenderHeld = false;
		{
			const auto renderGuard = Engine::audioEngine()->requestChangesGuard();
			remover = std::thread([&]
			{
				started.store(true, std::memory_order_release);
				removalResult.set_value(removeObjectIfAvailable(*clip, &target, apiAvailable));
			});
			while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }
			remainedPendingWhileRenderHeld = result.wait_for(std::chrono::milliseconds(250)) == std::future_status::timeout;
		}
		remover.join();

		QVERIFY2(apiAvailable.load(std::memory_order_acquire),
			"AutomationClip must expose target removal through its synchronized API");
		QVERIFY2(remainedPendingWhileRenderHeld,
			"target removal returned while render publication still held the engine change guard");
		QVERIFY(result.get());
		QVERIFY(clip->objects().empty());
	}

	void disconnectActionRemovesTarget()
	{
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		FloatModel target{0.5f, 0.0f, 1.0f, 0.01f};
		clip->addObject(&target, false);

		QTRY_VERIFY_WITH_TIMEOUT(viewOf(clip) != nullptr, 5000);
		QAction disconnect;
		disconnect.setData(target.id());
		QVERIFY(QMetaObject::invokeMethod(viewOf(clip), "disconnectObject", Qt::DirectConnection,
			Q_ARG(QAction*, &disconnect)));
		QVERIFY(clip->objects().empty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(AutomationClipDisconnectTest)

#include "AutomationClipDisconnectTest.moc"
