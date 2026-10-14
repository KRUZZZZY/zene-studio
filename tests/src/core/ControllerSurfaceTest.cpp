/*
 * ControllerSurfaceTest.cpp - headless proof of the controller surface
 *                             (feature row 19, board task #651): soft-takeover,
 *                             LED/feedback output and mapping templates.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QDomDocument>
#include <QSignalSpy>
#include <QtTest>

#include "AutomatableModel.h"
#include "AudioEngine.h"
#include "Controller.h"
#include "ControllerConnection.h"
#include "ControllerSurface.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "MidiPort.h"
#include "PeakController.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! The synthetic-CC entry point: the same event the MIDI clients hand to
//! MidiLearn::handleMidiEvent and to MidiPort::processInEvent (see
//! MidiClientRaw::processParsedEvent and the SND_SEQ_EVENT_CONTROLLER case of
//! MidiAlsaSeq::run). There is no hardware on this box; every claim in this
//! file is made by feeding this event, so a claim that needs a real controller
//! is not made here.
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

//! Bind \a model through the landed MIDI-learn path - the same one-click
//! binding the Edit > MIDI Learn menu arms - and hand back the controller the
//! learn created. Everything this file proves sits ON that path: it is not a
//! second binding mechanism.
MidiController* bindThroughMidiLearn(AutomatableModel* model, int channel,
	int controllerNumber)
{
	MidiLearn::instance()->setFocusTarget(model);
	MidiLearn::instance()->setEnabled(true);
	if (!MidiLearn::instance()->handleMidiEvent(makeCc(channel, controllerNumber, 127)))
	{
		return nullptr;
	}
	return boundMidiController(model);
}

//! The template name this file uses. Deliberately not a name a user would
//! pick: saveTemplate() writes a REAL file under the user config dir, and the
//! tests remove it again.
const QString kTemplate = QStringLiteral("zene-surface-selftest");

} // namespace

class ControllerSurfaceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::getSong() != nullptr);
		// A command-driven case below needs the registry addressable; without
		// this every invoke answers 'the engine is still starting up'.
		ControlRegistry::instance()->setReady(true);
	}

	void cleanupTestCase()
	{
		// Never leave the self-test template behind: saveTemplate() writes into
		// the user's config dir, and a leftover file would show up in the next
		// run's template list.
		ControllerSurface::instance().removeTemplate(kTemplate);
		Engine::destroy();
	}

	void cleanup()
	{
		// Never leak an armed learn (or a dangling focus target) into the next
		// test - the MidiLearnTest discipline.
		MidiLearn::instance()->setFocusTarget(nullptr);
		MidiLearn::instance()->setEnabled(false);
	}

	void defaultPeakControllerWithoutEffectReturnsSilence()
	{
		PeakController controller(Engine::getSong());
		Controller::triggerFrameCounter();
		QCOMPARE(controller.currentValue(0), 0.0f);
	}

	/*! Soft-takeover: a hardware control that is bound to a control whose
	 *  stored value differs from the knob's position is IGNORED, and the model
	 *  moves only once a control-change crosses the stored value. The negative
	 *  control below (SoftTakeoverOffMovesOnTheFirstCc) is the same sequence
	 *  with the gate off, so this is a real check and not a tautology. */
	void SoftTakeoverWaitsForTheHardwareToCrossTheStoredValue()
	{
		FloatModel knob(0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Soft Takeover Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 2, 74);
		QVERIFY(controller != nullptr);
		QCOMPARE(controller->midiPort().inputChannel(), 3); // 1-based: CC ch 2 -> 3
		QCOMPARE(controller->midiPort().inputController(), 74);

		// The fader sits at 60%: the takeover waits for 0.6, not for wherever
		// the hardware happens to be.
		controller->setSoftTakeoverEnabled(true);
		controller->setSoftTakeoverTarget(0.6f);
		QVERIFY(controller->softTakeoverEnabled());
		QVERIFY(!controller->softTakeoverCaptured());

		QSignalSpy moved(controller, SIGNAL(valueChanged()));

		// The hardware starts at 0.09 and walks up to 0.59 - every one of these
		// is BELOW the stored value and further than one MIDI step from it, so
		// none of them may move the model. 75/127 = 0.5906, which is 0.0094 away
		// from 0.6: more than the one-step (1/127) tolerance, so still ignored.
		for (int value = 12; value <= 75; ++value)
		{
			controller->midiPort().processInEvent(makeCc(2, 74, value), TimePos());
		}
		QCOMPARE(moved.count(), 0);
		QVERIFY(!controller->softTakeoverCaptured());

		// 76/127 = 0.5984 is the CLOSEST representable control-change value to
		// 0.6 - 0.0016 away, well inside the one-step tolerance. The hardware is
		// at the stored value now, so this is the instant it takes over: a knob
		// cannot be placed on 0.6 exactly, and refusing this step would mean a
		// surface that never takes over.
		controller->midiPort().processInEvent(makeCc(2, 74, 76), TimePos());
		QCOMPARE(moved.count(), 1);
		QVERIFY(controller->softTakeoverCaptured());

		// Stepping past the target moves it too, and no further gate applies.
		controller->midiPort().processInEvent(makeCc(2, 74, 77), TimePos());
		QCOMPARE(moved.count(), 2);

		// From there every further control-change moves it, including ones
		// below the takeover point - the gate is a one-time crossing, not a
		// permanent clamp.
		controller->midiPort().processInEvent(makeCc(2, 74, 30), TimePos());
		QCOMPARE(moved.count(), 3);
		controller->midiPort().processInEvent(makeCc(2, 74, 120), TimePos());
		QCOMPARE(moved.count(), 4);
	}

	/*! The crossing the test above proves is an OBSERVED one: the hardware has
	 *  to be seen to pass the stored value. A first control-change that already
	 *  sits on the far side of the target is not a crossing - the knob was
	 *  never at the stored value - so it must be ignored rather than capture
	 *  the control and jump the model to the hardware's position. */
	void SoftTakeoverNeedsAnObservedCrossingNotTheFirstSample()
	{
		FloatModel knob(0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface No Spurious Capture Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 8, 76);
		QVERIFY(controller != nullptr);
		controller->setSoftTakeoverEnabled(true);
		controller->setSoftTakeoverTarget(0.5f);

		QSignalSpy moved(controller, SIGNAL(valueChanged()));

		// The knob rests at 0.5 and the hardware already sits at 101/127 =
		// 0.795, well above it. Nothing has crossed 0.5 yet, so this must not
		// take the control over: 0.795 is nowhere near the target, so the
		// one-step tolerance cannot be what refuses it.
		controller->midiPort().processInEvent(makeCc(8, 76, 101), TimePos());
		QCOMPARE(moved.count(), 0);
		QVERIFY(!controller->softTakeoverCaptured());

		// Still above the target after a second value - same verdict, so the
		// refusal above is not just about the very first event.
		controller->midiPort().processInEvent(makeCc(8, 76, 110), TimePos());
		QCOMPARE(moved.count(), 0);
		QVERIFY(!controller->softTakeoverCaptured());

		// Now the hardware comes DOWN through the stored value: 101/127 = 0.795
		// was above 0.5 and 60/127 = 0.472 is below it. That is a crossing, and
		// 0.472 is 0.028 away from the target - far outside the one-step
		// tolerance - so only the observed crossing can capture here.
		controller->midiPort().processInEvent(makeCc(8, 76, 60), TimePos());
		QCOMPARE(moved.count(), 1);
		QVERIFY(controller->softTakeoverCaptured());
	}

	/*! LED feedback borrows the port: an input-only control is moved to Duplex
 *  with its output channel pointed at this control's channel, because a
 *  port that cannot output cannot carry the write. Turning feedback off
 *  must GIVE THE PORT BACK - a control the user only ever listens to must
 *  not stay exposed as an output, on a channel the user never chose, and
 *  those are exactly the settings MidiPort::saveSettings writes out. */
	void DisablingFeedbackGivesThePortBack()
	{
		FloatModel knob(0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Feedback Port Restore Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 9, 90);
		QVERIFY(controller != nullptr);
		QVERIFY(controller->midiPort().mode() == MidiPort::Mode::Input);

		// What the port looked like before feedback touched it.
		const MidiPort::Mode modeBefore = controller->midiPort().mode();
		const int channelBefore = controller->midiPort().outputChannel();

		controller->setFeedbackEnabled(true);
		QVERIFY(controller->feedbackEnabled());
		QVERIFY(controller->midiPort().mode() == MidiPort::Mode::Duplex);
		QCOMPARE(controller->midiPort().outputChannel(), controller->midiPort().inputChannel());

		controller->setFeedbackEnabled(false);
		QVERIFY(!controller->feedbackEnabled());
		QCOMPARE(controller->midiPort().mode(), modeBefore);
		QCOMPARE(controller->midiPort().outputChannel(), channelBefore);

		// And a control that was already Duplex on a channel of its own is
		// left alone by feedback: only what feedback itself changed is undone.
		controller->midiPort().setMode(MidiPort::Mode::Duplex);
		controller->midiPort().setOutputChannel(3);
		controller->setFeedbackEnabled(true);
		controller->setFeedbackEnabled(false);
		QVERIFY(controller->midiPort().mode() == MidiPort::Mode::Duplex);
		QCOMPARE(controller->midiPort().outputChannel(), 3);
	}

	/*! The soft-takeover target is project state: a project saved with takeover
	 *  armed reopens with the target it had, not with a target of zero. The
	 *  enabled flag alone would reopen a control that captures on the very
	 *  first control-change - the one thing soft takeover exists to prevent. */
	void TheSoftTakeoverTargetSurvivesTheProjectRoundTrip()
	{
		FloatModel knob(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Target Round Trip Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 10, 91);
		QVERIFY(controller != nullptr);
		controller->setSoftTakeoverEnabled(true);
		controller->setSoftTakeoverTarget(0.25f);

		QDomDocument doc;
		QDomElement root = doc.createElement("lmms");
		doc.appendChild(root);
		QDomElement modelElement = doc.createElement("surfacetargetroundtrip");
		root.appendChild(modelElement);
		knob.saveSettings(doc, modelElement, "knob");

		FloatModel reloaded(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Target Round Trip Reloaded"));
		reloaded.loadSettings(modelElement, "knob");

		MidiController* restored = boundMidiController(&reloaded);
		QVERIFY(restored != nullptr);
		QVERIFY(restored->softTakeoverEnabled());
		QCOMPARE(restored->softTakeoverTarget(), 0.25f);
		QVERIFY(!restored->softTakeoverCaptured());

		// The consequence, not just the number: with the target restored, a
		// control-change that sits on the far side of 0.25 without ever
		// crossing it does not capture. Against a target of zero it would.
		QSignalSpy moved(restored, SIGNAL(valueChanged()));
		restored->midiPort().processInEvent(makeCc(10, 91, 100), TimePos());
		QCOMPARE(moved.count(), 0);
		QVERIFY(!restored->softTakeoverCaptured());
	}

	/*! The negative control for the test above: with soft-takeover OFF - the
	 *  default - the very first control-change moves the model. Without this,
	 *  the ignored CCs above could be explained by something other than the
	 *  gate. */
	void SoftTakeoverOffMovesOnTheFirstCc()
	{
		FloatModel knob(0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Hard Takeover Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 2, 75);
		QVERIFY(controller != nullptr);
		QVERIFY(!controller->softTakeoverEnabled());

		QSignalSpy moved(controller, SIGNAL(valueChanged()));
		controller->midiPort().processInEvent(makeCc(2, 75, 12), TimePos());
		QCOMPARE(moved.count(), 1);
	}

	/*! LED/feedback output, measured at the client boundary. There is no
	 *  hardware on this box, so the strongest measurable claim in-process is
	 *  "the write reached the MIDI client": MidiPort::processOutEvent() counts
	 *  the events it hands to m_midiClient->processOutEvent(), and that counter
	 *  is read here. The client's own bytes go to sendByte(), which the dummy
	 *  client no-ops - so this test does NOT prove a lamp lit, and says so. */
	void FeedbackIsAMeasuredWriteToTheOutputClient()
	{
		FloatModel knob(0.5f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Feedback Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 4, 21);
		QVERIFY(controller != nullptr);

		// The port starts as a plain INPUT port, so nothing can travel out.
		QVERIFY(controller->midiPort().mode() == MidiPort::Mode::Input);
		QCOMPARE(controller->midiPort().outputEventsOffered(), quint64(0));
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(0));

		// Feedback off (the default): a control-change moves the model and
		// writes nothing back.
		controller->midiPort().processInEvent(makeCc(4, 21, 100), TimePos());
		QCOMPARE(controller->midiPort().outputEventsOffered(), quint64(0));
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(0));

		// Enabling it writes the stored value straight back - and it moves the
		// port to Duplex with its output channel pointed at the channel this
		// control transmits on, or MidiPort::processOutEvent would filter the
		// write out against a different output channel.
		controller->setFeedbackEnabled(true);
		QVERIFY(controller->feedbackEnabled());
		QVERIFY(controller->midiPort().mode() == MidiPort::Mode::Duplex);
		QCOMPARE(controller->midiPort().outputChannel(), controller->midiPort().inputChannel());
		QCOMPARE(controller->midiPort().outputEventsOffered(), quint64(1));
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(1));

		// The value written is the model's, as the byte the hardware receives.
		// The CC value 100 set the controller's last value; 100/127 rounds to
		// 100. This is the mapping, asserted rather than assumed.
		controller->midiPort().processInEvent(makeCc(4, 21, 100), TimePos());
		QCOMPARE(controller->feedbackByte(), 100);
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(2));

		// Every later move writes back too, and the port's two counters stay in
		// step because nothing filtered the event out on the way.
		controller->midiPort().processInEvent(makeCc(4, 21, 0), TimePos());
		QCOMPARE(controller->feedbackByte(), 0);
		QCOMPARE(controller->midiPort().outputEventsOffered(), quint64(3));
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(3));

		// A forced write outside the control-change path is the same shape - the
		// seam a caller (and controller.feedback) uses.
		QVERIFY(controller->sendFeedback());
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(4));

		// Turning it off stops the writes: the negative control for the whole
		// measurement.
		controller->setFeedbackEnabled(false);
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(4));
		controller->midiPort().processInEvent(makeCc(4, 21, 64), TimePos());
		QCOMPARE(controller->midiPort().outputEventsWritten(), quint64(4));
	}

	/*! Mapping templates: save the project's current bindings under a name,
	 *  list the name, read it back with the surface flags intact, and apply it.
	 *  The template store is files outside the project, so this test writes a
	 *  real file and removes it again in cleanupTestCase(). */
	void TemplatesSaveListReadAndApply()
	{
		ControllerSurface& surface = ControllerSurface::instance();
		surface.removeTemplate(kTemplate); // a leftover from an interrupted run

		FloatModel knob(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Template Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 5, 30);
		QVERIFY(controller != nullptr);
		controller->setSoftTakeoverEnabled(true);
		controller->setSoftTakeoverTarget(0.25f);
		controller->setFeedbackEnabled(true);

		const QStringList before = surface.listTemplates();
		QVERIFY(surface.saveTemplate(kTemplate));

		const QStringList after = surface.listTemplates();
		QCOMPARE(after.size(), before.size() + 1);
		QVERIFY(after.contains(kTemplate));
		QVERIFY(surface.templatePathFor(kTemplate).endsWith(QStringLiteral(".json")));

		const ControllerTemplate saved = surface.loadTemplate(kTemplate);
		QCOMPARE(saved.name, kTemplate);
		QCOMPARE(saved.bindings.size(), 1);
		// 1-based channel: CC channel 5 -> MidiPort channel 6.
		QCOMPARE(saved.bindings.first().channel, 6);
		QCOMPARE(saved.bindings.first().controller, 30);
		QCOMPARE(saved.bindings.first().targetName, knob.fullDisplayName());
		// The two flags travel with the binding: a template that restored only
		// the addresses would leave every fader hard-taking-over.
		QVERIFY(saved.bindings.first().softTakeover);
		QVERIFY(saved.bindings.first().feedback);

		// Applying it re-binds the control. That REPLACES the controller this
		// test was holding, so nothing below may use the old pointer.
		QCOMPARE(surface.applyTemplate(kTemplate), 1);

		MidiController* rebound = boundMidiController(&knob);
		QVERIFY(rebound != nullptr);
		QCOMPARE(rebound->midiPort().inputChannel(), 6);
		QCOMPARE(rebound->midiPort().inputController(), 30);
		QVERIFY(rebound->softTakeoverEnabled());
		QVERIFY(rebound->feedbackEnabled());
		QVERIFY(rebound->midiPort().mode() == MidiPort::Mode::Duplex);
		// The re-bound control drives the model on the synthetic CC. Soft takeover
		// is armed at the model's 0.25, so the FIRST CC alone must not move it:
		// there is no observed crossing yet. See
		// SoftTakeoverNeedsAnObservedCrossingNotTheFirstSample.
		QSignalSpy moved(rebound, SIGNAL(valueChanged()));
		rebound->midiPort().processInEvent(makeCc(5, 30, 100), TimePos());
		QCOMPARE(moved.count(), 0);
		// The second CC comes from the other side of 0.25: that is a crossing, so
		// this one captures and reaches the model.
		rebound->midiPort().processInEvent(makeCc(5, 30, 0), TimePos());
		QCOMPARE(moved.count(), 1);

		// The bindings the project reports are the same list the template holds.
		const QVector<ControllerTemplateBinding> current = surface.currentBindings();
		QCOMPARE(current.size(), 1);
		QCOMPARE(current.first().targetName, knob.fullDisplayName());
		QCOMPARE(current.first().channel, 6);
		QCOMPARE(current.first().controller, 30);
		QVERIFY(current.first().feedback);

		QVERIFY(surface.removeTemplate(kTemplate));
		QVERIFY(!surface.listTemplates().contains(kTemplate));
	}

	/*! Negative controls for the template store: a control that is not in the
	 *  project does not resolve, and a template that was never saved does not
	 *  bind anything. Without these, "applyTemplate returned 1" above could be
	 *  read as "it binds whatever it is given". */
	void TemplatesReportWhatTheyCannotResolve()
	{
		ControllerSurface& surface = ControllerSurface::instance();
		QVERIFY(surface.resolveTarget(QStringLiteral("no such control in this project"))
			== nullptr);
		QVERIFY(surface.resolveTarget(QString()) == nullptr);
		QCOMPARE(surface.applyTemplate(QStringLiteral("zene-surface-never-saved")), 0);
		QVERIFY(surface.loadTemplate(QStringLiteral("zene-surface-never-saved"))
			.bindings.isEmpty());
	}

	/*! The surface flags survive the project's own serialisation: they are
	 *  written into the model's <connection> element by
	 *  MidiController::saveSettings, which is why they are project state and
	 *  why the A16 rows for controller.soft_takeover and controller.feedback
	 *  are mutating. */
	void SurfaceFlagsSurviveTheProjectRoundTrip()
	{
		FloatModel knob(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Round Trip Knob"));

		MidiController* controller = bindThroughMidiLearn(&knob, 6, 22);
		QVERIFY(controller != nullptr);
		controller->setSoftTakeoverEnabled(true);
		controller->setFeedbackEnabled(true);

		QDomDocument doc;
		QDomElement root = doc.createElement("lmms");
		doc.appendChild(root);
		QDomElement modelElement = doc.createElement("surfaceroundtrip");
		root.appendChild(modelElement);
		knob.saveSettings(doc, modelElement, "knob");

		const QString xml = doc.toString();
		QVERIFY(xml.contains(QStringLiteral("softtakeover=\"1\"")));
		QVERIFY(xml.contains(QStringLiteral("feedback=\"1\"")));

		FloatModel reloaded(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(),
			QStringLiteral("Surface Round Trip Reloaded"));
		reloaded.loadSettings(modelElement, "knob");

		MidiController* restored = boundMidiController(&reloaded);
		QVERIFY(restored != nullptr);
		QVERIFY(restored->softTakeoverEnabled());
		QVERIFY(restored->feedbackEnabled());
		QCOMPARE(restored->midiPort().inputChannel(), 7);
		QCOMPARE(restored->midiPort().inputController(), 22);
	}

	/*! Two bound controls that share a fullDisplayName() are AMBIGUOUS, and an
	 *  ambiguous name must be refused rather than resolved to whichever
	 *  ControllerConnection happens to come first in the list. The same
	 *  command already refuses an empty 'control' when more than one control is
	 *  bound (findBoundControl's `all.size() > 1`); naming one of two identical
	 *  names is the same unanswerable question asked a different way.
	 *
	 *  Two FloatModels under the song share a parent and a display name, so
	 *  their fullDisplayName() is the same string - the collision a user hits
	 *  with two identically named plugins or two copies of one device. Both are
	 *  bound here through the landed MIDI-learn path. */
	void DuplicateControlNamesAreRefusedNotResolvedToTheFirstBinding()
	{
		const QString name = QStringLiteral("Surface Ambiguous Knob");
		FloatModel first(0.25f, 0.0f, 1.0f, 0.01f, Engine::getSong(), name);
		FloatModel second(0.75f, 0.0f, 1.0f, 0.01f, Engine::getSong(), name);

		QVERIFY(bindThroughMidiLearn(&first, 10, 31) != nullptr);
		QVERIFY(bindThroughMidiLearn(&second, 11, 32) != nullptr);

		// The collision is real: the two models really do answer to ONE name.
		QCOMPARE(first.fullDisplayName(), second.fullDisplayName());
		QVERIFY(!first.fullDisplayName().isEmpty());

		// Naming that name must be refused, typed, and must change NEITHER
		// binding: picking the first would write to a control the caller did
		// not choose.
		const ControlResult result = ControlRegistry::instance()->invoke(
			QStringLiteral("controller.soft_takeover"),
			{{QStringLiteral("control"), first.fullDisplayName()},
				{QStringLiteral("enabled"), true}});
		QVERIFY2(!result.ok, "a control name two bound controls answer to must be refused");
		QVERIFY2(result.errorMessage.contains(QStringLiteral("2")),
			qPrintable(QStringLiteral("ok=%1 message=[%2]").arg(result.ok).arg(result.errorMessage)));

		QVERIFY(!boundMidiController(&first)->softTakeoverEnabled());
		QVERIFY(!boundMidiController(&second)->softTakeoverEnabled());
	}
};

QTEST_GUILESS_MAIN(ControllerSurfaceTest)
#include "ControllerSurfaceTest.moc"
