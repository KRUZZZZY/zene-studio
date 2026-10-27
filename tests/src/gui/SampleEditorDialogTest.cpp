/*
 * SampleEditorDialogTest.cpp - the sample editor's typed range, zoom and handles (0.5.0 theme 15)
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

/*! 0.5.0 theme 15: the sample editor's range as Start, End and Length fields - read and typed in time, bars|beats
 *  or samples, stepped by the arrow keys - the waveform's zoom and edge handles, and the operations acting on
 *  exactly the range the fields show, a refusal leaving it as it was. */

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QApplication>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <limits>
#include <vector>

#include "AccessibleCanvas.h"
#include "ControlRegistry.h"
#include "Editor.h"
#include "Clipboard.h"
#include "Engine.h"
#include "Graph.h"
#include "Sample.h"
#include "FileDialog.h"
#include "Graph.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleEditorDialog.h"
#include "SampleThumbnail.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int Rate = 44100;

class TestEditor final : public Editor
{
public:
	TestEditor() : Editor() {}

protected:
	void play() override {}
};

//! A clip of one second of a quiet tone at 44.1 kHz.
SampleClip* toneClip(Track* track)
{
	std::vector<SampleFrame> frames;
	for (int i = 0; i < Rate; ++i)
	{
		const auto v = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 220.0 * i / Rate));
		frames.emplace_back(v, v);
	}
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), Rate));
	return clip;
}

SampleFrameField* field(QWidget* dialog, const QString& name)
{
	// SampleFrameField has no Q_OBJECT, so findChildren<SampleFrameField*> matched any QSpinBox (and Qt 6.12 refuses it).
	for (auto* spinBox : dialog->findChildren<QSpinBox*>())
	{
		auto* candidate = dynamic_cast<SampleFrameField*>(spinBox);
		if (candidate != nullptr && candidate->accessibleName() == name) { return candidate; }
	}
	return nullptr;
}

QPushButton* button(QWidget* dialog, const QString& text)
{
	for (auto* candidate : dialog->findChildren<QPushButton*>())
	{
		if (candidate->text() == text) { return candidate; }
	}
	return nullptr;
}

void type(SampleFrameField* into, const QString& text)
{
	into->setFocus();
	into->selectAll();
	QTest::keyClicks(into, text);
	QTest::keyClick(into, Qt::Key_Enter);
}

class TestGraph final : public Graph
{
public:
	using Graph::Graph;

	void deliverDrop(QDropEvent* event) { dropEvent(event); }
	void deliverDragEnter(QDragEnterEvent* event) { dragEnterEvent(event); }
};

} // namespace

class SampleEditorDialogTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_clip = toneClip(m_track);
		m_dialog = new SampleEditorDialog(m_clip);
		m_dialog->setAttribute(Qt::WA_DeleteOnClose, false);
		m_dialog->resize(800, 360);
		m_dialog->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_dialog));
		m_dialog->setUnit(samplerange::Unit::Time);
	}

	void cleanup()
	{
		delete m_dialog;
		m_dialog = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void lastPlayedEditorClearsWhenDestroyed()
	{
		auto* editor = new TestEditor;
		editor->togglePlayStop();
		QCOMPARE(Editor::lastPlayedEditor(), static_cast<Editor*>(editor));

		delete editor;
		QVERIFY(Editor::lastPlayedEditor() == nullptr);
	}

	void editorOwnsOnlyItsTransportToolbar()
	{
		TestEditor editor;
		QCOMPARE(editor.findChildren<DropToolBar*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
	}

	void openForGivesEveryDialogAView()
	{
		// new-p21-61: window.sample_editor's applyZoom was reported to dereference a possibly null view. The view is a
		// member built in the constructor's initialiser list, so a dialog from openFor - new or reopened - always has one.
		SampleClip* clip = toneClip(m_track);
		SampleEditorDialog* dialog = SampleEditorDialog::openFor(clip, nullptr);
		QVERIFY(dialog != nullptr);
		QVERIFY(dialog->view() != nullptr);
		QCOMPARE(dialog->clip(), clip);
		QCOMPARE(SampleEditorDialog::openFor(clip, nullptr), dialog);
		QVERIFY(dialog->view() != nullptr);
		delete dialog;
		delete clip;
	}

	void graphClickAtFivePixelHeightKeepsSamplesFinite()
	{
		Graph graph(nullptr, Graph::Style::Linear, 16, 5);
		graph.setFixedSize(16, 5);
		graph.show();
		QVERIFY(QTest::qWaitForWindowExposed(&graph));
		QCOMPARE(graph.height(), 5);

		QTest::mouseClick(&graph, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));

		QVERIFY(std::isfinite(graph.model()->samples()[0]));
	}

	void graphDrawAtFivePixelHeightKeepsSamplesFinite()
	{
		Graph graph(nullptr, Graph::Style::Linear, 16, 5);
		graph.setFixedSize(16, 5);
		graph.show();
		QVERIFY(QTest::qWaitForWindowExposed(&graph));
		QCOMPARE(graph.height(), 5);

		QTest::mousePress(&graph, Qt::LeftButton, Qt::NoModifier, QPoint(2, 3));
		std::vector<float> samples(static_cast<std::size_t>(graph.model()->length()), 0.25f);
		graph.model()->setSamples(samples.data());
		QTest::mouseMove(&graph, QPoint(3, 2));
		QTest::mouseRelease(&graph, Qt::LeftButton, Qt::NoModifier, QPoint(3, 2));

		QVERIFY(std::isfinite(graph.model()->samples()[0]));
	}

	void zoomingPastCachedThumbnailStillDrawsTheWaveform()
	{
		std::vector<SampleFrame> frames(100);
		std::fill(frames.begin() + 10, frames.end(), SampleFrame(0.5f, 0.5f));
		const Sample sample{std::make_shared<SampleBuffer>(frames.data(), frames.size(), Rate)};
		const SampleThumbnail thumbnail{sample};
		QImage image(21, 20, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setPen(Qt::white);
		thumbnail.visualize({.sampleRect = image.rect()}, painter);
		painter.end();

		QCOMPARE(image.pixelColor(10, 4), QColor(Qt::white));

		// The invalid reverse-iterator read can appear to work in a release build; pin its bound to the selected source.
		QFile implementation(QStringLiteral("../../src/gui/SampleThumbnail.cpp"));
		QVERIFY(implementation.open(QIODevice::ReadOnly));
		const QByteArray source = implementation.readAll();
		const auto boundsStart = source.indexOf("const auto beginIndex");
		const auto aggregationStart = source.indexOf("auto minPeak", boundsStart);
		QVERIFY(boundsStart >= 0 && aggregationStart > boundsStart);
		const auto bounds = source.mid(boundsStart, aggregationStart - boundsStart);
		QVERIFY(bounds.contains("finerThumbnailWidth - 1"));
		QVERIFY(!bounds.contains("finerThumbnail->"));
	}

	void textReadsAndTypesInEachUnit()
	{
		const samplerange::Context ctx{Rate, 0};
		using samplerange::Unit;
		QCOMPARE(samplerange::frameText(66150, ctx, Unit::Time), QStringLiteral("0:01.500"));
		QCOMPARE(samplerange::frameText(100, ctx, Unit::Samples), QStringLiteral("100 smp"));
		qint64 frame = 0;
		QVERIFY(samplerange::frameFromText(QStringLiteral("1:02.5"), ctx, Unit::Time, &frame));
		QCOMPARE(frame, qint64{62500} * Rate / 1000);
		QVERIFY(samplerange::frameFromText(QStringLiteral("0.25 s"), ctx, Unit::Samples, &frame));  // a suffix names its unit
		QCOMPARE(frame, qint64{Rate / 4});
		// The clock format does not skip the suffix: a colon and a "s" word name the same unit together.
		QVERIFY(samplerange::frameFromText(QStringLiteral("1:02.500 s"), ctx, Unit::Time, &frame));
		QCOMPARE(frame, qint64{62500} * Rate / 1000);
		QVERIFY(samplerange::frameFromText(QStringLiteral("1:02 sec"), ctx, Unit::Time, &frame));
		QCOMPARE(frame, qint64{62} * Rate);
		QVERIFY(samplerange::frameFromText(QStringLiteral("0:30 s"), ctx, Unit::Time, &frame));
		QCOMPARE(frame, qint64{30} * Rate);
		QVERIFY(samplerange::frameFromText(QStringLiteral("2756 smp"), ctx, Unit::Time, &frame));
		QCOMPARE(frame, qint64{2756});
		QVERIFY(samplerange::frameFromText(QStringLiteral("300"), ctx, Unit::Samples, &frame));
		QCOMPARE(frame, qint64{300});
		QVERIFY(!samplerange::frameFromText(QStringLiteral("soon"), ctx, Unit::Time, &frame));
		QVERIFY(!samplerange::frameFromText(QStringLiteral("1:75"), ctx, Unit::Time, &frame));
		// Bars|beats at the song's tempo from the clip's start: bar 2 is one bar of the song's time in.
		QVERIFY(samplerange::frameFromText(QStringLiteral("2.1.1"), ctx, Unit::BarsBeats, &frame));
		QCOMPARE(frame, std::llround(Engine::getSong()->secondsAtTick(Engine::getSong()->ticksPerBar()) * Rate));
		QCOMPARE(samplerange::unitFor(musicaltime::Format::Ticks), Unit::Samples);
	}

	void firstShiftClickEditsOnlyTheClickedSample()
	{
		Graph graph(nullptr, Graph::Style::Bar, 104, 100);
		std::vector<float> initial(graph.model()->length(), 0.0f);
		graph.model()->setSamples(initial.data());

		QTest::mouseClick(&graph, Qt::LeftButton, Qt::ShiftModifier, QPoint(52, 50));

		const int clickedSample = static_cast<int>(50.0f * graph.model()->length() / (graph.width() - 4));
		QVERIFY(graph.model()->samples()[clickedSample] != 0.0f);
		for (int i = 0; i < graph.model()->length(); ++i)
		{
			if (i != clickedSample) { QCOMPARE(graph.model()->samples()[i], 0.0f); }
		}
	}

	void graphModelRejectsLengthsBelowTwoSamples()
	{
		graphModel model(-1.0f, 1.0f, 4, nullptr);
		QSignalSpy lengthChanges(&model, &graphModel::lengthChanged);
		QVERIFY(lengthChanges.isValid());

		for (const int invalidLength : {-1, 0, 1})
		{
			model.setLength(invalidLength);
			QCOMPARE(model.length(), 4);
		}
		QCOMPARE(lengthChanges.count(), 0);

		model.setLength(2);
		QCOMPARE(model.length(), 2);
		QCOMPARE(lengthChanges.count(), 1);
		model.smooth();
		model.normalize();

		graphModel oneSampleModel(-1.0f, 1.0f, 1, nullptr);
		QCOMPARE(oneSampleModel.length(), 2);
		oneSampleModel.smooth();
		oneSampleModel.normalize();
	}

	void playbackCursorChecksVisibleRangeBeforeUnsignedSubtraction()
	{
		// The product view is a separately built plugin, so enforce its range-check contract here.
		QFile file(QStringLiteral("../../plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp"));
		QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
		const QByteArray source = file.readAll();
		const auto paintEvent = source.indexOf("void AudioFileProcessorWaveView::paintEvent");
		QVERIFY(paintEvent >= 0);
		const auto rangeStartCheck = source.indexOf("m_framesPlayed >= static_cast<f_cnt_t>(m_from)", paintEvent);
		const auto rangeEndCheck = source.indexOf("m_framesPlayed <= static_cast<f_cnt_t>(m_to)", paintEvent);
		const auto frameOffset = source.indexOf("m_framesPlayed - static_cast<f_cnt_t>(m_from)", paintEvent);
		QVERIFY(rangeStartCheck >= paintEvent);
		QVERIFY(rangeEndCheck >= paintEvent);
		QVERIFY(frameOffset >= paintEvent);
		QVERIFY(rangeStartCheck < frameOffset);
		QVERIFY(rangeEndCheck < frameOffset);
	}

	void theFieldsShowTheSelection()
	{
		m_dialog->setRange(4410, 22050);
		QCOMPARE(field(m_dialog, QStringLiteral("Range start"))->text(), QStringLiteral("0:00.100"));
		QCOMPARE(field(m_dialog, QStringLiteral("Range end"))->text(), QStringLiteral("0:00.500"));
		QCOMPARE(field(m_dialog, QStringLiteral("Range length"))->text(), QStringLiteral("0:00.400"));
		QVERIFY(m_dialog->view()->accessibleDescription().contains(QStringLiteral("0:00.400 long")));
		m_dialog->setUnit(samplerange::Unit::Samples);
		QCOMPARE(field(m_dialog, QStringLiteral("Range start"))->text(), QStringLiteral("4410 smp"));
		const QJsonObject state = m_dialog->describe();
		QCOMPARE(state.value(QStringLiteral("from")).toInt(), 4410);
		QCOMPARE(state.value(QStringLiteral("length")).toInt(), 22050 - 4410);
		QCOMPARE(state.value(QStringLiteral("unit")).toString(), QStringLiteral("samples"));
	}

	void sampleFrameCountsBeyondIntAreRejected()
	{
		const auto maxFrames = static_cast<std::size_t>(std::numeric_limits<int>::max());
		const auto representable = sampleeditor::detail::representableFrameCount(maxFrames);
		QVERIFY(representable.has_value());
		QCOMPARE(*representable, std::numeric_limits<int>::max());
		QVERIFY(!sampleeditor::detail::representableFrameCount(maxFrames + 1).has_value());
	}

	void typedValuesSetTheRange()
	{
		m_dialog->setRange(4410, 22050);
		type(field(m_dialog, QStringLiteral("Range start")), QStringLiteral("0:00.250"));
		QCOMPARE(m_dialog->view()->from(), Rate / 4);
		QCOMPARE(m_dialog->view()->to(), 22050);
		type(field(m_dialog, QStringLiteral("Range length")), QStringLiteral("441 smp"));
		QCOMPARE(m_dialog->view()->to(), Rate / 4 + 441);
		type(field(m_dialog, QStringLiteral("Range end")), QStringLiteral("0.75"));
		QCOMPARE(m_dialog->view()->to(), Rate * 3 / 4);
		// The arrow keys step a field: a frame in samples.
		m_dialog->setUnit(samplerange::Unit::Samples);
		SampleFrameField* start = field(m_dialog, QStringLiteral("Range start"));
		start->setFocus();
		QTest::keyClick(start, Qt::Key_Up);
		QCOMPARE(m_dialog->view()->from(), Rate / 4 + 1);
	}

	void theWaveformZoomsAndItsEdgesDrag()
	{
		SampleSelectionView* view = m_dialog->view();
		m_dialog->setRange(Rate / 4, Rate / 2);
		view->zoomToSelection();
		QVERIFY(view->viewFrames() < view->frames());
		QVERIFY(view->viewFrom() <= Rate / 4 && view->viewFrom() + view->viewFrames() >= Rate / 2);
		view->showAll();
		QCOMPARE(view->viewFrom(), 0);
		QCOMPARE(view->viewFrames(), view->frames());
		// Dragging the end's handle moves only the end.
		const int endX = static_cast<int>(static_cast<double>(Rate / 2) * view->width() / view->frames());
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(endX, view->height() / 2));
		QTest::mouseMove(view, QPoint(endX + 80, view->height() / 2));
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(endX + 80, view->height() / 2));
		QCOMPARE(view->from(), Rate / 4);
		QVERIFY(view->to() > Rate / 2);
	}

	//! Q050-099: the waveform is a focusable canvas with a cursor, range selection and
	//! zoom from the keyboard, and an applied range edit is one undo step.
	void theWaveformTakesKeysForCursorRangeZoomAndEdit()
	{
		SampleSelectionView* view = m_dialog->view();
		QCOMPARE(a11y::canvasRole(view), QAccessible::Canvas);
		QVERIFY((view->focusPolicy() & Qt::TabFocus) != 0);
		QCOMPARE(view->accessibleName(), QStringLiteral("Waveform"));
		view->setFocus();
		QVERIFY(view->hasFocus());

		QTest::keyClick(view, Qt::Key_Right);
		const int step = std::max(1, view->viewFrames() / 100);
		QCOMPARE(view->from(), step);
		QCOMPARE(view->to(), step);
		QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
		QCOMPARE(view->from(), step);
		QCOMPARE(view->to(), 2 * step);
		QTest::keyClick(view, Qt::Key_Home);
		QCOMPARE(view->from(), 0);
		QCOMPARE(view->to(), 0);
		QVERIFY(a11y::canvasValue(view).contains(QStringLiteral("Frames 0 to 0")));

		QTest::keyClick(view, Qt::Key_Plus, Qt::ControlModifier);
		QVERIFY(view->viewFrames() < view->frames());
		QTest::keyClick(view, Qt::Key_0, Qt::ControlModifier);
		QCOMPARE(view->viewFrames(), view->frames());
		QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
		QCOMPARE(view->from(), 0);
		QCOMPARE(view->to(), view->frames());

		QPushButton* silence = button(m_dialog, QStringLiteral("Silence"));
		QVERIFY(silence != nullptr);
		silence->setFocus();
		QTest::keyClick(silence, Qt::Key_Space);
		QCOMPARE(m_clip->sample().data()[0][0], 0.f);
		QCOMPARE(m_clip->sample().data()[Rate / 2][0], 0.f);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY(m_clip->sample().data()[Rate / 4][0] != 0.f || m_clip->sample().data()[Rate / 4 + 1][0] != 0.f);
	}

	void anOperationActsOnTheFieldsRange()
	{
		m_dialog->setRange(4410, 8820);
		QPushButton* silence = button(m_dialog, QStringLiteral("Silence"));
		QVERIFY(silence != nullptr && silence->isEnabled());
		silence->click();
		QCOMPARE(m_clip->sample().data()[6000][0], 0.f);
		QVERIFY(m_clip->sample().data()[2000][0] != 0.f || m_clip->sample().data()[2001][0] != 0.f);
		QVERIFY2(m_dialog->status().contains(QStringLiteral("applied")), qPrintable(m_dialog->status()));
		QCOMPARE(m_dialog->view()->from(), 4410);  // the same length keeps the range for the next operation
		QCOMPARE(m_dialog->view()->to(), 8820);
		button(m_dialog, QStringLiteral("Crop"))->click();
		QCOMPARE(static_cast<int>(m_clip->sample().sampleSize()), 4410);
		QCOMPARE(m_dialog->view()->from(), 0);  // a crop leaves the range as the whole source
		QCOMPARE(m_dialog->view()->to(), 4410);
	}

	void aRefusalKeepsTheRange()
	{
		m_dialog->setRange(0, m_dialog->view()->frames());
		button(m_dialog, QStringLiteral("Delete"))->click();  // it would delete everything the clip plays
		QVERIFY2(m_dialog->status().contains(QStringLiteral("not applied")), qPrintable(m_dialog->status()));
		QCOMPARE(m_dialog->view()->from(), 0);
		QCOMPARE(m_dialog->view()->to(), Rate);
		QCOMPARE(static_cast<int>(m_clip->sample().sampleSize()), Rate);
	}

	//! Q050-106 AC2 through the Sample Editor: the button drives sample.edit_range
	//! op 'mono', the source reads mono, and one undo restores the exact stereo
	//! samples and layout.
	void theMonoButtonConvertsTheWholeSourceAndUndoRestoresIt()
	{
		std::vector<SampleFrame> stereo;
		for (int i = 0; i < Rate; ++i)
		{
			stereo.emplace_back(0.20f * std::sin(2.0 * M_PI * 220.0 * i / Rate),
				-0.10f * std::cos(2.0 * M_PI * 330.0 * i / Rate));
		}
		m_clip->setSampleBuffer(std::make_shared<SampleBuffer>(stereo.data(), stereo.size(), Rate));
		m_dialog->view()->setClip(m_clip);

		QPushButton* mono = button(m_dialog, QStringLiteral("To Mono"));
		QVERIFY(mono != nullptr);
		QVERIFY(mono->isEnabled());
		mono->click();

		QCOMPARE(m_dialog->describe().value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(m_dialog->describe().value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));
		QVERIFY2(m_dialog->status().contains(QStringLiteral("Mono")), qPrintable(m_dialog->status()));
		QCOMPARE(static_cast<int>(m_clip->sample().sampleSize()), Rate);
		for (int i = 0; i < Rate; ++i)
		{
			const float mean = (stereo[static_cast<std::size_t>(i)].left()
				+ stereo[static_cast<std::size_t>(i)].right()) / 2.0f;
			QCOMPARE(m_clip->sample().data()[i].left(), mean);
			QCOMPARE(m_clip->sample().data()[i].right(), mean);
		}

		const ControlResult undo = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(m_dialog->describe().value(QStringLiteral("layout")).toString(), QStringLiteral("stereo"));
		for (int i = 0; i < Rate; ++i)
		{
			QCOMPARE(m_clip->sample().data()[i].left(), stereo[static_cast<std::size_t>(i)].left());
			QCOMPARE(m_clip->sample().data()[i].right(), stereo[static_cast<std::size_t>(i)].right());
		}
	}

	//! Q050-106 AC1/AC3 through the Sample Editor: the split button asks for two
	//! destinations and runs sample.split_stereo; both files appear and the clip
	//! is unchanged.
	void theSplitButtonWritesTwoMonoFilesFromTheMenuPath()
	{
		const auto before = std::vector<SampleFrame>(m_clip->sample().data(),
			m_clip->sample().data() + m_clip->sample().sampleSize());
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString left = directory.filePath(QStringLiteral("left.wav"));
		const QString right = directory.filePath(QStringLiteral("right.wav"));

		QTimer probe;
		probe.setInterval(10);
		connect(&probe, &QTimer::timeout, this, [&]
		{
			auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr || dialog->objectName() != QStringLiteral("stereoSplitDialog")) { return; }
			auto* leftField = dialog->findChild<QLineEdit*>(QStringLiteral("leftPath"));
			auto* rightField = dialog->findChild<QLineEdit*>(QStringLiteral("rightPath"));
			if (leftField == nullptr || rightField == nullptr) { dialog->reject(); return; }
			leftField->setText(left);
			rightField->setText(right);
			dialog->accept();
			probe.stop();
		});
		probe.start();

		QPushButton* split = button(m_dialog, QStringLiteral("Split Stereo..."));
		QVERIFY(split != nullptr);
		QVERIFY(split->isEnabled());
		split->click();
		probe.stop();

		QVERIFY2(QFileInfo::exists(left), qPrintable(left + QStringLiteral(" status=") + m_dialog->status()));
		QVERIFY2(QFileInfo::exists(right), qPrintable(right + QStringLiteral(" status=") + m_dialog->status()));
		QVERIFY(QFileInfo(left).size() > 0);
		QVERIFY(QFileInfo(right).size() > 0);
		QVERIFY2(m_dialog->status().contains(QStringLiteral("Wrote")), qPrintable(m_dialog->status()));
		QCOMPARE(m_clip->sample().sampleSize(), before.size());
		for (std::size_t i = 0; i < before.size(); ++i)
		{
			QCOMPARE(m_clip->sample().data()[i].left(), before[i].left());
			QCOMPARE(m_clip->sample().data()[i].right(), before[i].right());
		}
	}

	void audioOpenDialogUsesSingleFileSelection()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		bool dialogFound = false;
		auto fileMode = QFileDialog::AnyFile;
		QTimer dialogProbe;
		dialogProbe.setInterval(10);
		connect(&dialogProbe, &QTimer::timeout, this, [&]
		{
			auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr) { return; }

			dialogFound = true;
			fileMode = dialog->fileMode();
			dialogProbe.stop();
			dialog->reject();
		});

		QTimer watchdog;
		watchdog.setSingleShot(true);
		connect(&watchdog, &QTimer::timeout, []
		{
			if (auto* modal = QApplication::activeModalWidget()) { modal->close(); }
		});
		dialogProbe.start();
		watchdog.start(5000);
		FileDialog::openAudioFile(directory.filePath(QStringLiteral("selected.wav")));
		QVERIFY(dialogFound);
		QCOMPARE(fileMode, QFileDialog::ExistingFile);
	}

	void graphRejectsSampleFileDropsUntilSupported()
	{
		TestGraph graph(nullptr);
		QMimeData mimeData;
		mimeData.setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),
			QByteArrayLiteral("samplefile:/tmp/dropped.wav"));

		QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mimeData,
			Qt::LeftButton, Qt::NoModifier);
		graph.deliverDrop(&drop);
		QVERIFY2(!drop.isAccepted(), "Graph accepted a sample file it does not load");

		QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mimeData,
			Qt::LeftButton, Qt::NoModifier);
		graph.deliverDragEnter(&enter);
		QVERIFY(!enter.isAccepted());
	}

private:
	SampleTrack* m_track = nullptr;
	SampleClip* m_clip = nullptr;
	SampleEditorDialog* m_dialog = nullptr;
};

QTEST_MAIN(SampleEditorDialogTest)
#include "SampleEditorDialogTest.moc"
