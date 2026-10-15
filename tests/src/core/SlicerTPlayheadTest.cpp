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

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AllocationProbe.h"
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

QString prepareSlicerTrack(QTemporaryDir& scratch, InstrumentTrack** track)
{
	const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("slicer-pool.wav")), 440.0);
	if (wav.isEmpty()) { return QStringLiteral("could not write the SlicerT fixture"); }
	Engine::getSong()->clearProject();
	const QString id = addTrack(QStringLiteral("instrument"));
	if (id.isEmpty()) { return QStringLiteral("could not add an instrument track"); }
	ControlResult failure;
	*track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &failure));
	if (*track == nullptr) { return QStringLiteral("track.add did not create an InstrumentTrack"); }
	InstrumentLoaderThread loader(nullptr, *track, QStringLiteral("slicert"));
	loader.start();
	if (!loader.wait()) { return QStringLiteral("the SlicerT loader did not finish"); }
	QCoreApplication::processEvents();
	Instrument* const slicer = (*track)->instrument();
	if (slicer == nullptr || slicer->thread() != QThread::currentThread())
	{
		return QStringLiteral("SlicerT was not installed on the control thread");
	}
	if (!QMetaObject::invokeMethod(slicer, "updateFile", Qt::DirectConnection, Q_ARG(QString, wav)))
	{
		return QStringLiteral("SlicerT did not accept the fixture file");
	}
	return {};
}

bool identicalAudio(const std::vector<SampleFrame>& left, std::span<const SampleFrame> right)
{
	if (left.size() != right.size()) { return false; }
	for (std::size_t frame = 0; frame < left.size(); ++frame)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			if (std::bit_cast<std::uint32_t>(left[frame][channel])
				!= std::bit_cast<std::uint32_t>(right[frame][channel]))
			{
				return false;
			}
		}
	}
	return true;
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
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
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
		// PluginView has no Q_OBJECT, so findChild<PluginView*> matched any QWidget (and Qt 6.12 refuses it): find the real one.
		gui::PluginView* view = nullptr;
		for (QWidget* child : window->findChildren<QWidget*>())
		{
			if ((view = dynamic_cast<gui::PluginView*>(child)) != nullptr) { break; }
		}
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

		// The first note above warms the worker and the note buffer path. A later
		// note still needs its own resampler state, but that state now comes from
		// the instrument's constructor-time pool. Deterministic processing keeps
		// this real NotePlayHandle render on the probing thread.
		track->processInEvent(noteOn);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		renderPeriod();
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});

		const ControlResult beforeOverflow = ControlRegistry::instance()->invoke(QStringLiteral("control.ping"));
		QVERIFY(beforeOverflow.ok);
		const qint64 overflowsBefore = beforeOverflow.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("slicert_state_overflow")).toInt();
		Note overlappingNote(TimePos(1000000), TimePos(0), track->baseNote());
		Engine::audioEngine()->setOverloadSheddingEnabled(false);
		for (int i = 0; i < 130; ++i)
		{
			auto* handle = NotePlayHandleManager::acquire(track, 0, 1000000, overlappingNote);
			QVERIFY(Engine::audioEngine()->addPlayHandle(handle));
		}
		renderPeriod();
		const ControlResult afterOverflow = ControlRegistry::instance()->invoke(QStringLiteral("control.ping"));
		QVERIFY(afterOverflow.ok);
		const qint64 overflowsAfter = afterOverflow.result.value(QStringLiteral("audio")).toObject()
			.value(QStringLiteral("slicert_state_overflow")).toInt();
		QVERIFY2(overflowsAfter > overflowsBefore, "SlicerT state-pool exhaustion was not reported by control.ping");
		track->processInEvent(noteOff);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int i = 0; i < 8; ++i) { renderPeriod(); }
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
	}

	//! Cluster 4467 round 2: every reused playback slot produces the fresh-state slice output bit-for-bit.
	void reusedPlaybackStateMatchesFreshStateAcrossManyNotes()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(scratch, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		const Note note(TimePos(1), TimePos(0), track->baseNote());
		std::vector<SampleFrame> freshOutput;

		for (int noteNumber = 0; noteNumber < 32; ++noteNumber)
		{
			auto* const handle = NotePlayHandleManager::acquire(track, 0,
				audio->framesPerPeriod() * 2, note);
			QVERIFY(audio->addPlayHandle(handle));
			audio->renderNextPeriod();
			const auto output = audio->renderNextPeriod();
			QVERIFY2(handle->m_pluginData != nullptr, "SlicerT did not claim a playback-state slot");
			if (noteNumber == 0)
			{
				freshOutput.assign(output.begin(), output.end());
			}
			else
			{
				QVERIFY2(identicalAudio(freshOutput, output),
					qPrintable(QStringLiteral("reused slice output differs from fresh state at note %1").arg(noteNumber)));
			}
			audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);
		}
	}

	//! Regression: SlicerT fade timing when playback speed differs from 1 (bug new-p12-251).
	//! At a frame ~1000 output frames before the end of a 0.5-speed slice, the corrected fade has not
	//! begun (remaining output frames > fadeOutFrames), so the output is still (near) full amplitude.
	//! The buggy formula instead treats that frame as deep inside the fade window.
	void fadeTimingStaysCorrectWhenSpeedDiffers()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(scratch, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		track->pitchModel()->setValue(1200.0f);   // pitchRatio = 0.5
		const Note note(TimePos(1), TimePos(0), track->baseNote());
		auto* const handle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 2000, note);
		QVERIFY(audio->addPlayHandle(handle));

		std::vector<SampleFrame> all;
		for (int i = 0; i < 1600; ++i)
		{
			const auto period = audio->renderNextPeriod();
			all.insert(all.end(), period.begin(), period.end());
		}

		int lastActive = static_cast<int>(all.size()) - 1;
		while (lastActive >= 0 && std::fabs(all[lastActive][0]) <= 1e-4f) { --lastActive; }
		QVERIFY(lastActive > 2000);
		const int target = lastActive - 1000;
		QVERIFY(target >= 0);
		const float amplitude = std::fabs(all[target][0]);

		QVERIFY2(amplitude > 0.3f,
			qPrintable(QStringLiteral("fade attenuation started too early: amplitude=%1").arg(amplitude)));
	}

	//! Cluster 4467 round 2: removing a track with a live SlicerT state destroys the note before the instrument.
	void deletingSlicerTrackCancelsActivePlaybackState()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(scratch, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		const Note note(TimePos(1000000), TimePos(0), track->baseNote());
		auto* const handle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 16, note);
		QVERIFY(audio->addPlayHandle(handle));
		audio->renderNextPeriod();
		QVERIFY2(handle->m_pluginData != nullptr, "the active SlicerT note has no pooled state to retire");

		const QString id = QStringLiteral("trk-%1").arg(track->id());
		const ControlResult removed = revtest::run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		audio->renderNextPeriod();
		const auto remainingNotes = std::count_if(audio->playHandles().begin(), audio->playHandles().end(),
			[](PlayHandle* item) { return item->type() == PlayHandle::Type::NotePlayHandle; });
		QCOMPARE(remainingNotes, qsizetype{0});
	}
};

QTEST_MAIN(SlicerTPlayheadTest)
#include "SlicerTPlayheadTest.moc"
