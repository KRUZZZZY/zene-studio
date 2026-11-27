/*
 * ControllerFrameNotificationTest.cpp - the per-period controller notification never reaches a
 *                                       GUI thread from the render thread (cluster new-p12-56)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING);
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

// Controller::triggerFrameCounter() runs at the end of every render period, on the render
// thread inside the realtime scope, and used to emit Controller::valueChanged() for every
// registered controller. The GUI does not listen to that signal directly: it listens to the
// model that the controller drives. The chain is all Qt::DirectConnection -
// Controller::valueChanged() -> ControllerConnection::valueChanged()
// (src/core/ControllerConnection.cpp:153) -> AutomatableModel::dataChanged()
// (src/core/AutomatableModel.cpp:744) - and the last hop, from dataChanged() to the widget's
// slot (e.g. FloatModelEditorBase::friendlyUpdate, src/gui/widgets/FloatModelEditorBase.cpp),
// is Qt::AutoConnection. The widget lives in the GUI thread, so that hop is queued and Qt
// allocates a QMetaCallEvent and appends it to the GUI thread's posted-event queue FROM THE
// RENDER THREAD, once per controller per period. That is an allocation on the audio thread,
// which AGENTS.md's realtime rule forbids.
//
// The fix keeps the render thread to one atomic publication and moves the notification to the
// control thread, which owns a timer. The tests below drive that timer through its object name
// (the same seam AutomationRenderLockTest and AutomationModesTest use for Song's drain timer)
// instead of waiting for it, so nothing here depends on wall-clock time: a period is published
// by calling triggerFrameCounter(), and a notification is delivered by invoking the timer's
// own timeout signal.

#include <QtTest>
#include <QCoreApplication>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>

#include <cstdint>

#include "AllocationProbe.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Controller.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "RealtimeScope.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! The name the control thread's notification timer is created with. A test finds the timer by
//! it (QObject::findChild) and fires it synchronously; production starts it with the first
//! controller and lets the event loop run it.
const QString kNotificationTimerName = QStringLiteral( "controllerFrameNotificationTimer" );

//! The GUI end of the chain: an object that can only be reached through the event queue, the
//! way the widget on the far side of AutomatableModel::dataChanged() is reached from the
//! render thread. Its slot counts deliveries, so a queued event is visible as a count and not
//! as a timer that did or did not fire.
class QueuedCounter final : public QObject
{
	Q_OBJECT
public:
	int delivered = 0;

	//! Delivers everything yet posted to this object, synchronously: a deterministic stand-in
	//! for the GUI thread's event loop turn.
	void drainPostedEvents() { QCoreApplication::sendPostedEvents( this ); }

public slots:
	void noteValueChanged() { ++delivered; }
};

//! The allocations \a body makes on this thread, counted by the probe (AllocationProbe.h).
template<typename Body>
auto countAllocations( Body&& body ) -> std::uint64_t
{
	test::resetAllocationCount();
	test::tlCountAllocations = true;
	body();
	test::tlCountAllocations = false;
	return test::tlAllocationCount;
}

} // namespace

class ControllerFrameNotificationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
		// The test drives every period itself: a device thread rendering in the background
		// would publish periods nobody counted.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	/*! The defect. One render period, with a GUI-thread receiver on the far side of the real
	 *  chain, must post no queued event and allocate nothing on the calling (render) thread.
	 *  Both measurements are of the same call: the queued event is the allocation. */
	void renderThreadPeriodPostsNoQueuedNotificationAndAllocatesNothing()
	{
		Controller probe( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Probe" ) );
		FloatModel knob( 0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral( "Frame Notification Knob" ) );
		// What setControllerConnection() builds: the model owns the connection and the
		// controller drives it. setControllerConnection() emits dataChanged() once itself.
		knob.setControllerConnection( new ControllerConnection( &probe ) );

		QueuedCounter gui;
		QObject::connect( &knob, &Model::dataChanged, &gui, &QueuedCounter::noteValueChanged,
			Qt::QueuedConnection );
		gui.drainPostedEvents();
		gui.delivered = 0;

		std::uint64_t allocations = 0;
		{
			// AudioEngine::renderStageMix() runs inside this scope on the render thread.
			const RealtimeScope realtime;
			allocations = countAllocations( [] {
				Controller::triggerFrameCounter();
			} );
		}
		gui.drainPostedEvents();

		QVERIFY2( gui.delivered == 0 && allocations == 0,
			qPrintable( QStringLiteral( "one render period posted %1 queued controller "
				"notification(s) to the GUI thread and allocated %2 time(s) on the render "
				"thread" ).arg( gui.delivered )
				.arg( static_cast<qulonglong>( allocations ) ) ) );

		// The period itself still advanced: the publication is what the notification reads.
		const f_cnt_t before = Controller::runningPeriods();
		Controller::triggerFrameCounter();
		QCOMPARE( Controller::runningPeriods(), before + 1 );
	}

	/*! The other half of the contract, and the reason the fix is not "delete the emit": the
	 *  GUI still learns that a controller value moved, from the control thread's own timer. */
	void theControlThreadTimerNotifiesTheGuiChain()
	{
		Controller probe( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Chain Probe" ) );
		FloatModel knob( 0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral( "Frame Notification Chain Knob" ) );
		knob.setControllerConnection( new ControllerConnection( &probe ) );

		QueuedCounter gui;
		QObject::connect( &knob, &Model::dataChanged, &gui, &QueuedCounter::noteValueChanged,
			Qt::QueuedConnection );

		QTimer* const timer = notificationTimer();
		QVERIFY2( timer != nullptr,
			"no control-thread notification timer was created for the controller" );
		QVERIFY( timer->isActive() );
		QVERIFY( timer->interval() > 0 );

		// Report whatever an earlier slot left pending, so this slot starts from a state the
		// control thread has already caught up with.
		QVERIFY( fireNotificationTimer( timer ) );
		gui.drainPostedEvents();
		gui.delivered = 0;

		// An idle engine: the timer fires but there is nothing new to report.
		QVERIFY( fireNotificationTimer( timer ) );
		gui.drainPostedEvents();
		QCOMPARE( gui.delivered, 0 );

		// One period, then the control thread's turn: exactly one notification reaches the GUI.
		Controller::triggerFrameCounter();
		QVERIFY( fireNotificationTimer( timer ) );
		gui.drainPostedEvents();
		QCOMPARE( gui.delivered, 1 );
	}

	/*! Throttling: the notification reports "the values moved" once per timer tick, not once
	 *  per period. Nothing is lost (the tick after the periods reports them) and nothing is
	 *  repeated (a tick with no new period reports nothing). */
	void oneTimerTickReportsEveryPeriodSinceTheLastTick()
	{
		Controller probe( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Throttle Probe" ) );
		QSignalSpy notified( &probe, SIGNAL(valueChanged()) );
		QTimer* const timer = notificationTimer();
		QVERIFY( timer != nullptr );

		QVERIFY( fireNotificationTimer( timer ) );  // start from a reported state
		notified.clear();

		Controller::triggerFrameCounter();
		QVERIFY( fireNotificationTimer( timer ) );
		QCOMPARE( notified.count(), 1 );

		QVERIFY( fireNotificationTimer( timer ) );  // no period since the last tick
		QCOMPARE( notified.count(), 1 );

		for( int period = 0; period < 3; ++period ) { Controller::triggerFrameCounter(); }
		QVERIFY( fireNotificationTimer( timer ) );
		QCOMPARE( notified.count(), 2 );
	}

	/*! The render thread must never run the notification, even if something calls the
	 *  dispatcher from inside the realtime scope: it would post a queued event per controller
	 *  per period again. The period stays pending and the control thread reports it. */
	void theRenderThreadNotifiesNothing()
	{
		Controller probe( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Render Thread Probe" ) );
		QSignalSpy notified( &probe, SIGNAL(valueChanged()) );
		QTimer* const timer = notificationTimer();
		QVERIFY( timer != nullptr );
		QVERIFY( fireNotificationTimer( timer ) );
		notified.clear();

		Controller::triggerFrameCounter();
		{
			const RealtimeScope realtime;
			QVERIFY( fireNotificationTimer( timer ) );
		}
		QCOMPARE( notified.count(), 0 );

		QVERIFY( fireNotificationTimer( timer ) );
		QCOMPARE( notified.count(), 1 );
	}

	/*! Reconfiguration: a controller destroyed between the period and the tick is not notified
	 *  (the dispatcher walks the live list), and the controllers that remain still are. */
	void aControllerDestroyedBeforeTheTickIsNotNotified()
	{
		Controller survivor( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Survivor" ) );
		QSignalSpy notified( &survivor, SIGNAL(valueChanged()) );
		QTimer* const timer = notificationTimer();
		QVERIFY( timer != nullptr );
		QVERIFY( fireNotificationTimer( timer ) );
		notified.clear();

		auto* doomed = new Controller( Controller::ControllerType::Lfo, Engine::getSong(),
			QStringLiteral( "Frame Notification Doomed" ) );
		QPointer<Controller> watch( doomed );
		delete doomed;
		QVERIFY( watch.isNull() );

		Controller::triggerFrameCounter();
		QVERIFY( fireNotificationTimer( timer ) );
		QCOMPARE( notified.count(), 1 );
	}

private:
	//! The control thread's notification timer, found the way production's own timers are
	//! found by this directory's tests (Song's automationWriteDrainTimer).
	static QTimer* notificationTimer()
	{
		return QCoreApplication::instance()->findChild<QTimer*>( kNotificationTimerName );
	}

	//! Runs one tick of the timer that production runs every few milliseconds. Returns false if
	//! the tick could not be delivered, so the caller can fail the test (QVERIFY inside this
	//! helper would only leave the helper).
	static bool fireNotificationTimer( QTimer* timer )
	{
		return QMetaObject::invokeMethod( timer, "timeout", Qt::DirectConnection );
	}
};

QTEST_GUILESS_MAIN( ControllerFrameNotificationTest )
#include "ControllerFrameNotificationTest.moc"
