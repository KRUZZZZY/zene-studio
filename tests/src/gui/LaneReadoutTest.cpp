/*
 * LaneReadoutTest.cpp - the note editor's lanes say what their values mean (0.5.0 theme 11)
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

/*! 0.5.0 theme 11: every lane's scale - its name, the values at its edges and marks, each value with its unit
 *  (velocity in %, pan as L/C/R, micro-pitch in cents, raw MIDI 0..127, pitch bend signed around its centre);
 *  the readout of the value under the pointer and of the selection; a short note's minimum click target. */

#include <QtTest>

#include "ControlRegistry.h"
#include "ControllerLaneView.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LaneScale.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteEditorParts.h"
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

class LaneReadoutTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(48), TimePos(192), 64, 80), false);
		m_clip->rearrangeAllNotes();
		m_editor = new NoteEditor;
		m_editor->resize(1200, 640);
		m_editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_editor));
		m_editor->setClip(m_clip);
		m_editor->setZoom(1.0);
		NoteGrid& g = m_editor->state().grid;
		g.setScrollTicks(0);
		g.setScrollY(g.rowOfKey(66) * g.rowHeight());
		emit m_editor->stateChanged();
	}

	void cleanup()
	{
		delete m_editor;
		m_editor = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void eachNoteLaneHasAScaleWithUnits()
	{
		const LaneScale velocity = lanescale::forNoteLane(NoteLane::Velocity);
		QCOMPARE(velocity.low, 0);
		QCOMPARE(velocity.high, 200);
		QCOMPARE(velocity.marks, (std::vector<int>{0, 100, 200}));
		QCOMPARE(velocity.text(83), QStringLiteral("83 %"));
		QCOMPARE(velocity.rangeText(), QStringLiteral("0 % to 200 %"));
		const LaneScale pan = lanescale::forNoteLane(NoteLane::Pan);
		QCOMPARE(pan.marks, (std::vector<int>{-100, 0, 100}));
		QCOMPARE(pan.text(-12), QStringLiteral("L 12"));
		QCOMPARE(pan.text(0), QStringLiteral("C"));
		QCOMPARE(pan.text(40), QStringLiteral("R 40"));
		const LaneScale pitch = lanescale::forNoteLane(NoteLane::Pitch);
		QCOMPARE(pitch.low, -200);  // the drawn span, not the whole +-4800
		QCOMPARE(pitch.high, 200);
		QCOMPARE(pitch.text(25), QStringLiteral("+25 ct"));
		QCOMPARE(lanescale::forNoteLane(NoteLane::Timbre).marks, (std::vector<int>{0, 64, 127}));
		QCOMPARE(lanescale::forNoteLane(NoteLane::Repeats).text(3), QStringLiteral("3 repeats"));
	}

	void controllerLanesKeepTheirMidiScale()
	{
		const LaneScale mod = lanescale::forController(QStringLiteral("cc:1"));
		QCOMPARE(mod.title, QStringLiteral("Mod wheel (CC 1)"));
		QCOMPARE(mod.rangeText(), QStringLiteral("0 to 127"));
		QCOMPARE(lanescale::forController(QStringLiteral("cc:20")).title, QStringLiteral("CC 20"));
		const LaneScale sustain = lanescale::forController(QStringLiteral("cc:64"));
		QCOMPARE(sustain.text(100), QStringLiteral("On (100)"));
		QCOMPARE(sustain.text(10), QStringLiteral("Off (10)"));
		const LaneScale bend = lanescale::forController(QStringLiteral("pitchbend"));
		QCOMPARE(bend.high, 16383);
		QCOMPARE(bend.marks, (std::vector<int>{0, 8192, 16383}));
		QCOMPARE(bend.text(8192), QStringLiteral("0 (centre)"));
		QCOMPARE(bend.text(16383), QStringLiteral("+8191"));
		QCOMPARE(bend.text(0), QStringLiteral("-8192"));
	}

	void theSelectionReadsAsWords()
	{
		const LaneScale velocity = lanescale::forNoteLane(NoteLane::Velocity);
		QVERIFY(lanescale::selectionText(velocity, 0, 0, 0).isEmpty());
		QCOMPARE(lanescale::selectionText(velocity, 1, 90, 90), QStringLiteral("Selected: 90 %"));
		QCOMPARE(lanescale::selectionText(velocity, 2, 80, 80), QStringLiteral("2 selected: 80 %"));
		QCOMPARE(lanescale::selectionText(velocity, 3, 64, 100), QStringLiteral("3 selected: 64 % to 100 %"));
		QVERIFY(lanescale::laneHeight() >= 96);  // the design system's, larger only in accessible mode
	}

	void theLaneReadsOutThePointerAndTheSelection()
	{
		m_editor->setLane(NoteLane::Velocity);
		NoteLaneView* lane = m_editor->laneView();
		QVERIFY(lane->readout().isEmpty());  // no pointer, no selection
		const int x0 = static_cast<int>(m_editor->state().grid.xOfTick(0));
		QTest::mouseMove(lane, QPoint(x0, lane->height() / 2));
		QVERIFY2(lane->readout().contains(QStringLiteral("100 %")), qPrintable(lane->readout()));
		QVERIFY(lane->readout().contains(QStringLiteral("pointer at")));
		const int between = static_cast<int>(m_editor->state().grid.xOfTick(96));
		QTest::mouseMove(lane, QPoint(between, lane->height() / 2));
		QVERIFY2(lane->readout().startsWith(QStringLiteral("Pointer at")), qPrintable(lane->readout()));
		QEvent leave(QEvent::Leave);
		QCoreApplication::sendEvent(lane, &leave);
		m_clip->notes()[1]->setSelected(true);
		QCOMPARE(lane->readout(), QStringLiteral("Selected: 80 %"));
		lane->grab();  // the scale, the stems and the header paint without a pointer
	}

	void theControllerLaneReadsOutItsCurve()
	{
		m_editor->setControllerLane(QStringLiteral("cc:1"));
		QCoreApplication::processEvents();
		auto* lane = m_editor->findChild<ControllerLaneView*>(QStringLiteral("noteControllerLane"));
		QVERIFY(lane != nullptr && lane->isVisible());
		const NoteGrid& g = m_editor->state().grid;
		QTest::mouseMove(lane, QPoint(static_cast<int>(g.xOfTick(24)), lane->height() / 2));
		QVERIFY2(lane->readout().contains(QStringLiteral("no curve")), qPrintable(lane->readout()));
		const QPoint from(static_cast<int>(g.xOfTick(0)) + 1, 0);
		const QPoint to(static_cast<int>(g.xOfTick(48)) + 1, 0);
		QTest::mousePress(lane, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(lane, to);
		QVERIFY2(lane->readout().startsWith(QStringLiteral("Drawing 127")), qPrintable(lane->readout()));
		QTest::mouseRelease(lane, Qt::LeftButton, Qt::NoModifier, to);
		QTest::mouseMove(lane, QPoint(static_cast<int>(g.xOfTick(24)), lane->height() - 1));
		QVERIFY2(lane->readout().contains(QStringLiteral(": 127 (pointer at 0)")), qPrintable(lane->readout()));
		lane->grab();
		m_editor->setControllerLane(QString());
	}

	void aShortNoteHasAMinimumTarget()
	{
		// A one-tick note at 1 px per tick draws three pixels wide; it is hit a few pixels past them, where no
		// other note is - and never past the minimum target.
		m_clip->addNote(Note(TimePos(1), TimePos(384), 60, 100), false);
		m_clip->rearrangeAllNotes();
		emit m_editor->stateChanged();
		NoteCanvas* canvas = m_editor->canvas();
		Note* shortNote = m_clip->notes().back();  // position-sorted: the one at tick 384
		QCOMPARE(shortNote->pos().getTicks(), 384);
		const QRect drawn = canvas->noteRect(*shortNote);
		QVERIFY(drawn.width() < 8);
		const int y = drawn.center().y();
		QCOMPARE(canvas->noteAt(QPoint(drawn.right() + 2, y)), shortNote);
		QVERIFY(canvas->noteAt(QPoint(drawn.right() + 12, y)) == nullptr);
		// A long note's own pixels stay its own.
		const QRect longNote = canvas->noteRect(*m_clip->notes()[0]);
		QCOMPARE(canvas->noteAt(longNote.center()), m_clip->notes()[0]);
	}

	void aShortProbabilisticNoteHasAVisibleCue()
	{
		m_clip->addNote(Note(TimePos(1), TimePos(384), 60, 100), false);
		m_clip->rearrangeAllNotes();
		emit m_editor->stateChanged();
		NoteCanvas* canvas = m_editor->canvas();
		Note* shortNote = m_clip->notes().back();
		const QRect drawn = canvas->noteRect(*shortNote);
		QVERIFY(drawn.width() <= 4);

		shortNote->setProbability(1.f);
		emit m_editor->stateChanged();
		QCoreApplication::processEvents();
		const QColor alwaysPlayed = canvas->grab().toImage().pixelColor(drawn.center());
		shortNote->setProbability(0.5f);
		emit m_editor->stateChanged();
		QCoreApplication::processEvents();
		const QColor mayNotPlay = canvas->grab().toImage().pixelColor(drawn.center());
		QVERIFY2(alwaysPlayed != mayNotPlay, "a narrow note's probability must change its rendered fill");

		shortNote->setSelected(true);
		shortNote->setProbability(1.f);
		emit m_editor->stateChanged();
		QCoreApplication::processEvents();
		const QColor selectedAlwaysPlayed = canvas->grab().toImage().pixelColor(drawn.center());
		shortNote->setProbability(0.5f);
		emit m_editor->stateChanged();
		QCoreApplication::processEvents();
		const QColor selectedMayNotPlay = canvas->grab().toImage().pixelColor(drawn.center());
		QVERIFY2(selectedAlwaysPlayed != selectedMayNotPlay, "selection must not hide a narrow note's probability");
	}

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	NoteEditor* m_editor = nullptr;
};

QTEST_MAIN(LaneReadoutTest)
#include "LaneReadoutTest.moc"
