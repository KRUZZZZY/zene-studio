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
#include <cmath>
#include <cstdint>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AllocationProbe.h"
#include "AutomatableModel.h"
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

//! Appends \a value to \a bytes as a little-endian integer of \a size bytes.
void appendLittleEndian(QByteArray& bytes, quint32 value, int size)
{
	for (int i = 0; i < size; ++i) { bytes.append(static_cast<char>((value >> (8 * i)) & 0xff)); }
}

//! A constant-amplitude 16-bit mono WAV: the SlicerT fade is then the only thing that changes the
//! level, so the envelope can be read straight off the rendered frames.
QString writeConstantWav(const QString& path, int rate, int frames)
{
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i) { appendLittleEndian(pcm, 16000, 2); }

	QByteArray out;
	out.append("RIFF");
	appendLittleEndian(out, 36 + pcm.size(), 4);
	out.append("WAVEfmt ");
	appendLittleEndian(out, 16, 4);
	appendLittleEndian(out, 1, 2);
	appendLittleEndian(out, 1, 2);
	appendLittleEndian(out, rate, 4);
	appendLittleEndian(out, rate * 2, 4);
	appendLittleEndian(out, 2, 2);   // block align
	appendLittleEndian(out, 16, 2);  // bits per sample
	out.append("data");
	appendLittleEndian(out, pcm.size(), 4);
	out.append(pcm);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return {}; }
	file.write(out);
	file.close();
	return path;
}

//! The loudest level over \a count frames from \a from: the slice's own level over a stretch.
float peakLevel(const std::vector<SampleFrame>& frames, int from, int count)
{
	float peak = 0.0f;
	for (int i = std::max(from, 0); i < from + count && i < static_cast<int>(frames.size()); ++i)
	{
		peak = std::max(peak, std::fabs(frames[i][0]));
	}
	return peak;
}

//! The first frame at or after \a whose level passes \a threshold (over it when \a above is set).
int firstFrame(const std::vector<SampleFrame>& frames, int from, float threshold, bool above)
{
	for (int i = std::max(from, 0); i < static_cast<int>(frames.size()); ++i)
	{
		const float level = std::fabs(frames[i][0]);
		if (above ? level > threshold : level < threshold) { return i; }
	}
	return -1;
}

//! The last frame whose level is over \a threshold, or -1.
int lastFrameAbove(const std::vector<SampleFrame>& frames, float threshold)
{
	for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i)
	{
		if (std::fabs(frames[i][0]) > threshold) { return i; }
	}
	return -1;
}

//! One real render period, driven by the test (the device is stopped).
void renderPeriod()
{
	static_cast<void>(Engine::audioEngine()->renderNextPeriod());
}

