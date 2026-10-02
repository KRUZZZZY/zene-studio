/*
 * NoteEditorTest.cpp - the reworked note editor's tools, each one edit and one undo step
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

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteEditorParts.h"
#include "NotePlayHandle.h"
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

class NoteEditorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The editor auditions through the instrument, which plays a note: the pool main()
		// creates must exist in a test binary too (onboarding trap, CLAUDE-READ-FIRST §4).
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
		// Two notes a bar apart, on C5 and E5.
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(48), TimePos(192), 64, 100), false);
		m_clip->rearrangeAllNotes();
		m_editor = new NoteEditor;
		m_editor->resize(1200, 640);
		m_editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_editor));
		m_editor->setClip(m_clip);
		m_editor->setZoom(1.0);  // 1 px per tick: positions are easy to aim at
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

	void aClickWithTheDrawToolAddsOneSnappedNote()
	{
		m_editor->setTool(NoteTool::Draw);
		const QPoint at = point(100, 62);
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, at);
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		QVERIFY2(noteAt(m_clip, 96, 62) != nullptr, "the note starts on the 1/16 grid cell clicked");
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
	}

	void draggingTheSelectionIsOneEditAndOneUndo()
	{
		select({m_clip->notes()[0], m_clip->notes()[1]});
		m_editor->setTool(NoteTool::Select);
		const QPoint from = m_editor->canvas()->noteRect(*m_clip->notes()[0]).center();
		const int rowH = m_editor->state().grid.rowHeight();
		drag(from, from + QPoint(24, rowH));
		QVERIFY2(noteAt(m_clip, 24, 59) != nullptr && noteAt(m_clip, 216, 63) != nullptr,
			"both selected notes move 1/8 later and one key down");
		QVERIFY(undo());
		QVERIFY(noteAt(m_clip, 0, 60) != nullptr && noteAt(m_clip, 192, 64) != nullptr);
	}

	void theRightEdgeResizes()
	{
		select({m_clip->notes()[0]});
		const QRect r = m_editor->canvas()->noteRect(*m_clip->notes()[0]);
		drag(QPoint(r.right() - 1, r.center().y()), QPoint(r.right() + 47, r.center().y()));
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->length().getTicks()), 96);
		QCOMPARE(m_editor->state().lastLength, 96);
	}

	void theSliceToolSplitsANoteOnTheGrid()
	{
		m_editor->setTool(NoteTool::Slice);
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(24, 60));
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->length().getTicks()), 24);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 24, 60)->length().getTicks()), 24);
	}

	void theEraserRemovesWhatItCrosses()
	{
		m_editor->setTool(NoteTool::Erase);
		drag(point(10, 60), point(200, 64));
		QCOMPARE(m_clip->notes().size(), std::size_t{0});
	}

	void aRubberBandSelects()
	{
		m_editor->setTool(NoteTool::Select);
		drag(point(150, 70), point(300, 50));
		QVERIFY(!m_clip->notes()[0]->selected());
		QVERIFY(m_clip->notes()[1]->selected());
		const QJsonObject roll = invoke(QStringLiteral("roll.get_state"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}}).result;
		QCOMPARE(roll.value(QStringLiteral("selected_notes")).toArray().size(), 1);
	}

	void foldingChangesTheRowsNotTheNotes()
	{
		m_editor->setFold(NoteFold::Used);
		QCOMPARE(m_editor->state().grid.rowCount(), 2);
		QCOMPARE(m_editor->describe().value(QStringLiteral("fold")).toString(), QStringLiteral("used"));
		m_editor->setFold(NoteFold::None);
		QCOMPARE(m_editor->state().grid.rowCount(), 128);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
	}

	void theVelocityLaneDrawsAndIsOneUndo()
	{
		m_editor->setLane(NoteLane::Velocity);
		NoteLaneView* lane = m_editor->laneView();
		const int x0 = static_cast<int>(m_editor->state().grid.xOfTick(0));
		const int x1 = static_cast<int>(m_editor->state().grid.xOfTick(192));
		QTest::mousePress(lane, Qt::LeftButton, Qt::NoModifier, QPoint(x0, 2));
		QTest::mouseMove(lane, QPoint(x1, 2));
		QTest::mouseRelease(lane, Qt::LeftButton, Qt::NoModifier, QPoint(x1, 2));
		QVERIFY(m_clip->notes()[0]->getVolume() > 190);
		QVERIFY(m_clip->notes()[1]->getVolume() > 190);
		QVERIFY(undo());
		QCOMPARE(static_cast<int>(m_clip->notes()[0]->getVolume()), 100);
		QCOMPARE(static_cast<int>(m_clip->notes()[1]->getVolume()), 100);
	}

	void anAgentsEditShowsAtOnce()
	{
		const auto [first, last] = m_editor->canvas()->visibleRange();
		QVERIFY(invoke(QStringLiteral("note.add"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("key"), 65}, {QStringLiteral("position"), 96}, {QStringLiteral("length"), 24}}).ok);
		const auto [first2, last2] = m_editor->canvas()->visibleRange();
		QCOMPARE(last2 - first2, last - first + 1);
	}

	void theNumberKeysPickTheToolsAndDeleteRemovesTheSelection()
	{
		m_editor->canvas()->setFocus();
		QTest::keyClick(m_editor, Qt::Key_4);
		QCOMPARE(m_editor->state().tool, NoteTool::Erase);
		QTest::keyClick(m_editor, Qt::Key_2);
		QCOMPARE(m_editor->describe().value(QStringLiteral("tool")).toString(), QStringLiteral("select"));
		select({m_clip->notes()[1]});
		QTest::keyClick(m_editor->canvas(), Qt::Key_Delete);
		QCOMPARE(m_clip->notes().size(), std::size_t{1});
	}

	void noteEditIsValidatedWholeBeforeAnythingIsWritten()
	{
		const QString clip = control::clipIdOf(m_clip);
		const QString first = control::noteIdOf(m_clip->notes()[0]);
		const QString second = control::noteIdOf(m_clip->notes()[1]);
		const ControlResult refused = invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("note"), first}, {QStringLiteral("key"), 70}},
				QJsonObject{{QStringLiteral("note"), second}, {QStringLiteral("pan"), 500}}}}});
		QVERIFY(!refused.ok);
		QCOMPARE(m_clip->notes()[0]->key(), 60);  // the valid first entry was not applied either
		const ControlResult empty = invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("note"), first}}}}});
		QVERIFY2(!empty.ok, "an entry that names no field is refused, not a silent no-op");
		const ControlResult unknown = invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("note"), QStringLiteral("note-999999")}, {QStringLiteral("key"), 1}}}}});
		QVERIFY(!unknown.ok);
	}

	void noteEditKeepsIdsAndUndoesAsOne()
	{
		const QString clip = control::clipIdOf(m_clip);
		Note* a = m_clip->notes()[0];
		Note* b = m_clip->notes()[1];
		const QString ida = control::noteIdOf(a);
		const QString idb = control::noteIdOf(b);
		// Swap their order in time: the re-sort must not change either id.
		const ControlResult done = invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("edits"), QJsonArray{
				QJsonObject{{QStringLiteral("note"), ida}, {QStringLiteral("position"), 300}, {QStringLiteral("velocity"), 30}},
				QJsonObject{{QStringLiteral("note"), idb}, {QStringLiteral("position"), 10}, {QStringLiteral("probability"), 0.5}}}}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("edited")).toInt(), 2);
		QCOMPARE(control::noteIdOf(m_clip->notes()[0]), idb);
		QCOMPARE(control::noteIdOf(m_clip->notes()[1]), ida);
		QVERIFY(undo());
		QVERIFY(noteAt(m_clip, 0, 60) != nullptr && noteAt(m_clip, 192, 64) != nullptr);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 100);
		QCOMPARE(noteAt(m_clip, 192, 64)->probability(), 1.f);
	}

	void aDeletedClipLeavesAnEmptyEditorThatStillPaints()
	{
		delete m_clip;
		m_clip = nullptr;
		QVERIFY(m_editor->clip() == nullptr);
		m_editor->grab();  // paints every part over the absent clip
		QCOMPARE(m_editor->describe().value(QStringLiteral("clip")).toString(), QString());
	}

private:
	QPoint point(int tick, int key) const
	{
		const NoteGrid& g = m_editor->state().grid;
		return QPoint(static_cast<int>(g.xOfTick(tick)) + 2, g.yOfKey(key) + g.rowHeight() / 2);
	}

	void drag(const QPoint& from, const QPoint& to)
	{
		QWidget* canvas = m_editor->canvas();
		QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(canvas, (from + to) / 2);
		QTest::mouseMove(canvas, to);
		QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
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

QTEST_MAIN(NoteEditorTest)
#include "NoteEditorTest.moc"
