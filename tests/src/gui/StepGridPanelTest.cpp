/*
 * StepGridPanelTest.cpp - the melodic step grid (Q050-294.2): a MIDI clip opened in the grid and
 *                         edited by clicks and fields, every gesture one note.step_set and one undo
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
/*! The grid opens a clip and shows its ordinary notes; a click on an empty cell adds a note snapped
 *  into the scale, a right click removes one, and the inspector's pitch (stepping in the scale),
 *  velocity, chance and ratchet fields change the selected note - each one undo step. An agent's
 *  note.step_set shows in the grid at once, the piano roll and roll.get_state read the same notes,
 *  and the note editor's Notes > Tools > Open in Step Grid runs window.step_grid. */

#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QPushButton>
#include <QToolButton>
#include <algorithm>
#include <tuple>
#include <vector>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteConditions.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "StepGridPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool undo() { return run(QStringLiteral("control.undo")).ok; }

//! position, key, length, velocity, chance, repeats, id - the whole of what the grid edits.
using NoteRow = std::tuple<int, int, int, int, float, int, QString>;

std::vector<NoteRow> rowsOf(const MidiClip* clip)
{
	std::vector<NoteRow> out;
	for (const Note* note : clip->notes())
	{
		out.emplace_back(note->pos().getTicks(), note->key(), note->length().getTicks(), static_cast<int>(note->getVolume()),
			note->probability(), note->conditions().repeats, control::noteIdOf(note));
	}
	std::sort(out.begin(), out.end());
	return out;
}

Note* noteAt(const MidiClip* clip, int position, int key)
{
	for (Note* note : clip->notes())
	{
		if (note->pos().getTicks() == position && note->key() == key) { return note; }
	}
	return nullptr;
}

//! Type @a value into a spin box and commit it with Return, as a person does.
void typeInto(QSpinBox* box, int value)
{
	box->setFocus();
	QTest::keyClick(box, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClicks(box, QString::number(value));
	QTest::keyClick(box, Qt::Key_Return);
}

StepGridPanel* findOpenGrid()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* grid = dynamic_cast<StepGridPanel*>(widget); grid != nullptr && grid->isVisible()) { return grid; }
	}
	return nullptr;
}

} // namespace

class StepGridPanelTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	StepGridPanel* m_panel = nullptr;

	template<class T> T* child(const char* name) const { return m_panel->findChild<T*>(QLatin1String(name)); }

	void clickCell(int step, int key, Qt::MouseButton button = Qt::LeftButton)
	{
		const QRect cell = m_panel->canvas()->cellRect(step, key);
		QVERIFY2(!cell.isEmpty(), "the cell is a row of the grid");
		QTest::mouseClick(m_panel->canvas(), button, Qt::NoModifier, cell.center());
	}

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
		// C5 E5 G5 and an out-of-scale C#5 on the first four sixteenths, in C major.
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		const int keys[] = {60, 64, 67, 61};
		for (int i = 0; i < 4; ++i) { m_clip->addNote(Note(TimePos(12), TimePos(i * 12), keys[i], 100), false); }
		m_clip->rearrangeAllNotes();
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), QStringLiteral("C")}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		m_panel = new StepGridPanel;
		m_panel->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_panel));
		QCOMPARE(m_panel->setClip(control::clipIdOf(m_clip)), QString());
	}

	void cleanup()
	{
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok);
		delete m_panel;
		m_panel = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	//! Opening a clip shows each of its notes in its (step, key) cell, out-of-scale ones too.
	void theGridShowsTheClipsNotes()
	{
		QCOMPARE(m_panel->state().value(QStringLiteral("count")).toInt(), 4);
		for (const Note* note : m_clip->notes())
		{
			const QJsonObject cell = m_panel->noteAt(note->pos().getTicks() / 12, note->key());
			QCOMPARE(cell.value(QStringLiteral("note")).toString(), control::noteIdOf(note));
			QVERIFY(!m_panel->canvas()->cellRect(note->pos().getTicks() / 12, note->key()).isEmpty());
		}
		QVERIFY(!m_panel->inScale(61) && m_panel->inScale(64));
		// Scale rows only hides the empty out-of-scale rows but never a row that holds a note.
		child<QCheckBox>("stepGridScaleRows")->click();
		QVERIFY(!m_panel->rows().contains(63));
		QVERIFY(m_panel->rows().contains(61) && m_panel->rows().contains(62));
		child<QCheckBox>("stepGridScaleRows")->click();
		QVERIFY(m_panel->rows().contains(63));
	}

	//! A click on an empty cell adds one note there - its pitch snapped into the scale - and one undo takes it out.
	void clickingAnEmptyCellAddsANoteInTheScale()
	{
		const std::vector<NoteRow> before = rowsOf(m_clip);
		clickCell(5, 66);  // F#: out of C major, snaps down to F (a tie goes down)
		QCOMPARE(static_cast<int>(m_clip->notes().size()), 5);
		Note* added = noteAt(m_clip, 60, 65);
		QVERIFY2(added != nullptr, "the new note is on step 5, snapped to F5");
		QCOMPARE(m_panel->selectedNote(), control::noteIdOf(added));
		QCOMPARE(static_cast<int>(added->length().getTicks()), 12);
		QVERIFY(undo());
		QVERIFY(rowsOf(m_clip) == before);
		QVERIFY(m_panel->noteAt(5, 65).isEmpty());
	}

	//! The inspector's fields change the selected note's velocity, chance and ratchet, one undo step each.
	void theInspectorEditsVelocityChanceAndRatchet()
	{
		const std::vector<NoteRow> before = rowsOf(m_clip);
		clickCell(0, 60);  // an occupied cell selects, it adds nothing
		QCOMPARE(static_cast<int>(m_clip->notes().size()), 4);
		typeInto(child<QSpinBox>("stepGridVelocity"), 150);
		typeInto(child<QSpinBox>("stepGridChance"), 25);
		typeInto(child<QSpinBox>("stepGridRatchet"), 4);
		const Note* note = noteAt(m_clip, 0, 60);
		QVERIFY(note != nullptr);
		QCOMPARE(static_cast<int>(note->getVolume()), 150);
		QCOMPARE(note->probability(), 0.25f);
		QCOMPARE(note->conditions().repeats, 3);  // ratchet 4 = four attacks
		QCOMPARE(child<QSpinBox>("stepGridRatchet")->value(), 4);
		QVERIFY(undo());
		QCOMPARE(noteAt(m_clip, 0, 60)->conditions().repeats, 0);
		QCOMPARE(noteAt(m_clip, 0, 60)->probability(), 0.25f);  // one undo, one edit
		QVERIFY(undo());
		QVERIFY(undo());
		QVERIFY(rowsOf(m_clip) == before);
		QCOMPARE(child<QSpinBox>("stepGridVelocity")->value(), 100);
	}

	//! The pitch field steps through the scale's keys; an out-of-scale note keeps its key while edited.
	void thePitchFieldStepsThroughTheScale()
	{
		clickCell(1, 64);
		QSpinBox* pitch = child<QSpinBox>("stepGridKey");
		pitch->setFocus();
		QTest::keyClick(pitch, Qt::Key_Up);
		QVERIFY2(noteAt(m_clip, 12, 65) != nullptr, "E5 up one degree of C major is F5");
		QTest::keyClick(pitch, Qt::Key_Up);
		QVERIFY2(noteAt(m_clip, 12, 67) != nullptr, "F5 up one degree is G5, not F#5");
		QCOMPARE(m_panel->noteAt(1, 67).value(QStringLiteral("note")).toString(), m_panel->selectedNote());
		QVERIFY(undo());
		QVERIFY(noteAt(m_clip, 12, 65) != nullptr);
		clickCell(3, 61);
		typeInto(child<QSpinBox>("stepGridVelocity"), 70);
		QVERIFY2(noteAt(m_clip, 36, 61) != nullptr && noteAt(m_clip, 36, 61)->getVolume() == 70,
			"the out-of-scale C#5 stays on its key and takes the velocity");
	}

	//! A right click removes the cell's note; one undo puts it back with its id.
	void aRightClickRemovesAndUndoRestores()
	{
		const std::vector<NoteRow> before = rowsOf(m_clip);
		clickCell(2, 67, Qt::RightButton);
		QVERIFY(noteAt(m_clip, 24, 67) == nullptr);
		QVERIFY(m_panel->noteAt(2, 67).isEmpty());
		QVERIFY(undo());
		QVERIFY(rowsOf(m_clip) == before);
		QVERIFY(!m_panel->noteAt(2, 67).isEmpty());
	}

	//! The resolution box regroups the same notes: at 1/8 two sixteenths share a step.
	void theResolutionRegroupsTheSameNotes()
	{
		auto* resolution = child<QComboBox>("stepGridResolution");
		resolution->setFocus();
		QTest::keyClick(resolution, Qt::Key_Down);  // 1/16 -> 1/8
		QCOMPARE(m_panel->stepTicks(), 24);
		QCOMPARE(m_panel->noteAt(0, 64).value(QStringLiteral("step")).toInt(), 0);
		QCOMPARE(m_panel->noteAt(1, 61).value(QStringLiteral("step")).toInt(), 1);
		clickCell(2, 72);
		QVERIFY2(noteAt(m_clip, 48, 72) != nullptr && noteAt(m_clip, 48, 72)->length().getTicks() == 24,
			"a new cell at 1/8 starts on step 2's tick and lasts one step");
	}

	//! An agent's note.step_set shows in the grid at once; the piano roll and roll.get_state read the same notes.
	void anAgentEditAndThePianoRollShowTheSameClip()
	{
		const QString clip = control::clipIdOf(m_clip);
		QJsonArray edits{QJsonObject{{QStringLiteral("step"), 4}, {QStringLiteral("key"), 62}, {QStringLiteral("chance"), 0.5}}};
		QVERIFY(run(QStringLiteral("note.step_set"), {{QStringLiteral("clip"), clip}, {QStringLiteral("edits"), edits}}).ok);
		QCOMPARE(m_panel->noteAt(4, 62).value(QStringLiteral("chance")).toDouble(), 0.5);
		const QJsonArray roll = run(QStringLiteral("roll.get_state"), {{QStringLiteral("clip"), clip}}).result.value(QStringLiteral("notes")).toArray();
		const QJsonArray cells = m_panel->describe().value(QStringLiteral("cells")).toArray();
		QCOMPARE(roll.size(), cells.size());
		for (const QJsonValue& value : roll)
		{
			const QJsonObject note = value.toObject();
			const QJsonObject cell = m_panel->noteAt(note.value(QStringLiteral("position")).toInt() / 12, note.value(QStringLiteral("key")).toInt());
			QCOMPARE(cell.value(QStringLiteral("note")).toString(), note.value(QStringLiteral("id")).toString());
			QCOMPARE(cell.value(QStringLiteral("velocity")).toInt(), note.value(QStringLiteral("velocity")).toInt());
		}
		NoteEditor editor;
		editor.resize(900, 500);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		editor.setClip(m_clip);
		QVERIFY(editor.canvas()->noteRect(*noteAt(m_clip, 48, 62)).isValid());
		QVERIFY(undo());
		QVERIFY(m_panel->noteAt(4, 62).isEmpty());
	}

	//! The note editor's Notes > Tools > Open in Step Grid runs window.step_grid on that clip.
	void theNoteEditorMenuOpensTheGrid()
	{
		NoteEditor editor;
		editor.resize(900, 500);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		editor.setClip(m_clip);
		auto* button = editor.findChild<QToolButton*>(QStringLiteral("noteEditorNotesMenu"));
		QVERIFY(button != nullptr && button->menu() != nullptr);
		emit button->menu()->aboutToShow();
		QAction* action = test::menuAction(button->menu(), {QStringLiteral("Tools"), QStringLiteral("Open in Step Grid")});
		QVERIFY(action != nullptr);
		QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("window.step_grid"));
		m_panel->hide();
		action->trigger();
		StepGridPanel* grid = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((grid = findOpenGrid()) != nullptr, 3000);
		QVERIFY(grid != m_panel);
		QCOMPARE(grid->clipId(), control::clipIdOf(m_clip));
		QCOMPARE(grid->state().value(QStringLiteral("count")).toInt(), 4);
		grid->close();  // WA_DeleteOnClose
	}
};

QTEST_MAIN(StepGridPanelTest)
#include "StepGridPanelTest.moc"
