/*
 * MidiLearnGuiTest.cpp - the GUI half of MIDI learn, under a real QApplication.
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

#include <QAction>
#include <QApplication>
#include <QtTest>

#include <thread>

#include "AutomatableModel.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "MidiLearnGui.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

//! The Edit > MIDI Learn tick is a widget state, so observing it needs a
//! QGuiApplication - this is the only MIDI-learn test that has one. Everything
//! it asserts about threads is the same in the daemon-less core tests.
class MidiLearnGuiTest : public QObject
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
		MidiLearnGui::instance()->setArmed(false);
		MidiLearnGui::instance()->setAction(nullptr);
		MidiLearn::instance()->setFocusTarget(nullptr);
		MidiLearn::instance()->setEnabled(false);
		MidiLearn::instance()->applyPendingBinding();
	}

	// A learn ends on the MIDI input thread - the control-change claims it and
	// disarms it - but the tick is a widget, and only the GUI thread may touch
	// it. Before this fix the action was only re-read by
	// MainWindow::updateMidiLearnAction() on aboutToShow, so it stayed ticked
	// until the Edit menu was reopened.
	void MenuTickIsReconciledWithoutReopeningTheMenu()
	{
		QAction learnAction;
		learnAction.setCheckable(true);
		MidiLearnGui::instance()->setAction(&learnAction);

		FloatModel target(0.5f, 0.0f, 1.0f, 0.01f);
		QVERIFY(target.controllerConnection() == nullptr);

		// Arm the way the Edit menu toggle does, and focus a control the way the
		// GUI focus filter does.
		MidiLearnGui::instance()->setArmed(true);
		MidiLearn::instance()->setFocusTarget(&target);
		QVERIFY(learnAction.isChecked());

		// The control-change arrives on the MIDI input thread - the thread that
		// may not touch the action.
		Qt::HANDLE inputThreadId = nullptr;
		std::thread inputThread([&inputThreadId]() {
			inputThreadId = QThread::currentThreadId();
			MidiLearn::instance()->handleMidiEvent(MidiEvent(MidiControlChange, 2, 74, 127));
		});
		inputThread.join();

		QVERIFY(inputThreadId != QThread::currentThreadId());

		// The learn is over - and the tick is still set, because nothing outside
		// the GUI thread has written to the action.
		QVERIFY(!MidiLearn::instance()->isEnabled());
		QVERIFY(learnAction.isChecked());

		// The GUI side brings it back in line on its own: no menu reopening, no
		// aboutToShow, just the bind timer noticing the learn ended.
		QTRY_VERIFY(!learnAction.isChecked());

		// And the learn really did bind, on this thread.
		QVERIFY(target.controllerConnection() != nullptr);
		QVERIFY(MidiLearn::instance()->lastBindingThreadId() == QThread::currentThreadId());
		QVERIFY(MidiLearn::instance()->focusTarget() == nullptr);
	}
};

// QTEST_MAIN would do, but the platform plugin has to be chosen before the
// QApplication exists: the repo's gate runner exports QT_QPA_PLATFORM=offscreen
// (tests/run-all-gates.sh, Gate 1) and its build workflow does not, so default
// to offscreen when nothing else is configured and no display is attached.
int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	QApplication app(argc, argv);
	MidiLearnGuiTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "MidiLearnGuiTest.moc"
