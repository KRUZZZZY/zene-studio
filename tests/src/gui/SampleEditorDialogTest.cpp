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

#include <QPushButton>

#include <cmath>
#include <vector>

#include "ControlRegistry.h"
#include "Editor.h"
#include "Engine.h"
#include "Graph.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleEditorDialog.h"
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

private:
	SampleTrack* m_track = nullptr;
	SampleClip* m_clip = nullptr;
	SampleEditorDialog* m_dialog = nullptr;
};

QTEST_MAIN(SampleEditorDialogTest)
#include "SampleEditorDialogTest.moc"
