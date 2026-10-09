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

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"
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

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
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
		// The device is named by its catalogue id (dev-<n>): find the SlicerT entry by its plug-in name.
		const ControlResult listed = run(QStringLiteral("plugin.list"),
			{{QStringLiteral("kind"), QStringLiteral("instrument")}, {QStringLiteral("loadable_only"), true}});
		QString device;
		for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("name")).toString().compare(QStringLiteral("slicert"), Qt::CaseInsensitive) == 0
				|| entry.value(QStringLiteral("key")).toString().contains(QStringLiteral("slicert"), Qt::CaseInsensitive))
			{
				device = entry.value(QStringLiteral("id")).toString();
				break;
			}
		}
		QVERIFY2(!device.isEmpty(), "this build's plug-in folder lists no SlicerT instrument");
		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), id}, {QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));

		ControlResult failure;
		auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &failure));
		QVERIFY(track != nullptr);
		Instrument* slicer = track->instrument();
		QVERIFY(slicer != nullptr);
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

QTEST_GUILESS_MAIN(SlicerTPlayheadTest)
#include "SlicerTPlayheadTest.moc"
