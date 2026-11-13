/*
 * MidiTakeMenuTest.cpp - Q050-054.1: an instrument track armed for MIDI takes from its Record
 *                        menu records through the header's Record and Stop buttons
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

/*! In the real application: the track's "Record > Record MIDI Takes > On" entry (the same menu the track
 *  header's menu and the Inspector show) arms the track through track.set_midi_arm; the header's Record
 *  button starts the pass, MIDI is played into the track while the engine runs, and the header's Stop
 *  button leaves the take - through record.midi_capture_commit, the command an agent's transport.stop
 *  runs too. */

#include <QtTest>

#include <QAbstractButton>
#include <QMenu>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "ShellCommandMenus.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QMenu* submenu(QMenu* menu, const QString& title)
{
	for (QAction* action : menu->actions())
	{
		if (action->menu() != nullptr && action->text() == title) { return action->menu(); }
	}
	return nullptr;
}

QAction* entry(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

//! The header button that dispatches @a command.
QAbstractButton* headerButton(const QString& command)
{
	for (QAbstractButton* button : getGUI()->mainWindow()->findChildren<QAbstractButton*>())
	{
		if (button->property("controlCommand").toString() == command && button->isVisible()) { return button; }
	}
	for (QAbstractButton* button : getGUI()->mainWindow()->findChildren<QAbstractButton*>())
	{
		if (button->property("controlCommand").toString() == command) { return button; }
	}
	return nullptr;
}

} // namespace

class MidiTakeMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		NotePlayHandleManager::init();  // as main() does: the note-on below needs the handle pool
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theRecordMenuArmsAndTheTransportButtonsRecordATake()
	{
		const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		ControlResult error;
		auto* track = dynamic_cast<InstrumentTrack*>(
			control::resolveTrack(added.result.value(QStringLiteral("track")).toString(), &error));
		QVERIFY(track != nullptr);

		QMenu menu;
		ShellCommandMenus::populateTrack(&menu, track);
		QMenu* record = submenu(&menu, QStringLiteral("Record"));
		QVERIFY(record != nullptr);
		QMenu* takes = submenu(record, QStringLiteral("Record MIDI Takes"));
		QVERIFY(takes != nullptr);
		QAction* on = entry(takes, QStringLiteral("On"));
		QAction* off = entry(takes, QStringLiteral("Off"));
		QVERIFY(on != nullptr && off != nullptr);
		QCOMPARE(on->property("controlCommand").toString(), QStringLiteral("track.set_midi_arm"));
		QAction* status = entry(record, QStringLiteral("MIDI Take Status"));
		QAction* keep = entry(record, QStringLiteral("Keep the Captured MIDI"));
		QVERIFY(status != nullptr && keep != nullptr && keep->isEnabled());
		QCOMPARE(keep->property("controlCommand").toString(), QStringLiteral("record.midi_capture_commit"));

		QVERIFY(!track->midiTakeCapture().isArmed());
		on->trigger();
		QVERIFY(track->midiTakeCapture().isArmed());

		QAbstractButton* recordButton = headerButton(QStringLiteral("transport.record"));
		QAbstractButton* stopButton = headerButton(QStringLiteral("transport.stop"));
		QVERIFY(recordButton != nullptr && stopButton != nullptr);
		Engine::getSong()->setPlayPos(0, Song::PlayMode::Song);
		recordButton->click();
		QTRY_VERIFY(Engine::getSong()->isPlaying());
		QVERIFY(track->midiTakeCapture().isCapturing());
		QTest::qWait(150);
		track->processInEvent(MidiEvent(MidiNoteOn, 0, 72, 100));
		QTest::qWait(150);
		track->processInEvent(MidiEvent(MidiNoteOff, 0, 72, 0));
		QTest::qWait(50);
		stopButton->click();
		QTRY_VERIFY(!Engine::getSong()->isPlaying());

		std::vector<MidiClip*> clips;
		for (Clip* clip : track->getClips())
		{
			if (auto* midi = dynamic_cast<MidiClip*>(clip)) { clips.push_back(midi); }
		}
		QCOMPARE(int(clips.size()), 1);
		QCOMPARE(int(clips.front()->notes().size()), 1);
		QCOMPARE(clips.front()->notes()[0]->key(), 72);
		QCOMPARE(ControlRegistry::instance()->lastTransaction()->command, QStringLiteral("record.midi_capture_commit"));

		off->trigger();
		QVERIFY(!track->midiTakeCapture().isArmed());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MidiTakeMenuTest)
#include "MidiTakeMenuTest.moc"
