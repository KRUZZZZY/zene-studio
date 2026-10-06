/*
 * NoteEditorFlTest.cpp - the note editor follows FL Studio's piano roll (owner decision 2026-10-03)
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

/*! The FL model as research/reviews/bughunt-2026-10-03/ux-research/FL-PIANO-ROLL-SPEC.md sets it out, each
 *  gesture asserted on the notes it leaves and on its undo: the tool letters, the right button (a drag
 *  deletes what it crosses in one step; from an empty row it slices), Ctrl selecting, the Shift / Ctrl locks,
 *  the left-edge resize, Alt bypassing the snap, Alt+wheel, the last note's length, the double-click
 *  properties box and the clipboard keys. */

#include <QtTest>

#include <QCheckBox>
#include <QJsonArray>
#include <QSpinBox>
#include <QWheelEvent>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "NoteProperties.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool undo() { return invoke(QStringLiteral("control.undo")).ok; }

Note* noteAt(MidiClip* clip, int position, int key)
{
	for (Note* note : clip->notes())
	{
		if (note->pos().getTicks() == position && note->key() == key) { return note; }
	}
	return nullptr;
}

} // namespace

class NoteEditorFlTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		NotePlayHandleManager::init();  // the editor auditions through the instrument
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
		// A: C5 0..48, B: E5 0..48 (a chord), C: D5 192..288.
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(48), TimePos(0), 64, 100), false);
		m_clip->addNote(Note(TimePos(96), TimePos(192), 62, 100), false);
		m_clip->rearrangeAllNotes();
		m_editor = new NoteEditor;
		m_editor->resize(1200, 640);
		m_editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_editor));
		m_editor->setClip(m_clip);
		m_editor->setZoom(1.0);  // 1 px per tick
		NoteGrid& g = m_editor->state().grid;
		g.setScrollTicks(0);
		g.setScrollY(g.rowOfKey(68) * g.rowHeight());
		emit m_editor->stateChanged();
	}

	void cleanup()
	{
		delete m_editor;
		m_editor = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void drawIsTheDefaultAndTheLettersPickFlsTools()
	{
		QCOMPARE(m_editor->state().tool, NoteTool::Draw);
		const std::pair<Qt::Key, NoteTool> letters[] = {{Qt::Key_B, NoteTool::Paint}, {Qt::Key_D, NoteTool::Erase},
			{Qt::Key_T, NoteTool::Mute}, {Qt::Key_C, NoteTool::Slice}, {Qt::Key_E, NoteTool::Select},
			{Qt::Key_Y, NoteTool::Audition}, {Qt::Key_P, NoteTool::Draw}};
		for (const auto& [key, tool] : letters)
		{
			QTest::keyClick(m_editor, key);
			QCOMPARE(m_editor->state().tool, tool);
		}
		QTest::keyClick(m_editor, Qt::Key_2);  // 1..7 stay as aliases
		QCOMPARE(m_editor->state().tool, NoteTool::Select);
		// The note area claims the letters and the clipboard keys over a window shortcut.
		QVERIFY(claimed(Qt::Key_D, Qt::NoModifier));
		QVERIFY(claimed(Qt::Key_C, Qt::ControlModifier));
		QVERIFY(claimed(Qt::Key_B, Qt::ControlModifier));
		QVERIFY2(!claimed(Qt::Key_Q, Qt::NoModifier), "a key the editor does not use stays the window's");
	}

	void zZoomsToTheSelection()
	{
		select({noteAt(m_clip, 192, 62)});
		QTest::keyClick(m_editor, Qt::Key_Z);
		const NoteGrid& g = m_editor->state().grid;
		QVERIFY2(g.firstVisibleTick() <= 192 && g.lastVisibleTick() >= 288, "the selected note is in view");
		QVERIFY2(g.pixelsPerTick() > 1.0, "and fills it: the view zoomed in on one note");
	}

	void aRightDragDeletesEveryNoteItCrossesInOneUndo()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, point(10, 64));
		QTest::mouseMove(canvas, point(10, 60));
		QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, point(10, 60));
		QCOMPARE(m_clip->notes().size(), std::size_t{1});
		QVERIFY(noteAt(m_clip, 192, 62) != nullptr);
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});  // both come back with ONE undo (note.remove_many)
	}

	void aRightDragFromAnEmptyRowSlicesWhatTheLineCrosses()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, point(24, 66));
		QTest::mouseMove(canvas, point(24, 62));
		QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, point(24, 59));
		QCOMPARE(m_clip->notes().size(), std::size_t{5});
		for (const int key : {60, 64})
		{
			QCOMPARE(static_cast<int>(noteAt(m_clip, 0, key)->length().getTicks()), 24);
			QCOMPARE(static_cast<int>(noteAt(m_clip, 24, key)->length().getTicks()), 24);
		}
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
	}

	void ctrlSelectsAndCtrlShiftAddsWithoutDrawing()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::ControlModifier, point(10, 60));
		QVERIFY(noteAt(m_clip, 0, 60)->selected() && !noteAt(m_clip, 0, 64)->selected());
		QTest::mouseClick(canvas, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, point(10, 64));
		QVERIFY(noteAt(m_clip, 0, 60)->selected() && noteAt(m_clip, 0, 64)->selected());
		drag(point(150, 66), point(260, 58), Qt::ControlModifier);  // a rectangle over C only
		QVERIFY(noteAt(m_clip, 192, 62)->selected() && !noteAt(m_clip, 0, 60)->selected());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});  // Ctrl never drew
	}

	void shiftLocksThePitchAndCtrlTheTimingWhileDragging()
	{
		const int rowH = m_editor->state().grid.rowHeight();
		QWidget* canvas = m_editor->canvas();
		QPoint from = point(200, 62);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(canvas, from + QPoint(48, 2 * rowH));
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::ShiftModifier, from + QPoint(48, 2 * rowH));
		QVERIFY2(noteAt(m_clip, 240, 62) != nullptr, "Shift: moved in time, not in pitch");
		from = point(250, 62);
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(canvas, from + QPoint(48, rowH));
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::ControlModifier, from + QPoint(48, rowH));
		QVERIFY2(noteAt(m_clip, 240, 61) != nullptr, "Ctrl: moved in pitch, not in time");
	}

	void shiftDragOnTheLeftEdgeResizesFromTheStart()
	{
		select({noteAt(m_clip, 192, 62)});
		const QRect r = m_editor->canvas()->noteRect(*noteAt(m_clip, 192, 62));
		const QPoint edge(r.left() + 1, r.center().y());
		drag(edge, edge + QPoint(24, 0), Qt::ShiftModifier);
		const Note* note = noteAt(m_clip, 216, 62);
		QVERIFY2(note != nullptr, "the start moved");
		QCOMPARE(static_cast<int>(note->endPos().getTicks()), 288);  // the end stayed
		QCOMPARE(m_editor->state().lastLength, 72);
	}

	void altBypassesTheSnap()
	{
		drag(point(200, 62), point(205, 62));
		QVERIFY2(noteAt(m_clip, 192, 62) != nullptr, "5 ticks snaps back to the 1/16 line");
		drag(point(200, 62), point(205, 62), Qt::AltModifier);
		QVERIFY2(noteAt(m_clip, 197, 62) != nullptr, "with Alt the move is not snapped");
	}

	void altWheelSetsVelocityFinelyWithCtrlAndShiftNudges()
	{
		wheel(point(200, 62), Qt::AltModifier | Qt::ControlModifier, 120);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 101);
		wheel(point(200, 62), Qt::AltModifier, -120);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 96);
		wheel(point(200, 62), Qt::AltModifier | Qt::ShiftModifier, 120);
		QVERIFY2(noteAt(m_clip, 204, 62) != nullptr, "Shift+Alt nudges a grid step later");
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 100);  // an unselected note elsewhere is left alone
	}

	void aPaintDragDownAColumnPaintsEveryRowItCrosses()
	{
		QTest::keyClick(m_editor, Qt::Key_B);  // the Paint tool
		QCOMPARE(m_editor->state().tool, NoteTool::Paint);
		const QPoint from = point(400, 66);
		const QPoint to = point(400, 70);  // straight down the same grid column
		QWidget* canvas = m_editor->canvas();
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(canvas, to);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
		// A painted cell is a (key, start) pair: two rows in one column are two different cells,
		// and the drag must not lose the second one to the first (a start-only duplicate check).
		QVERIFY2(noteAt(m_clip, 396, 66) != nullptr, "the row the drag started on");
		QVERIFY2(noteAt(m_clip, 396, 70) != nullptr, "and the row it crossed into");
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
	}

	void aNewNoteTakesTheLastTouchedNotesLength()
	{
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(200, 62));  // touch C (96 long)
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(400, 66));  // the right half of a cell
		QCOMPARE(static_cast<int>(noteAt(m_clip, 396, 66)->length().getTicks()), 96);
		m_editor->setNewNote(24, 100);  // New Note Length (the grid box's right-click) wins over the last note's
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(600, 66));
		QCOMPARE(static_cast<int>(noteAt(m_clip, 600, 66)->length().getTicks()), 24);
	}

	void aDoubleClickOpensTheNotesPropertiesAndOkIsOneEdit()
	{
		QTest::mouseDClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(200, 62));
		auto* box = m_editor->findChild<NotePropertiesDialog*>(QStringLiteral("noteProperties"));
		QVERIFY(box != nullptr);
		box->findChild<QSpinBox*>(QStringLiteral("velocity"))->setValue(150);
		box->findChild<QSpinBox*>(QStringLiteral("timbre"))->setValue(90);
		box->findChild<QCheckBox*>(QStringLiteral("slide"))->setChecked(true);
		box->accept();
		const Note* note = noteAt(m_clip, 192, 62);
		QCOMPARE(static_cast<int>(note->getVolume()), 150);
		QCOMPARE(note->mpeTimbre(), 90);
		QVERIFY(note->slide());
		QVERIFY(undo());  // ONE note.edit
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 100);
		QVERIFY(!noteAt(m_clip, 192, 62)->slide());
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		// A double-click on an empty row draws one note and opens nothing. QTest::mouseDClick on a widget sends only the
		// double-click event, and a QTest release moves its clock on by a whole double-click interval, so the press
		// that draws is sent first and the release last - the timestamps stay within one double-click.
		QTest::mousePress(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(400, 66));
		QTest::mouseDClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(400, 66));
		QTest::mouseRelease(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(400, 66));
		QVERIFY(noteAt(m_clip, 396, 66) != nullptr);
		QVERIFY(m_editor->findChild<NotePropertiesDialog*>(QStringLiteral("noteProperties")) == nullptr);
	}

	void theClipboardKeysCopyPasteDuplicateAndDelete()
	{
		select({noteAt(m_clip, 192, 62)});
		QWidget* canvas = m_editor->canvas();
		QTest::keyClick(canvas, Qt::Key_B, Qt::ControlModifier);
		QVERIFY2(noteAt(m_clip, 288, 62) != nullptr, "Ctrl+B duplicates just after the selection");
		QVERIFY(noteAt(m_clip, 288, 62)->selected() && !noteAt(m_clip, 192, 62)->selected());
		QVERIFY(undo());
		select({noteAt(m_clip, 0, 60)});
		QTest::keySequence(canvas, QKeySequence::Copy);
		QTest::keySequence(canvas, QKeySequence::Paste);
		QCOMPARE(m_clip->notes().size(), std::size_t{4});  // pasted where it was copied from
		QTest::keyClick(canvas, Qt::Key_Delete);  // the pasted note is the selection
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		QTest::keySequence(canvas, QKeySequence::Cut);
		QVERIFY2(m_clip->notes().empty(), "nothing selected: Ctrl+X cuts the whole clip");
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
	}

