/*
 * SlicerTSampleSwapGuiTest.cpp - replacing the SlicerT sample through its own editor, while a note sounds
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

// Cluster new-p9-431. The render thread used to read the instrument's editing state (m_originalSample and
// m_slicePoints) while the editor replaced it, so a sample dropped or opened while a note was sounding
// could switch that note onto a buffer the same edit was about to release. The fix publishes an immutable
// snapshot that a note pins on its first period (plugins/SlicerT/SlicerT.cpp, publishPlaybackSnapshot).
//
// These are the GUI acceptance paths, driven through the plug-in's own editor rather than its slot:
//   - the "samplefile" drop a browser file drag carries (SlicerTView::dropEvent),
//   - the "clip_2" drop a sample clip drag carries (the same handler, the clip's DataFile),
//   - the file-open dialog (SlicerTView::openFiles, with the real FileDialog answered inside its own
//     event loop), including the cancelled dialog,
//   - a payload the editor does not take.
// Each fixture is a constant at its own level, so "which file is this note rendering" is a measurement,
// and a note that renders nothing cannot pass (silence is outside every band). The last test covers the
// slice map: the per-slice controls a sounding note applies must belong to the interval it renders, even
// after a slice point is deleted and SlicerT::sliceMerged shifts the control banks.

#include <QtTest>
#include <QApplication>
#include <QDropEvent>
#include <QFileDialog>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "NotePlayHandle.h"
#include "SongEditor.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "PluginView.h"
#include "StringPairDrag.h"
#include "GuiTestApplication.h"

using namespace lmms;

namespace
{

//! The fixture rate: 44.1 kHz throughout, so slice fractions map to whole frames.
constexpr int kRate = 44100;

//! Adds a track of \a type through the registered command, the way the editor does.
QString addTrack(const QString& type = QStringLiteral("instrument"))
{
	const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
		QJsonObject{{QStringLiteral("type"), type}});
	return added.ok ? added.result.value(QStringLiteral("track")).toString() : QString();
}

//! Appends \a value to \a bytes as a little-endian integer of \a size bytes.
void appendLittleEndian(QByteArray& bytes, quint32 value, int size)
{
	for (int i = 0; i < size; ++i) { bytes.append(static_cast<char>((value >> (8 * i)) & 0xff)); }
}

//! A constant-amplitude 16-bit mono WAV: \a level (0..32767) over \a frames.
QString writeLevelWav(const QString& path, int rate, int frames, quint32 level)
{
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i) { appendLittleEndian(pcm, level, 2); }

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

//! The loudest level over \a count frames from \a from.
float peakLevel(const std::vector<SampleFrame>& frames, int from, int count)
{
	float peak = 0.0f;
	for (int i = std::max(from, 0); i < from + count && i < static_cast<int>(frames.size()); ++i)
	{
		peak = std::max(peak, std::fabs(frames[i][0]));
	}
	return peak;
}

//! True when \a value lies inside [\a low, \a high]: the band one fixture's own level occupies.
bool inBand(float value, float low, float high) { return value >= low && value <= high; }

//! The loudest level of the last of \a periods real render periods; the test drives the device itself.
float lastPeriodPeak(AudioEngine* audio, int periods)
{
	float peak = 0.0f;
	for (int i = 0; i < periods; ++i)
	{
		const auto period = audio->renderNextPeriod();
		peak = peakLevel(std::vector<SampleFrame>(period.begin(), period.end()), 0,
			static_cast<int>(period.size()));
	}
	return peak;
}

//! Adds a SlicerT instrument track that plays \a wav, after clearing the project.
QString prepareSlicerTrack(const QString& wav, InstrumentTrack** track)
{
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
	if ((*track)->instrument() == nullptr) { return QStringLiteral("SlicerT was not installed"); }
	if (!QMetaObject::invokeMethod((*track)->instrument(), "updateFile", Qt::DirectConnection,
		Q_ARG(QString, wav)))
	{
		return QStringLiteral("SlicerT did not accept the fixture file");
	}
	return {};
}

//! The first child of \a parent whose runtime class name ends with \a classSuffix.
QWidget* childOfClass(QWidget* parent, const QString& classSuffix)
{
	for (QWidget* child : parent->findChildren<QWidget*>())
	{
		if (QString::fromLatin1(child->metaObject()->className()).endsWith(classSuffix)) { return child; }
	}
	return nullptr;
}

//! The instrument window and plug-in view the GUI built for \a track (the drag-and-drop path).
bool slicerGui(InstrumentTrack* track, gui::InstrumentTrackWindow** window, gui::PluginView** view)
{
	*window = nullptr;
	*view = nullptr;
	for (gui::TrackView* candidate : gui::getGUI()->songEditor()->m_editor->trackViews())
	{
		if (candidate->getTrack() != track) { continue; }
		if (auto* trackView = dynamic_cast<gui::InstrumentTrackView*>(candidate))
		{
			*window = trackView->getInstrumentTrackWindow();
		}
	}
	if (*window == nullptr) { return false; }
	// PluginView has no Q_OBJECT, so findChild<PluginView*> matches any QWidget: dynamic_cast instead.
	for (QWidget* child : (*window)->findChildren<QWidget*>())
	{
		*view = dynamic_cast<gui::PluginView*>(child);
		if (*view != nullptr) { return true; }
	}
	return false;
}

//! One StringPair drop on \a target: the payload a drag out of the browser carries. The drag-enter
//! comes first, the way the platform delivers a drag, then the drop itself.
bool sendStringPairDrop(QWidget* target, const QString& key, const QString& value)
{
	const std::unique_ptr<QMimeData> mime{gui::StringPairDrag::createMimeData(key, value)};
	const QPoint point(target->width() / 2, target->height() / 2);
	QDragEnterEvent enter(point, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(target, &enter);
	QDropEvent drop(QPointF(point), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	drop.setAccepted(false);
	QApplication::sendEvent(target, &drop);
	return enter.isAccepted();
}

//! The payload a drag of a sample clip carries: the clip's own document, whose clip element names the
//! file as its "src" attribute (SampleClip::saveSettings), inside the type element DataFile resolves.
QString sampleClipPayload(const QString& wav)
{
	return QStringLiteral("<?xml version=\"1.0\"?><zene-project version=\"1\" type=\"dnddata\">"
		"<dnddata><sampleclip src=\"%1\"/></dnddata></zene-project>").arg(wav);
}

//! Schedules the answer to the next FileDialog FileDialog::openAudioFile() opens: accept @a wav, or
//! cancel when it is empty. It runs inside the dialog's own event loop, so nothing waits on a clock.
void answerNextFileDialog(const QString& wav, bool* answered)
{
	*answered = false;
	QTimer::singleShot(0, qApp, [wav, answered]() {
		auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr) { return; }
		// QFileDialog re-declares accept() as protected, so the answer goes through QDialog's slot.
		if (wav.isEmpty()) { static_cast<QDialog*>(dialog)->reject(); }
		else
		{
			dialog->selectFile(wav);
			static_cast<QDialog*>(dialog)->accept();
		}
		*answered = true;
	});
}

//! One mouse event on \a target in the coordinates the widget itself sees.
void sendWidgetMouse(QWidget* target, QEvent::Type type, const QPoint& point, Qt::MouseButton button)
{
	const Qt::MouseButtons buttons = type == QEvent::MouseButtonPress ? button : Qt::NoButton;
	QMouseEvent event(type, QPointF(point), QPointF(target->mapToGlobal(point)), button, buttons,
		Qt::NoModifier);
	QApplication::sendEvent(target, &event);
}

//! The SlicerT slice map's size, read through the plug-in's own invokable (no plug-in header needed).
int slicerSliceCount(Instrument* slicer)
{
	int count = -1;
	QMetaObject::invokeMethod(slicer, "sliceCount", Q_RETURN_ARG(int, count));
	return count;
}

//! Writes one per-slice control through the plug-in's own invokable: the path plugin.slice_set takes.
bool setSlicerSliceControl(Instrument* slicer, int slice, const char* name, double value)
{
	bool written = false;
	QMetaObject::invokeMethod(slicer, "setSliceControl", Q_RETURN_ARG(bool, written), Q_ARG(int, slice),
		Q_ARG(QString, QString::fromLatin1(name)), Q_ARG(double, value));
	return written;
}

} // namespace

class SlicerTSampleSwapGuiTest : public QObject
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

	//! BUG new-p9-431, the drop routes. Both payloads a sample drag carries go through
	//! SlicerTView::dropEvent. Neither may switch what a sounding note is rendering - that note keeps the
	//! file it started on - and both must change what the NEXT note plays. A payload the editor does not
	//! take changes nothing. The three fixtures are constants at different levels, so each assertion
	//! names one file, and a silent render cannot satisfy any of them.
	void droppingASampleKeepsASoundingNoteOnItsSource()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString held = writeLevelWav(scratch.filePath(QStringLiteral("held.wav")), kRate,
			kRate * 3, 16000);     // 0.488
		const QString first = writeLevelWav(scratch.filePath(QStringLiteral("first.wav")), kRate,
			kRate * 3, 3000);      // 0.092
		const QString second = writeLevelWav(scratch.filePath(QStringLiteral("second.wav")), kRate,
			kRate * 3, 8000);      // 0.244
		QVERIFY(!held.isEmpty() && !first.isEmpty() && !second.isEmpty());

		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(held, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		gui::InstrumentTrackWindow* window = nullptr;
		gui::PluginView* view = nullptr;
		QVERIFY(slicerGui(track, &window, &view));
		const Note note(TimePos(1), TimePos(0), track->baseNote());

		// A payload the editor does not take leaves the instrument alone: a note still plays the file.
		QVERIFY2(!sendStringPairDrop(view, QStringLiteral("note"), QStringLiteral("60")),
			"the editor accepted a payload it cannot take");
		auto* const refusedHandle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 8, note);
		QVERIFY(audio->addPlayHandle(refusedHandle));
		const float refused = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(refused, 0.42f, 0.56f), qPrintable(QStringLiteral("a refused drop loaded "
			"something: a fresh note measured %1").arg(refused)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);

		// A sounding note, held through both drops.
		auto* const heldHandle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 12, note);
		QVERIFY(audio->addPlayHandle(heldHandle));
		const float started = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(started, 0.42f, 0.56f), qPrintable(QStringLiteral("the held note does not play "
			"the file it started on: peak %1").arg(started)));

		QVERIFY2(sendStringPairDrop(view, QStringLiteral("samplefile"), first),
			"the editor refused the browser's sample drag");
		const float afterFileDrop = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(afterFileDrop, 0.42f, 0.56f), qPrintable(QStringLiteral("the samplefile drop "
			"switched the sounding note: peak %1").arg(afterFileDrop)));

		QVERIFY2(sendStringPairDrop(view, QStringLiteral("clip_%1").arg(static_cast<int>(Track::Type::Sample)),
			sampleClipPayload(second)), "the editor refused a sample clip drag");
		const float afterClipDrop = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(afterClipDrop, 0.42f, 0.56f), qPrintable(QStringLiteral("the sample-clip drop "
			"switched the sounding note: peak %1").arg(afterClipDrop)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);

		// A note started afterwards plays the last dropped file - a distinctive, nonzero level.
		auto* const freshHandle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 8, note);
		QVERIFY(audio->addPlayHandle(freshHandle));
		const float fresh = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(fresh, 0.18f, 0.31f), qPrintable(QStringLiteral("a note started after the drops "
			"measured %1, not the dropped file's 0.244 level").arg(fresh)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);
	}

	//! BUG new-p9-431, the file-open route: SlicerTView::openFiles() runs the real FileDialog. Cancelling
	//! it changes nothing, accepting it replaces what the NEXT note plays, and neither switches a
	//! sounding note, which keeps the file it started on.
	void openingASampleKeepsASoundingNoteOnItsSource()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString held = writeLevelWav(scratch.filePath(QStringLiteral("held.wav")), kRate,
			kRate * 3, 16000);     // 0.488
		const QString opened = writeLevelWav(scratch.filePath(QStringLiteral("opened.wav")), kRate,
			kRate * 3, 5000);      // 0.153
		QVERIFY(!held.isEmpty() && !opened.isEmpty());

		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(held, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		gui::InstrumentTrackWindow* window = nullptr;
		gui::PluginView* view = nullptr;
		QVERIFY(slicerGui(track, &window, &view));
		const Note note(TimePos(1), TimePos(0), track->baseNote());

		auto* const heldHandle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 12, note);
		QVERIFY(audio->addPlayHandle(heldHandle));
		const float started = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(started, 0.42f, 0.56f), qPrintable(QStringLiteral("the held note does not play "
			"the file it started on: peak %1").arg(started)));

		// Cancelled dialog: nothing is loaded, and the sounding note is untouched.
		bool answered = false;
		answerNextFileDialog(QString(), &answered);
		QVERIFY(QMetaObject::invokeMethod(view, "openFiles", Qt::DirectConnection));
		QVERIFY2(answered, "the file dialog was never answered");
		const float afterCancel = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(afterCancel, 0.42f, 0.56f), qPrintable(QStringLiteral("a cancelled file-open "
			"changed the sounding note: peak %1").arg(afterCancel)));

		// Accepted dialog: the replacement becomes the live sample, but not this note's source.
		answerNextFileDialog(opened, &answered);
		QVERIFY(QMetaObject::invokeMethod(view, "openFiles", Qt::DirectConnection));
		QVERIFY2(answered, "the file dialog was never answered");
		const float afterOpen = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(afterOpen, 0.42f, 0.56f), qPrintable(QStringLiteral("the file-open switched the "
			"sounding note: peak %1").arg(afterOpen)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);

		// A note started after the open plays the opened file - a distinctive, nonzero level.
		auto* const freshHandle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 8, note);
		QVERIFY(audio->addPlayHandle(freshHandle));
		const float fresh = lastPeriodPeak(audio, 2);
		QVERIFY2(inBand(fresh, 0.11f, 0.20f), qPrintable(QStringLiteral("a note started after the "
			"file-open measured %1, not the opened file's 0.153 level").arg(fresh)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);
	}

	//! BUG new-p9-431, the slice map half: the per-slice controls a sounding note applies must belong to
	//! the interval it renders. Deleting a slice point merges two slices and shifts the control banks
	//! (SlicerT::sliceMerged), so a note that kept its pinned interval must follow the controls of the
	//! live slice that interval sits in - not those of whichever slice took over its old index. The three
	//! slices carry distinct adjacent gains, so the level names the controls that were applied.
	void heldSliceNoteKeepsTheControlsOfItsOwnSlice()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wav = writeLevelWav(scratch.filePath(QStringLiteral("slices.wav")), kRate,
			kRate * 3, 16000);
		QVERIFY(!wav.isEmpty());

		InstrumentTrack* track = nullptr;
		const QString error = prepareSlicerTrack(wav, &track);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		AudioEngine* const audio = Engine::audioEngine();
		audio->setOverloadSheddingEnabled(false);
		Instrument* const slicer = track->instrument();
		gui::InstrumentTrackWindow* window = nullptr;
		gui::PluginView* view = nullptr;
		QVERIFY(slicer != nullptr && slicerGui(track, &window, &view));

		// Two double clicks in the waveform editor insert two interior slice points: three slices.
		window->resize(760, 560);
		window->show();
		QCoreApplication::processEvents();
		QWidget* const waveform = childOfClass(window, QStringLiteral("SlicerTWaveform"));
		QVERIFY(waveform != nullptr);
		waveform->resize(248, 128);
		QCoreApplication::processEvents();
		QVERIFY2(waveform->width() >= 200 && waveform->height() > 40,
			qPrintable(QStringLiteral("the waveform editor has no usable geometry: %1x%2")
				.arg(waveform->width()).arg(waveform->height())));
		const QPoint firstPoint(waveform->width() / 3, 80);
		const QPoint secondPoint(waveform->width() * 2 / 3, 80);
		sendWidgetMouse(waveform, QEvent::MouseButtonDblClick, firstPoint, Qt::LeftButton);
		sendWidgetMouse(waveform, QEvent::MouseButtonDblClick, secondPoint, Qt::LeftButton);
		QVERIFY2(slicerSliceCount(slicer) == 3, qPrintable(QStringLiteral("the waveform left %1 slices, "
			"expected 3").arg(slicerSliceCount(slicer))));

		// Distinct adjacent controls: -1, -3 and -6 dB.
		QVERIFY(setSlicerSliceControl(slicer, 0, "gain", -1.0));
		QVERIFY(setSlicerSliceControl(slicer, 1, "gain", -3.0));
		QVERIFY(setSlicerSliceControl(slicer, 2, "gain", -6.0));

		// A note on the second slice (base note + 2) renders the middle third at its own -3 dB.
		const Note sliceNote(TimePos(1), TimePos(0), track->baseNote() + 2);
		auto* const handle = NotePlayHandleManager::acquire(track, 0,
			audio->framesPerPeriod() * 12, sliceNote);
		QVERIFY(audio->addPlayHandle(handle));
		const float before = lastPeriodPeak(audio, 2);
		QVERIFY2(before > 0.2f, qPrintable(QStringLiteral("the slice note is silent: peak %1").arg(before)));

		// Delete the first interior point while it sounds: slices 0 and 1 merge (the merged slice keeps
		// slice 0's -1 dB) and the old slice 2's -6 dB moves into slot 1, where this note's index was.
		sendWidgetMouse(waveform, QEvent::MouseMove, firstPoint, Qt::NoButton);
		sendWidgetMouse(waveform, QEvent::MouseButtonPress, firstPoint, Qt::RightButton);
		QVERIFY2(slicerSliceCount(slicer) == 2, qPrintable(QStringLiteral("the right click left %1 "
			"slices, expected 2").arg(slicerSliceCount(slicer))));

		const float after = lastPeriodPeak(audio, 2);
		const float ratio = after / before;
		QVERIFY2(inBand(ratio, 1.15f, 1.38f), qPrintable(QStringLiteral("the held note applied another "
			"slice's controls after the merge: level ratio %1 (expected 1.26 for the merged -1 dB slice, "
			"0.71 for the -6 dB slice that took its index)").arg(ratio)));
		audio->removePlayHandlesOfTypes(track, PlayHandle::Type::NotePlayHandle);
	}
};

QTEST_MAIN(SlicerTSampleSwapGuiTest)
#include "SlicerTSampleSwapGuiTest.moc"
