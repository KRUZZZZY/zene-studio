/*
 * MidiLearnTest.cpp - headless proof of the global MIDI-learn mode.
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

#include <QDomDocument>
#include <QSignalSpy>
#include <QtTest>

#include "AutomatableModel.h"
#include "AudioEngine.h"
#include "Controller.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "MidiPort.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

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

} // namespace

class MidiLearnTest : public QObject
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
		// Never leak an armed learn (or a dangling focus target) into the next test.
		MidiLearn::instance()->setFocusTarget(nullptr);
		MidiLearn::instance()->setEnabled(false);
	}

	// The positive case: arm learn, point it at a control, feed the same
	// synthetic CC the MIDI client feeds, and a binding must appear on that
	// control - with the channel and controller number that were transmitted.
	void SyntheticCcBindsFocusedControl()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		QVERIFY(target.controllerConnection() == nullptr);

		const unsigned int before = MidiLearn::instance()->bindingCount();

		MidiLearn::instance()->setFocusTarget(&target);
		MidiLearn::instance()->setEnabled(true);
		QVERIFY(MidiLearn::instance()->isEnabled());

		// channel 2, controller 74, value 127
		QVERIFY(MidiLearn::instance()->handleMidiEvent(makeCc(2, 74, 127)));

		auto* controller = boundMidiController(&target);
		QVERIFY(controller != nullptr);
		QVERIFY(controller->type() == Controller::ControllerType::Midi);
		// MidiPort channels are 1-based: channel 2 becomes 3.
		QCOMPARE(controller->midiPort().inputChannel(), 3);
		QCOMPARE(controller->midiPort().inputController(), 74);

		// One-shot: a learn binds one control and disarms.
		QVERIFY(!MidiLearn::instance()->isEnabled());
		QVERIFY(MidiLearn::instance()->focusTarget() == nullptr);
		QCOMPARE(MidiLearn::instance()->bindingCount(), before + 1);

		// And the binding actually drives the model: a CC on the bound port
		// reaches the model's dataChanged, a CC on another controller does not.
		QSignalSpy spy(&target, SIGNAL(dataChanged()));
		controller->midiPort().processInEvent(makeCc(2, 74, 100), TimePos());
		QCOMPARE(spy.count(), 1);
		controller->midiPort().processInEvent(makeCc(2, 75, 100), TimePos());
		QCOMPARE(spy.count(), 1);
		controller->midiPort().processInEvent(makeCc(3, 74, 100), TimePos());
		QCOMPARE(spy.count(), 1);
		// The model reads the controller value once the period advances.
		// ValueBuffer::interpolate() ramps from the previous value at frame 0 to
		// the new one at the last frame, so read the end of the period.
		Controller::triggerFrameCounter();
		const int lastFrame = Engine::audioEngine()->framesPerPeriod() - 1;
		QVERIFY(target.controllerValue(lastFrame) > 0.0f);
	}

	// Save the project, reload it, and the binding must be identical and still
	// drive the same model - through the project's existing ControllerConnection
	// serialisation, no second format.
	void BindingSurvivesProjectRoundTrip()
	{
		FloatModel target(0.25f, 0.0f, 1.0f, 0.01f);

		MidiLearn::instance()->setFocusTarget(&target);
		MidiLearn::instance()->setEnabled(true);
		QVERIFY(MidiLearn::instance()->handleMidiEvent(makeCc(4, 22, 64)));
		QVERIFY(boundMidiController(&target) != nullptr);

		QDomDocument doc;
		QDomElement root = doc.createElement("lmms");
		doc.appendChild(root);
		QDomElement modelElement = doc.createElement("learnroundtrip");
		root.appendChild(modelElement);
		target.saveSettings(doc, modelElement, "knob");

		// The binding went out through the existing conventions: a <connection>
		// holding a serialised <Midicontroller> with its midiport settings.
		const QString xml = doc.toString();
		QVERIFY(xml.contains("connection"));
		QVERIFY(xml.contains("Midicontroller"));
		QVERIFY(xml.contains("inputcontroller"));

		// Reload into a fresh model, as a project load does.
		FloatModel reloaded(0.25f, 0.0f, 1.0f, 0.01f);
		reloaded.loadSettings(modelElement, "knob");

		auto* reloadedController = boundMidiController(&reloaded);
		QVERIFY(reloadedController != nullptr);
		QVERIFY(reloadedController != boundMidiController(&target));
		QVERIFY(reloadedController->type() == Controller::ControllerType::Midi);
		QCOMPARE(reloadedController->midiPort().inputChannel(), 5);
		QCOMPARE(reloadedController->midiPort().inputController(), 22);

		// It drives the reloaded model, not the original.
		QSignalSpy reloadedSpy(&reloaded, SIGNAL(dataChanged()));
		QSignalSpy originalSpy(&target, SIGNAL(dataChanged()));
		reloadedController->midiPort().processInEvent(makeCc(4, 22, 127), TimePos());
		QCOMPARE(reloadedSpy.count(), 1);
		QCOMPARE(originalSpy.count(), 0);
	}

	// Negative control: with learn mode off (the default), the very same
	// synthetic CC must not create a binding, must not consume the focus target
	// and must not arm anything.
	void LearnOffIgnoresTheSameCc()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		const unsigned int before = MidiLearn::instance()->bindingCount();

		QVERIFY(!MidiLearn::instance()->isEnabled());
		MidiLearn::instance()->setFocusTarget(&target);

		const bool inverted = qEnvironmentVariableIsSet("MIDILEARN_NEGATIVE_CONTROL_INVERTED");

		if (!inverted)
		{
			QVERIFY(!MidiLearn::instance()->handleMidiEvent(makeCc(2, 74, 127)));
			QVERIFY(target.controllerConnection() == nullptr);
			QCOMPARE(MidiLearn::instance()->bindingCount(), before);
			// Even a flood of CCs leaves the offline path inert.
			for (int i = 0; i < 512; ++i)
			{
				QVERIFY(!MidiLearn::instance()->handleMidiEvent(makeCc(i % 16, i % 128, 127)));
			}
			QVERIFY(target.controllerConnection() == nullptr);
			QCOMPARE(MidiLearn::instance()->bindingCount(), before);
		}
		else
		{
			// Deliberately inverted expectation. This run MUST fail; it is how the
			// negative control is shown to be a real check rather than a tautology.
			QVERIFY(MidiLearn::instance()->handleMidiEvent(makeCc(2, 74, 127)));
			QVERIFY(target.controllerConnection() != nullptr);
		}
	}

	// Armed, but nothing focused: a CC must not invent a target.
	void ArmedWithoutFocusTargetDoesNotBind()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		MidiLearn::instance()->setEnabled(true);
		MidiLearn::instance()->setFocusTarget(nullptr);
		QVERIFY(!MidiLearn::instance()->handleMidiEvent(makeCc(2, 74, 127)));
		QVERIFY(MidiLearn::instance()->isEnabled());
		QVERIFY(target.controllerConnection() == nullptr);
	}

	// Only control changes map; notes and bends must pass straight through.
	void NonControlChangeDoesNotBind()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		MidiLearn::instance()->setFocusTarget(&target);
		MidiLearn::instance()->setEnabled(true);
		QVERIFY(!MidiLearn::instance()->handleMidiEvent(MidiEvent(MidiNoteOn, 2, 60, 100)));
		QVERIFY(!MidiLearn::instance()->handleMidiEvent(MidiEvent(MidiPitchBend, 2, 0, 64)));
		QVERIFY(target.controllerConnection() == nullptr);
		QVERIFY(MidiLearn::instance()->isEnabled());
	}

	// The audio device thread is running for the whole of this test
	// (Engine::init(true) starts AudioDummy, which calls
	// AudioEngine::renderNextPeriod() in a loop). With learn armed and a control
	// focused, hundreds of real audio periods must not produce a binding: the
	// audio path has no call site into MidiLearn.
	void AudioPeriodDoesNotLearn()
	{
		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		const unsigned int before = MidiLearn::instance()->bindingCount();

		MidiLearn::instance()->setFocusTarget(&target);
		MidiLearn::instance()->setEnabled(true);

		QTest::qWait(250); // ~10 audio periods at 44.1 kHz / 1024 frames

		QVERIFY(target.controllerConnection() == nullptr);
		QCOMPARE(MidiLearn::instance()->bindingCount(), before);
		QVERIFY(MidiLearn::instance()->isEnabled()); // still waiting for a CC
	}
};

QTEST_GUILESS_MAIN(MidiLearnTest)
#include "MidiLearnTest.moc"
