/*
 * PluginGestureUndoTest.cpp - a hosted plug-in's editor gesture is one step of the project's history
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

/*! Q050-214, over the real main window and the product's hosting modules. The fixtures report a scripted begin, value,
 *  end sequence the way a dragged editor knob does: the CLAP ones from process() through their output events, the VST3
 *  one on the UI thread through the IComponentHandler the host gave it. One gesture spread over several 100 ms polls
 *  must be one journal step, and each of two gestures inside one poll its own, which Edit > Undo, the Undo History panel
 *  and control.undo all take back and which plugin.param_get then reads. An agent's plugin.param_set on the same parameter sits on the same stack. */

#include <QtTest>

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "ProjectJournal.h"
#include "SampleFrame.h"
#include "Song.h"
#include "UndoHistoryPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr float Tolerance = 1e-4f;

//! One hosted device under test, in the terms the test needs.
struct Subject
{
	QByteArray scriptVariable;  //!< the fixture's gesture script (clap-test-gesture.h, vst3-test-instrument.cpp)
	QObject* poller = nullptr;  //!< the object whose private poll() slot the 100 ms timer calls
	AutomatableModel* model = nullptr;
	QJsonObject address;  //!< target, plugin and index of the model
	std::function<void()> block;  //!< one audio block, for a fixture that reports from process()
	std::function<float()> heard;  //!< the gain a block applies now, where the fixture is a gain
};

QAction* editAction(const char* command)
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->data().toString() == QLatin1String(command)) { return action; }
		}
	}
	return nullptr;
}

QPushButton* panelButton(UndoHistoryPanel& panel, const char* command)
{
	for (QPushButton* button : panel.findChildren<QPushButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String(command)) { return button; }
	}
	return nullptr;
}

bool closeTo(float a, float b) { return std::abs(a - b) < Tolerance; }

} // namespace

class PluginGestureUndoTest : public QObject
{
	Q_OBJECT

private:
	//! What the fixture reports next (" b 0.25" and so on), delivered and then read by the poll, as in the product.
	void report(const Subject& subject, const char* tokens)
	{
		qputenv(subject.scriptVariable.constData(), QByteArray::number(++m_sequence) + ' ' + tokens);
		pump(subject);
	}

	void pump(const Subject& subject)
	{
		if (subject.block) { subject.block(); }
		// The VST3 fixture reports during the first poll's controller update, and the second one reads it.
		QVERIFY(QMetaObject::invokeMethod(subject.poller, "poll"));
		QVERIFY(QMetaObject::invokeMethod(subject.poller, "poll"));
	}

	float agentReads(const Subject& subject)
	{
		const ControlResult reply = ControlRegistry::instance()->invoke(QStringLiteral("plugin.param_get"), subject.address);
		if (!reply.ok) { qWarning("plugin.param_get: %s", qPrintable(reply.errorMessage)); return -1.f; }
		return static_cast<float>(reply.result.value(QStringLiteral("parameter")).toObject()
			.value(QStringLiteral("value")).toDouble());
	}

	void agentInvokes(const char* command)
	{
		const ControlResult reply = ControlRegistry::instance()->invoke(QString::fromLatin1(command), {});
		QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
	}

	//! Edit > Undo or Redo: the menu's action, enabled as the window enables it.
	void menuTriggers(const char* command)
	{
		getGUI()->mainWindow()->updateUndoRedoButtons();
		QAction* action = editAction(command);
		QVERIFY(action != nullptr);
		QVERIFY2(action->isEnabled(), command);
		action->trigger();
	}

