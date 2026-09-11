/*
 * MidiLearnThreadTest.cpp - proves the MIDI-learn binding is written on the GUI
 *                           thread, never on the MIDI input thread.
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

#include <QtTest>

#include <thread>

#include "AutomatableModel.h"
#include "AudioEngine.h"
#include "Controller.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "MidiLearnGui.h"
#include "MidiPort.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A synthetic hardware CC, exactly the event the MIDI clients hand to
//! MidiLearn::handleMidiEvent (see MidiClientRaw::processParsedEvent and the
//! SND_SEQ_EVENT_CONTROLLER case of MidiAlsaSeq::run).
MidiEvent makeCc(int channel, int controllerNumber, int value)
{
	return MidiEvent(MidiControlChange, channel, controllerNumber, value);
}

//! The bound controller of a model, or null when the model is unbound.
MidiController* boundMidiController(const AutomatableModel* model)
{
	auto* connection = model->controllerConnection();
	if (connection == nullptr) { return nullptr; }
	return dynamic_cast<MidiController*>(connection->getController());
}

//! What a delivery from the MIDI input thread produced.
struct Delivery
{
	Qt::HANDLE threadId;
	bool handled;
};

//! Deliver a CC the way the MIDI clients do: from a thread that is not the GUI
//! thread. std::thread rather than QThread because the real input paths are not
//! all Qt threads - MidiJack delivers from a JACK callback thread.
Delivery deliverFromMidiInputThread(const MidiEvent& event)
{
	Delivery delivery{nullptr, false};
	std::thread inputThread([&delivery, &event]() {
		delivery.threadId = QThread::currentThreadId();
		delivery.handled = MidiLearn::instance()->handleMidiEvent(event);
	});
	inputThread.join();
	return delivery;
}

//! Thread id as an integer, for QCOMPARE diagnostics.
quintptr threadIdOf(Qt::HANDLE handle)
{
	return reinterpret_cast<quintptr>(handle);
}

} // namespace

class MidiLearnThreadTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void cleanup()
	{
		// Never leak an armed learn, a pending request, a dangling action or a
		// dangling focus target into the next test.
		MidiLearnGui::instance()->setArmed(false);
		MidiLearnGui::instance()->setAction(nullptr);
		MidiLearn::instance()->setFocusTarget(nullptr);
		MidiLearn::instance()->setEnabled(false);
		MidiLearn::instance()->applyPendingBinding();
	}

	// The race, as a test. A control is focused and learn mode is armed on the
	// GUI thread; the CC that completes the learn arrives through
	// MidiLearn::handleMidiEvent() from another thread, which is what
	// MidiAlsaSeq::run() and MidiClientRaw::processParsedEvent() do.
	//
	// The assertions after the join are the proof: when the MIDI input thread
	// returns, the model has NO controller connection and the request is still
	// pending. If the write were still on the input thread, the first of those
	// two would fail - so this test fails against the code it replaces.
	void BindingIsCreatedOnTheGuiThreadNotTheMidiInputThread()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		const unsigned int before = MidiLearn::instance()->bindingCount();
		const Qt::HANDLE seamBefore = MidiLearn::instance()->lastBindingThreadId();

		// Arm exactly as the Edit menu does, so the production delivery path
		// (MidiLearnGui's GUI-thread timer) is the one under test, and register
		// the target the way the GUI focus filter does.
		MidiLearnGui::instance()->setArmed(true);
		MidiLearn::instance()->setFocusTarget(&target);

		QVERIFY(MidiLearn::instance()->isEnabled());
		QVERIFY(target.controllerConnection() == nullptr);

		// channel 2, controller 74, value 127
		const Delivery delivery = deliverFromMidiInputThread(makeCc(2, 74, 127));
		QVERIFY(delivery.threadId != QThread::currentThreadId());

		// Negative control (inverted): with MIDILEARN_RACE_CONTROL_INVERTED set,
		// this test asserts the *pre-fix* behaviour - the write happening on the
		// MIDI input thread - and therefore MUST fail. It is how the assertions
		// below are shown to be able to fail at all.
		if (qEnvironmentVariableIsSet("MIDILEARN_RACE_CONTROL_INVERTED"))
		{
			QVERIFY(target.controllerConnection() != nullptr);
		}
		else
		{
			// The MIDI input thread is back and has written nothing: no
			// connection on the model, no MidiController built, no binding
			// counted. The data (channel / controller number) is all it left.
			QVERIFY(target.controllerConnection() == nullptr);
			QVERIFY(MidiLearn::instance()->hasPendingBinding());
			QCOMPARE(MidiLearn::instance()->bindingCount(), before);
			QVERIFY(MidiLearn::instance()->lastBindingThreadId() == seamBefore);
		}

		// The GUI thread delivers it, on the production timer - no direct call
		// into the drain, so this is the path a real learn takes.
		QTRY_VERIFY(target.controllerConnection() != nullptr);

		// ...and the write itself happened here, on this thread. The seam is
		// MidiLearn::lastBindingThreadId(), set inside the bind.
		QCOMPARE(threadIdOf(MidiLearn::instance()->lastBindingThreadId()),
			threadIdOf(QThread::currentThreadId()));
		QVERIFY(MidiLearn::instance()->lastBindingThreadId() != delivery.threadId);
		QCOMPARE(MidiLearn::instance()->bindingCount(), before + 1);
		QVERIFY(!MidiLearn::instance()->hasPendingBinding());

		// The deferred bind built the same binding the immediate path builds:
		// channel 2 becomes 3 in MidiPort's 1-based numbering.
		auto* controller = boundMidiController(&target);
		QVERIFY(controller != nullptr);
		QVERIFY(controller->type() == Controller::ControllerType::Midi);
		QCOMPARE(controller->midiPort().inputChannel(), 3);
		QCOMPARE(controller->midiPort().inputController(), 74);

		// One-shot: the learn bound one control and disarmed itself.
		QVERIFY(!MidiLearn::instance()->isEnabled());
		QVERIFY(MidiLearn::instance()->focusTarget() == nullptr);

		// And the connection the GUI thread built is live: a CC on the bound
		// controller reaches the model, one on another controller does not.
		QSignalSpy spy(&target, SIGNAL(dataChanged()));
		controller->midiPort().processInEvent(makeCc(2, 74, 100), TimePos());
		QCOMPARE(spy.count(), 1);
		controller->midiPort().processInEvent(makeCc(2, 75, 100), TimePos());
		QCOMPARE(spy.count(), 1);
	}

	// The lifetime question. The deferred bind runs later, so the control the
	// user focused can be gone by then - project closed, track deleted, plugin
	// unloaded - all of which are GUI-thread actions that happen between the CC
	// and the drain.
	//
	// Nothing in the hand-off keeps that object alive, and nothing needs to: the
	// MIDI input thread is never told which object to touch, so the only pointer
	// involved is the GUI thread's own QPointer. When the model dies the
	// QPointer nulls itself, the drain finds no target and drops the request.
	void TargetGoneBeforeTheDeferredBindDropsTheRequest()
	{
		const unsigned int before = MidiLearn::instance()->bindingCount();
		const Qt::HANDLE seamBefore = MidiLearn::instance()->lastBindingThreadId();

		// Heap-allocated so the test can destroy it at the point it chooses.
		auto* target = new FloatModel(0.5f, 0.0f, 1.0f, 0.01f);

		// Arm without the GUI timer: this test controls exactly when the drain
		// runs, so it can put the model's death in between.
		MidiLearn::instance()->setFocusTarget(target);
		MidiLearn::instance()->setEnabled(true);

		const Delivery delivery = deliverFromMidiInputThread(makeCc(5, 21, 64));
		QVERIFY(delivery.threadId != QThread::currentThreadId());
		QVERIFY(target->controllerConnection() == nullptr);
		QVERIFY(MidiLearn::instance()->hasPendingBinding());

		// The track/control goes away before the deferred bind runs.
		delete target;

		// The GUI side already knows the target is gone: the QPointer the
		// MIDI-thread hand-off never received has been nulled by ~QObject.
		QVERIFY(MidiLearn::instance()->focusTarget() == nullptr);

		// The drain therefore drops the request instead of writing through a
		// dead pointer: no bind, no crash, nothing counted.
		QVERIFY(!MidiLearn::instance()->applyPendingBinding());
		QVERIFY(!MidiLearn::instance()->hasPendingBinding());
		QCOMPARE(MidiLearn::instance()->bindingCount(), before);
		QVERIFY(MidiLearn::instance()->lastBindingThreadId() == seamBefore);
	}
};

QTEST_GUILESS_MAIN(MidiLearnThreadTest)
#include "MidiLearnThreadTest.moc"
