/*
 * SlicerTPlayheadTest.cpp - the SlicerT playhead leaves the render thread through a timer (cluster 4468)
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

// Cluster 4468 (M0 blockers): SlicerT's note playback ran on the render thread and emitted isPlaying() there.
// A signal delivered to a GUI slot from the render thread is queued by Qt, and queueing allocates. The render
// thread now publishes the playhead to atomics; a timer on the GUI thread reads them and emits. The test runs
// real render periods with a slicer note sounding: the render thread must emit nothing, and the GUI timer must
// still deliver the playhead once events are pumped.

#include <QtTest>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "SongEditor.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "PluginView.h"
#include "../gui/GuiTestApplication.h"
#include "ReversibilityTestSupport.h"
#include "TakeLaneTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! One real render period, driven by the test (the device is stopped).
void renderPeriod()
{
	static_cast<void>(Engine::audioEngine()->renderNextPeriod());
}

} // namespace

class SlicerTPlayheadTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		guitest::startGui(m_home);
		QVERIFY(gui::getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! A sounding slicer note: the render periods emit no isPlaying from the render thread, and the GUI timer
	//! delivers the playhead to the connected slot once the event loop runs.
	void aSoundingSliceNoteEmitsItsPlayheadFromTheGuiThreadOnly()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("tone.wav")), 440.0);
		QVERIFY(!wav.isEmpty());

		Engine::getSong()->clearProject();
		const QString id = addTrack(QStringLiteral("instrument"));
		QVERIFY(!id.isEmpty());
		ControlResult failure;
		auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &failure));
		QVERIFY(track != nullptr);
		// This is the drag-and-drop construction path: the instrument is built on
		// InstrumentLoaderThread, then moved to the container (GUI) thread.
		InstrumentLoaderThread loader(nullptr, track, QStringLiteral("slicert"));
		loader.start();
		QVERIFY(loader.wait());
		QCoreApplication::processEvents();
		Instrument* slicer = track->instrument();
		QVERIFY(slicer != nullptr);
		QCOMPARE(slicer->thread(), QThread::currentThread());
		gui::InstrumentTrackView* trackView = nullptr;
		for (gui::TrackView* candidate : gui::getGUI()->songEditor()->m_editor->trackViews())
		{
			if (candidate->getTrack() == track)
			{
				trackView = dynamic_cast<gui::InstrumentTrackView*>(candidate);
				break;
			}
		}
		QVERIFY(trackView != nullptr);
		gui::InstrumentTrackWindow* window = trackView->getInstrumentTrackWindow();
		QVERIFY(window != nullptr);
		gui::PluginView* view = window->findChild<gui::PluginView*>();
		QVERIFY(view != nullptr);
		QCOMPARE(view->thread(), QThread::currentThread());
		QTimer* pollTimer = window->findChild<QTimer*>(QStringLiteral("slicerTPlayheadPoll"));
		QVERIFY(pollTimer != nullptr);
		QCOMPARE(pollTimer->thread(), QThread::currentThread());
		QVERIFY(pollTimer->isActive());
		// SlicerT's public slot (loadFile is protected in Plugin); the slot loads the file on the calling thread.
		QVERIFY(QMetaObject::invokeMethod(slicer, "updateFile", Qt::DirectConnection, Q_ARG(QString, wav)));

		// Signal by name: the test does not need the SlicerT header (its plug-in module is loaded by name).
		QSignalSpy playheads(slicer, SIGNAL(isPlaying(float, float, float)));
		QVERIFY(playheads.isValid());

		MidiEvent noteOn;
		noteOn.setType(MidiNoteOn);
		noteOn.setKey(track->baseNote());
		noteOn.setVelocity(100);
		track->processInEvent(noteOn);
		for (int i = 0; i < 8; ++i) { renderPeriod(); }

		QCOMPARE(playheads.count(), 0);  // nothing crossed from the render thread

		QTest::qWait(120);  // the GUI-side timer runs on this, the main thread
		QVERIFY2(playheads.count() > 0, "the GUI timer never delivered the playhead of a sounding slice");

		MidiEvent noteOff = noteOn;
		noteOff.setType(MidiNoteOff);
		noteOff.setVelocity(0);
		track->processInEvent(noteOff);
		for (int i = 0; i < 4; ++i) { renderPeriod(); }
		QTest::qWait(120);
	}
};

QTEST_MAIN(SlicerTPlayheadTest)
#include "SlicerTPlayheadTest.moc"