	void expectValue(const Subject& subject, float expected, int depth)
	{
		const float value = subject.model->value<float>();
		QVERIFY2(closeTo(value, expected), qPrintable(QStringLiteral("model %1, expected %2").arg(value).arg(expected)));
		const float read = agentReads(subject);
		QVERIFY2(closeTo(read, expected), qPrintable(QStringLiteral("param_get %1, expected %2").arg(read).arg(expected)));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);
		// Nothing the plug-in holds comes back over the restored value on the next poll.
		pump(subject);
		QVERIFY2(closeTo(subject.model->value<float>(), expected), "the next poll moved the value");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);
	}

	//! The whole contract, on one subject.
	void checkSubject(const Subject& subject)
	{
		subject.model->setValue(0.9f, false, false);  // away from every value below (the VST3 fixture starts at 0.5)
		pump(subject);
		const float start = subject.model->value<float>();
		QVERIFY(closeTo(start, 0.9f));
		const int depth = Engine::projectJournal()->undoDepth();
		using Check = void (PluginGestureUndoTest::*)(const Subject&, float, int);
		for (Check check : {&PluginGestureUndoTest::checkOneGestureOneStep, &PluginGestureUndoTest::checkTheAgentsEdits,
			&PluginGestureUndoTest::checkWhatMakesAStep, &PluginGestureUndoTest::checkAnUndoInTheMiddleOfADrag,
			&PluginGestureUndoTest::checkTwoGesturesBeforeOnePoll, &PluginGestureUndoTest::checkMarksAroundAPoll,
			&PluginGestureUndoTest::checkMoreGesturesThanTheQueueHolds})
		{
			(this->*check)(subject, start, depth);
			if (QTest::currentTestFailed()) { return; }
		}
	}

	//! One gesture over several polls, taken back and redone from the menu, by the agent and from the panel.
	void checkOneGestureOneStep(const Subject& subject, float start, int depth)
	{
		report(subject, "b 0.25");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);
		report(subject, "0.5");
		report(subject, "0.75 e");
		expectValue(subject, 0.75f, depth + 1);

		menuTriggers("control.undo");
		expectValue(subject, start, depth);
		if (subject.heard) { QVERIFY2(closeTo(subject.heard(), start), "the plug-in does not play the restored value"); }
		menuTriggers("control.redo");
		expectValue(subject, 0.75f, depth + 1);
		if (subject.heard) { QVERIFY2(closeTo(subject.heard(), 0.75f), "the plug-in does not play the redone value"); }

		agentInvokes("control.undo");
		expectValue(subject, start, depth);
		agentInvokes("control.redo");
		expectValue(subject, 0.75f, depth + 1);

		UndoHistoryPanel panel;
		QPushButton* undo = panelButton(panel, "control.undo");
		QPushButton* redo = panelButton(panel, "control.redo");
		QVERIFY(undo != nullptr && redo != nullptr);
		QTest::mouseClick(undo, Qt::LeftButton);
		expectValue(subject, start, depth);
		QTest::mouseClick(redo, Qt::LeftButton);
		expectValue(subject, 0.75f, depth + 1);
	}

	//! An agent's edit is on the same stack: the menu takes it back, the agent the gesture under it.
	void checkTheAgentsEdits(const Subject& subject, float start, int depth)
	{
		QJsonObject write = subject.address;
		write.insert(QStringLiteral("value"), 0.5);
		const ControlResult written = ControlRegistry::instance()->invoke(QStringLiteral("plugin.param_set"), write);
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		expectValue(subject, 0.5f, depth + 2);
		menuTriggers("control.undo");
		expectValue(subject, 0.75f, depth + 1);
		agentInvokes("control.undo");
		expectValue(subject, start, depth);

		// An agent's edit taken back from the menu, then a gesture: control.undo takes the gesture. The record the
		// agent's edit left behind names a step that was undone, not one a bound evicted, and refused this.
		const ControlResult again = ControlRegistry::instance()->invoke(QStringLiteral("plugin.param_set"), write);
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		menuTriggers("control.undo");
		report(subject, "b 0.25 e");
		expectValue(subject, 0.25f, depth + 1);
		agentInvokes("control.undo");
		expectValue(subject, start, depth);
	}

	void checkWhatMakesAStep(const Subject& subject, float start, int depth)
	{
		// A gesture wholly inside one poll is one step too, and a later poll's gesture is a step of its own.
		report(subject, "b 0.25 0.5 e");
		expectValue(subject, 0.5f, depth + 1);
		report(subject, "b 0.25");
		report(subject, "0.75 e");
		expectValue(subject, 0.75f, depth + 2);

		// A gesture that changes nothing leaves no step; a change after an ended gesture, with no gesture of its
		// own (a preset the plug-in's editor loaded), keeps its own step as it always did.
		report(subject, "b e");
		expectValue(subject, 0.75f, depth + 2);
		report(subject, "0.25");
		expectValue(subject, 0.25f, depth + 3);
		menuTriggers("control.undo");
		expectValue(subject, 0.75f, depth + 2);
		menuTriggers("control.undo");
		expectValue(subject, 0.5f, depth + 1);
		menuTriggers("control.undo");
		expectValue(subject, start, depth);
	}

	//! The plug-in's next value after an undo in the middle of a drag is a new step, not an edit the history lost.
	void checkAnUndoInTheMiddleOfADrag(const Subject& subject, float start, int depth)
	{
		report(subject, "b 0.25");
		menuTriggers("control.undo");
		pump(subject);  // the host's value has reached the plug-in's slot: nothing moves
		expectValue(subject, start, depth);
		report(subject, "0.5 e");
		expectValue(subject, 0.5f, depth + 1);
		menuTriggers("control.undo");
		expectValue(subject, start, depth);
	}

	//! Two complete gestures reach one poll: a step each, undone and redone in order, whoever asks.
	void checkTwoGesturesBeforeOnePoll(const Subject& subject, float start, int depth)
	{
		report(subject, "b 0.25 e b 0.3 0.5 e");
		expectValue(subject, 0.5f, depth + 2);
		menuTriggers("control.undo");
		expectValue(subject, 0.25f, depth + 1);
		menuTriggers("control.undo");
		expectValue(subject, start, depth);
		menuTriggers("control.redo");
		expectValue(subject, 0.25f, depth + 1);
		menuTriggers("control.redo");
		expectValue(subject, 0.5f, depth + 2);

		agentInvokes("control.undo");
		expectValue(subject, 0.25f, depth + 1);
		agentInvokes("control.undo");
		expectValue(subject, start, depth);
		agentInvokes("control.redo");
		expectValue(subject, 0.25f, depth + 1);
		agentInvokes("control.redo");
		expectValue(subject, 0.5f, depth + 2);
		menuTriggers("control.undo");
		menuTriggers("control.undo");
		expectValue(subject, start, depth);
	}

	//! A gesture that ends and the next that begins in one poll, and a change with no gesture just before a gesture.
	void checkMarksAroundAPoll(const Subject& subject, float start, int depth)
	{
		report(subject, "b 0.25 e b 0.5");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 2);
		report(subject, "0.75 e");
		expectValue(subject, 0.75f, depth + 2);
		menuTriggers("control.undo");
		expectValue(subject, 0.25f, depth + 1);
		menuTriggers("control.undo");
		expectValue(subject, start, depth);

		report(subject, "0.25 b 0.5 e");
		expectValue(subject, 0.5f, depth + 2);
		menuTriggers("control.undo");
		expectValue(subject, 0.25f, depth + 1);
		menuTriggers("control.undo");
		expectValue(subject, start, depth);
	}

	//! The queue holds 16 gestures. The rest merge into one step, and the plug-in's last value still arrives.
	void checkMoreGesturesThanTheQueueHolds(const Subject& subject, float start, int depth)
	{
		const auto valueOf = [](int gesture) { return 0.05f + 0.04f * gesture; };
		QByteArray tokens;
		for (int gesture = 0; gesture < 20; ++gesture)
		{
			tokens += "b " + QByteArray::number(valueOf(gesture), 'f', 2) + " e ";
		}
		report(subject, tokens.constData());
		expectValue(subject, valueOf(19), depth + 17);
		menuTriggers("control.undo");
		expectValue(subject, valueOf(15), depth + 16);
		for (int step = 0; step < 16; ++step) { menuTriggers("control.undo"); }
		expectValue(subject, start, depth);
	}

	auto makeTrack() -> std::unique_ptr<InstrumentTrack>
	{
		auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
		track->audioBusHandle()->effects()->setEnabledUnjournalled(true);
		return track;
	}

	//! The parameter @a name of @a models, by its fixture's display name.
	static auto addressOf(const QList<AutomatableModel*>& models, const QString& name, const InstrumentTrack* track,
		const QString& plugin, Subject* subject) -> bool
	{
		for (int i = 0; i < models.size(); ++i)
		{
			if (models[i]->displayName() != name) { continue; }
			subject->model = models[i];
			subject->address = {{QStringLiteral("target"), control::trackIdOf(track)},
				{QStringLiteral("plugin"), plugin}, {QStringLiteral("index"), i}};
			return true;
		}
		return false;
	}

	//! Hosts @a key as an effect on @a track and runs the contract on its parameter @a name.
	void checkEffect(const char* module, const Plugin::Descriptor::SubPluginFeatures::Key& key, const char* variable,
		const QString& name, bool blocks)
	{
		auto track = makeTrack();
		EffectChain* chain = track->audioBusHandle()->effects();
		Effect* effect = Effect::instantiate(QString::fromLatin1(module), chain, const_cast<Plugin::Descriptor::SubPluginFeatures::Key*>(&key));
		QVERIFY2(effect != nullptr && !effect->isCorrupted(), "the hosting module did not load the fixture");
		chain->appendEffect(effect);

		Subject subject;
		subject.scriptVariable = variable;
		subject.poller = effect->controls();
		QVERIFY(QMetaObject::invokeMethod(subject.poller, "poll"));  // the CLAP models are built by the first poll
		QVERIFY(addressOf(controlEffectParameters(effect), name, track.get(), QStringLiteral("fx-%1").arg(effect->id()), &subject));
		if (blocks)
		{
			subject.heard = [chain] {
				std::array<SampleFrame, 64> storage;
				storage.fill(SampleFrame(0.5f, 0.5f));
				SampleFrame* busData[1] = {storage.data()};
				AudioBus bus{busData, 1, static_cast<f_cnt_t>(storage.size())};
				chain->processAudioBuffer(bus);
				return storage[0][0] / 0.5f;
			};
			subject.block = [heard = subject.heard] { heard(); };
		}
		checkSubject(subject);
		qunsetenv(variable);
		chain->clear();
	}

	//! Loads @a key as @a track's instrument, the product's path, and runs the contract on its parameter @a name.
	void checkInstrument(const char* module, Plugin::Descriptor::SubPluginFeatures::Key key, const char* variable,
		const QString& name, bool blocks)
	{
		auto track = makeTrack();
		track->loadInstrument(QString::fromLatin1(module), &key);
		Instrument* instrument = track->instrument();
		QVERIFY2(instrument != nullptr && instrument->descriptor()->name == QLatin1String(module),
			"the hosting module did not load the fixture");

		Subject subject;
		subject.scriptVariable = variable;
		subject.poller = instrument;
		QVERIFY(QMetaObject::invokeMethod(subject.poller, "poll"));
		QVERIFY(addressOf(controlInstrumentParameters(instrument), name, track.get(), QStringLiteral("inst"), &subject));
		if (blocks)
		{
			subject.block = [instrument] {
				std::vector<SampleFrame> output(Engine::audioEngine()->framesPerPeriod());
				instrument->play(std::span<SampleFrame>{output});
			};
		}
		checkSubject(subject);
		qunsetenv(variable);
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's own hosting modules
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		// This test's thread is the only one running the fixtures' blocks.
		Engine::audioEngine()->audioDev()->stopProcessing();
		Engine::projectJournal()->setJournalling(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aClapEffectGestureIsOneStep()
	{
#ifdef CLAP_TEST_EFFECT_PATH
		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.name = QStringLiteral("LMMS CLAP Test Gain");
		key.attributes[QStringLiteral("file")] = QStringLiteral(CLAP_TEST_EFFECT_PATH);
		key.attributes[QStringLiteral("id")] = QStringLiteral("org.lmms.test.clap-gain");
		checkEffect("clapeffect", key, "CLAP_TEST_GESTURE", QStringLiteral("Gain"), true);
#else
		QSKIP("no CLAP fixture in this build");
#endif
	}

	void aClapInstrumentGestureIsOneStep()
	{
#ifdef CLAP_TEST_INSTRUMENT_PATH
		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.name = QStringLiteral("LMMS CLAP Test Instrument");
		key.attributes[QStringLiteral("file")] = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
		key.attributes[QStringLiteral("id")] = QStringLiteral("org.lmms.test.clap-instrument");
		checkInstrument("clapinstrument", key, "CLAP_TEST_GESTURE", QStringLiteral("Gain"), true);
#else
		QSKIP("no CLAP fixture in this build");
#endif
	}

	void aVst3EffectGestureIsOneStep()
	{
#ifdef VST3_TEST_INSTRUMENT_BUNDLE
		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.name = QStringLiteral("Zene VST3 Test Instrument");
		key.attributes[QStringLiteral("file")] = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
		key.attributes[QStringLiteral("class")] = QStringLiteral("Zene VST3 Test Instrument");
		checkEffect("vst3effect", key, "VST3_TEST_INSTRUMENT_GESTURE", QStringLiteral("Level"), false);
#else
		QSKIP("no VST3 fixture in this build");
#endif
	}

	void aVst3InstrumentGestureIsOneStep()
	{
#ifdef VST3_TEST_INSTRUMENT_BUNDLE
		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.name = QStringLiteral("Zene VST3 Test Instrument");
		key.attributes[QStringLiteral("file")] = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
		key.attributes[QStringLiteral("class")] = QStringLiteral("Zene VST3 Test Instrument");
		checkInstrument("vst3instrument", key, "VST3_TEST_INSTRUMENT_GESTURE", QStringLiteral("Level"), false);
#else
		QSKIP("no VST3 fixture in this build");
#endif
	}

private:
	QTemporaryDir m_home;
	int m_sequence = 0;
};

QTEST_MAIN(PluginGestureUndoTest)
#include "PluginGestureUndoTest.moc"
