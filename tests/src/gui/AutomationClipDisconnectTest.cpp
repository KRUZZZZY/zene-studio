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
#include <future>
#include <thread>

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
