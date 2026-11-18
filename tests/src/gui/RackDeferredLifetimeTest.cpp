/*
 * RackDeferredLifetimeTest.cpp - a rack's queued removal of a view that is already gone does nothing
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

/*! m0 blocker, cluster trackview-uaf follow-up (2026-10-08): EffectRackView and ControllerRackView connect a view's
 *  removal signal to their slot with a queued connection, and the signal carries the raw view pointer. A view removed
 *  in the event-loop turn it was asked to go (its chain cleared, its controller removed by the song) left the queued
 *  call holding a dead pointer: a dereference in EffectRackView::deletePlugin() or ControllerRackView::deleteController().
 *  Each race case removes the view before the events run, then runs them: nothing is touched. The normal path (no
 *  race) still removes the plugin or controller on the next turn. */

#include <QtTest>

#include <algorithm>

#include "ControllerRackView.h"
#include "ControllerView.h"
#include "Controller.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectRackView.h"
#include "EffectView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LfoController.h"
#include "MainWindow.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! One turn of the event loop: queued calls, then the deferred deletions of closed views, then timers.
void runEvents()
{
	QCoreApplication::sendPostedEvents();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents();
}

int controllerViewCount()
{
	return getGUI()->getControllerRackView()->findChildren<ControllerView*>().size();
}

ControllerView* controllerViewOf(const Controller* controller)
{
	for (ControllerView* view : getGUI()->getControllerRackView()->findChildren<ControllerView*>())
	{
		if (view->getController() == controller) { return view; }
	}
	return nullptr;
}

EffectView* effectViewOf(const EffectRackView* rack, const Effect* effect)
{
	for (EffectView* view : rack->findChildren<EffectView*>())
	{
		if (view->effect() == effect) { return view; }
	}
	return nullptr;
}

} // namespace

class RackDeferredLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's amplifier module, for the effect cases
#endif
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		QVERIFY(getGUI()->getControllerRackView() != nullptr);
		getGUI()->mainWindow()->show();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The song removes a controller in the turn its view asked to remove it: the queued removal does nothing.
	void aControllerRemovedBeforeItsQueuedRemovalRunsIsLeftAlone()
	{
		Song* song = Engine::getSong();
		const int viewsBefore = controllerViewCount();
		const auto controllersBefore = song->controllers().size();
		auto* controller = new LfoController(song);
		song->addController(controller);
		ControllerView* view = controllerViewOf(controller);
		QVERIFY(view != nullptr);
		view->removeController();                          // emits removedController(view): queued
		song->removeController(controller);                // the rack deletes the view; the song deletes the controller
		runEvents();                                       // red: deleteController() reads the dead view
		QCOMPARE(controllerViewCount(), viewsBefore);
		QCOMPARE(song->controllers().size(), controllersBefore);
	}

	//! A controller whose view asked to remove it is removed on the next turn (the normal path).
	void aControllerRemovedFromItsViewIsRemovedOnTheNextTurn()
	{
		Song* song = Engine::getSong();
		const int viewsBefore = controllerViewCount();
		const auto controllersBefore = song->controllers().size();
		auto* controller = new LfoController(song);
		song->addController(controller);
		ControllerView* view = controllerViewOf(controller);
		QVERIFY(view != nullptr);
		view->removeController();
		runEvents();
		QCOMPARE(controllerViewCount(), viewsBefore);
		QCOMPARE(song->controllers().size(), controllersBefore);
	}

	//! The chain is cleared in the turn its plugin's view asked to remove it: the queued removal does nothing.
	void anEffectRemovedBeforeItsQueuedRemovalRunsIsLeftAlone()
	{
		auto* chain = new EffectChain(nullptr);
		auto* rack = new EffectRackView(chain, nullptr);
		Effect* effect = Effect::instantiate(QStringLiteral("amplifier"), chain, nullptr);
		QVERIFY(effect != nullptr);
		chain->appendEffect(effect);
		EffectView* view = effectViewOf(rack, effect);
		QVERIFY(view != nullptr);
		view->deletePlugin();   // emits deletedPlugin(view): queued
		chain->clear();         // the rack deletes its views and the chain deletes the effects, this turn
		runEvents();            // red: deletePlugin() reads view->effect() on the freed view
		QCOMPARE(chain->effects().size(), std::size_t{0});
		QCOMPARE(rack->findChildren<EffectView*>().size(), 0);
		delete rack;
		delete chain;
	}

	//! A plugin whose view asked to remove it is removed on the next turn (the normal path).
	void anEffectRemovedFromItsViewIsRemovedOnTheNextTurn()
	{
		auto* chain = new EffectChain(nullptr);
		auto* rack = new EffectRackView(chain, nullptr);
		Effect* effect = Effect::instantiate(QStringLiteral("amplifier"), chain, nullptr);
		QVERIFY(effect != nullptr);
		chain->appendEffect(effect);
		EffectView* view = effectViewOf(rack, effect);
		QVERIFY(view != nullptr);
		view->deletePlugin();
		runEvents();
		QCOMPARE(chain->effects().size(), std::size_t{0});
		QCOMPARE(rack->findChildren<EffectView*>().size(), 0);
		delete rack;
		delete chain;
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(RackDeferredLifetimeTest)
#include "RackDeferredLifetimeTest.moc"