private:
	QPoint point(int tick, int key) const
	{
		const NoteGrid& g = m_editor->state().grid;
		return QPoint(static_cast<int>(g.xOfTick(tick)) + 2, g.yOfKey(key) + g.rowHeight() / 2);
	}

	void drag(const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods = Qt::NoModifier)
	{
		QWidget* canvas = m_editor->canvas();
		QTest::mousePress(canvas, Qt::LeftButton, mods, from);
		QTest::mouseMove(canvas, (from + to) / 2);
		QTest::mouseMove(canvas, to);
		QTest::mouseRelease(canvas, Qt::LeftButton, mods, to);
	}

	//! Whether the note area claims @a key over a window shortcut while it has the focus.
	bool claimed(Qt::Key key, Qt::KeyboardModifiers mods) const
	{
		QKeyEvent event(QEvent::ShortcutOverride, key, mods);
		event.ignore();
		QApplication::sendEvent(m_editor->canvas(), &event);
		return event.isAccepted();
	}

	void wheel(const QPoint& at, Qt::KeyboardModifiers mods, int amount)
	{
		QWheelEvent event(at, m_editor->canvas()->mapToGlobal(at), QPoint(), QPoint(0, amount), Qt::NoButton, mods,
			Qt::NoScrollPhase, false);
		QApplication::sendEvent(m_editor->canvas(), &event);
	}

	void select(const std::vector<Note*>& notes)
	{
		QJsonArray ids;
		for (const Note* note : notes) { ids.append(control::noteIdOf(note)); }
		QVERIFY(invoke(QStringLiteral("note.select"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("notes"), ids}}).ok);
	}

	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	NoteEditor* m_editor = nullptr;
};

QTEST_MAIN(NoteEditorFlTest)
#include "NoteEditorFlTest.moc"
