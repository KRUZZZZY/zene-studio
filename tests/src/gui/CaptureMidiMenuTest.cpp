/*
 * CaptureMidiMenuTest.cpp - Edit > Capture MIDI names what the capture window holds
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

/*! In the real application: with retrospective capture armed and two notes captured, opening the
 *  Edit menu gives the Capture MIDI item "(4 event(s), ...)" and enables it; with nothing captured
 *  it reads plain "Capture MIDI" and is disabled. */

#include <QtTest>

#include <QMenuBar>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "RetroMidiCapture.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* captureItem(QMenu** owner)
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->data().toString() == QLatin1String("midi.retro_capture_to_clip")) { *owner = top->menu(); return action; }
		}
	}
	return nullptr;
}

} // namespace

class CaptureMidiMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("midi.retro_capture_arm"), {{QStringLiteral("armed"), false}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theItemNamesTheWindow()
	{
		QMenu* edit = nullptr;
		QAction* capture = captureItem(&edit);
		QVERIFY(capture != nullptr && edit != nullptr);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("midi.retro_capture_arm"),
			{{QStringLiteral("armed"), true}}).ok);
		emit edit->aboutToShow();
		const int before = ControlRegistry::instance()->invoke(QStringLiteral("midi.retro_capture_status"), QJsonObject{})
			.result.value(QStringLiteral("events_buffered")).toInt();
		if (before == 0)
		{
			QCOMPARE(capture->text(), QStringLiteral("Capture MIDI"));
			QVERIFY(!capture->isEnabled());
		}
		RetroMidiCapture& ring = Engine::audioEngine()->midiClient()->retroCapture();
		ring.capture(MidiEvent(MidiNoteOn, 0, 60, 100), 0);
		ring.capture(MidiEvent(MidiNoteOff, 0, 60, 0), 48);
		ring.capture(MidiEvent(MidiNoteOn, 0, 64, 100), 96);
		ring.capture(MidiEvent(MidiNoteOff, 0, 64, 0), 144);
		emit edit->aboutToShow();
		QVERIFY(capture->isEnabled());
		QVERIFY2(capture->text().startsWith(QStringLiteral("Capture MIDI (%1 event(s), ").arg(before + 4)),
			qPrintable(capture->text()));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(CaptureMidiMenuTest)
#include "CaptureMidiMenuTest.moc"