//! Adds a SlicerT instrument track that plays \a wav without clearing existing tracks.
QString addSlicerTrack(const QString& wav, InstrumentTrack** track)
{
	if (wav.isEmpty()) { return QStringLiteral("could not write the SlicerT fixture"); }
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

QString prepareSlicerTrack(const QString& wav, InstrumentTrack** track)
{
	Engine::getSong()->clearProject();
	return addSlicerTrack(wav, track);
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

	//! Cluster 4467: the first note on a fresh SlicerT claims a preconstructed playback slot.
	//! Warm the worker and note-buffer paths on one track so the fresh track's first render is probed
	//! specifically for the per-note PlaybackState allocation.
	void firstNoteOnFreshSlicerInstrumentAllocatesNothing()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("first-note.wav")), 440.0);
		QVERIFY(!wav.isEmpty());
		Engine::getSong()->clearProject();
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);

		InstrumentTrack* warmTrack = nullptr;
		QString error = addSlicerTrack(wav, &warmTrack);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		const Note warmNote(TimePos(1), TimePos(0), warmTrack->baseNote());
		auto* const warmHandle = NotePlayHandleManager::acquire(warmTrack, 0,
			audio->framesPerPeriod() * 2, warmNote);
		QVERIFY(audio->addPlayHandle(warmHandle));
		audio->renderNextPeriod();

		InstrumentTrack* freshTrack = nullptr;
		error = addSlicerTrack(wav, &freshTrack);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		const Note firstNote(TimePos(1), TimePos(0), freshTrack->baseNote());
		auto* const firstHandle = NotePlayHandleManager::acquire(freshTrack, 0,
			audio->framesPerPeriod() * 2, firstNote);
		QVERIFY(audio->addPlayHandle(firstHandle));

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		audio->renderNextPeriod();
		test::tlCountAllocations = false;
		QVERIFY2(firstHandle->m_pluginData != nullptr,
			"the fresh SlicerT's first note did not claim a preconstructed playback slot");
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});

		audio->removePlayHandlesOfTypes(freshTrack, PlayHandle::Type::NotePlayHandle);
		audio->removePlayHandlesOfTypes(warmTrack, PlayHandle::Type::NotePlayHandle);
	}

	//! Cluster 4467 round 2: every reused playback slot produces the fresh-state slice output bit-for-bit.
	void reusedPlaybackStateMatchesFreshStateAcrossManyNotes()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(
			taketest::writeToneWav(scratch.filePath(QStringLiteral("slicer-pool.wav")), 440.0), &track);
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

	//! Cluster new-p12-251: the SlicerT fade is timed in OUTPUT frames. The remaining output frames of
	//! the slice are the remaining SOURCE frames times the speed ratio - the ratio is output/input
	//! (include/AudioResampler.h:97-116) - so the fade belongs in the last fadeOutFrames output frames.
	//! This plays a slice at speed 2 (BPM sync on, the original BPM twice the song tempo) and reads
	//! the envelope off the rendered frames: the divide formula the finding proposes would silence the
	//! slice a quarter of the way before its end.
	void fadeEnvelopeIsTimedInOutputFrames()
	{
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		const int rate = static_cast<int>(audio->outputSampleRate());
		QVERIFY(rate > 0);
		const int sourceFrames = rate / 2;            // half a second of source
		const int expectedOutput = sourceFrames * 2;  // the slice plays at speed 2
		const int fadeOutFrames = rate / 10;          // the 100 ms fade the test asks for

		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = writeConstantWav(scratch.filePath(QStringLiteral("constant.wav")), rate, sourceFrames);
		QVERIFY(!wav.isEmpty());

		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(wav, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		Instrument* const slicer = track->instrument();
		QVERIFY(slicer != nullptr);

		// The SlicerT's parameters, in the plugin's own declaration order (plugins/SlicerT/SlicerT.h:83-87).
		// The type and the range are checked, so a reordered plugin fails the test loudly.
		auto* const fadeOut = dynamic_cast<FloatModel*>(slicer->parameterModel(1));
		auto* const originalBpm = dynamic_cast<IntModel*>(slicer->parameterModel(2));
		auto* const bpmSync = dynamic_cast<BoolModel*>(slicer->parameterModel(4));
		QVERIFY(fadeOut != nullptr && fadeOut->maxValue() == 100.0f);
		QVERIFY(originalBpm != nullptr && originalBpm->maxValue() == 999);
		QVERIFY(bpmSync != nullptr);

		// BPM sync on, and the sample's own tempo twice the song's: the slice plays at speed 2.
		const int tempo = Engine::getSong()->getTempo();
		QVERIFY(tempo > 0 && tempo * 2 <= 999);
		bpmSync->setValue(1.0f);
		originalBpm->setValue(static_cast<float>(tempo * 2));
		fadeOut->setValue(100.0f);

		const Note note(TimePos(1), TimePos(0), track->baseNote());
		auto* const handle = NotePlayHandleManager::acquire(track, 0,
			expectedOutput + audio->framesPerPeriod() * 4, note);
		QVERIFY(audio->addPlayHandle(handle));

		std::vector<SampleFrame> rendered;
		const int periods = (expectedOutput + audio->framesPerPeriod() * 8) / audio->framesPerPeriod() + 4;
		for (int i = 0; i < periods; ++i)
		{
			const auto period = audio->renderNextPeriod();
			rendered.insert(rendered.end(), period.begin(), period.end());
		}

		const float peak = peakLevel(rendered, 0, static_cast<int>(rendered.size()));
		QVERIFY(peak > 1e-3f);
		// Where the slice starts, and the level of the constant signal itself: a quarter of the way
		// in, past the resampler's step transient at the start.
		const int origin = firstFrame(rendered, 0, 0.02f * peak, true);
		QVERIFY(origin >= 0);
		const float steady = peakLevel(rendered, origin + expectedOutput / 4, 64);
		QVERIFY(steady > 1e-3f);

		// The end of the slice's sound (the last frame above a fifth of the level) and the fade's
		// midpoint (the first frame below half of it).
		const int lastAudible = lastFrameAbove(rendered, 0.2f * steady);
		const int fadeMid = firstFrame(rendered, origin + expectedOutput / 2, 0.5f * steady, false);
		QVERIFY(lastAudible > origin && fadeMid > origin);

		// The slice plays at speed 2: it sounds for twice the source length. At speed 1 it would be
		// half of that, and the divide formula would cut it down to a quarter.
		const int audible = lastAudible - origin;
		QVERIFY2(audible > expectedOutput * 9 / 10 && audible <= expectedOutput,
			qPrintable(QStringLiteral("the slice sounded for %1 output frames, expected about %2")
				.arg(audible).arg(expectedOutput)));

		// The fade sits in the last fadeOutFrames output frames: its midpoint - the half-level
		// crossing of the cosine ramp - is half a fade window before the end of the slice. The
		// window allows the sample staging's own granularity (the fade value is computed from the
		// staged source position, include/Sample.h:72), which is half a fade window at most.
		const int expectedMid = origin + expectedOutput - fadeOutFrames / 2;
		QVERIFY2(std::abs(fadeMid - expectedMid) < fadeOutFrames / 5,
			qPrintable(QStringLiteral("the fade midpoint is at %1, expected about %2")
				.arg(fadeMid).arg(expectedMid)));

		// Before the fade the slice is still at its own level.
		const int beforeFade = origin + expectedOutput * 85 / 100;
		QVERIFY(beforeFade < static_cast<int>(rendered.size()));
		QVERIFY2(std::fabs(rendered[beforeFade][0]) > 0.9f * steady, "the slice was attenuated before the fade");
	}

	//! Cluster 4467 round 2: removing a track with a live SlicerT state destroys the note before the instrument.
	void deletingSlicerTrackCancelsActivePlaybackState()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(
			taketest::writeToneWav(scratch.filePath(QStringLiteral("slicer-pool.wav")), 440.0), &track);
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
