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
#include <QToolButton>

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
#include "MenuTestSupport.h"
#include "Song.h"
#include "TrackKeyNames.h"

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

	// --- 0.5.0 L07 ------------------------------------------------------------------------

	void thePaintToolLaysANoteOnEveryCellItCrossesAsOneEdit()
	{
		m_editor->setTool(NoteTool::Paint);
		// x = tick + 2 at 1 px per tick: the press, the midpoint and the release land in the 1/16 cells
		// starting at 300, 324 and 348.
		drag(point(300, 62), point(347, 62));
		QCOMPARE(m_clip->notes().size(), std::size_t{5});
		for (int at : {300, 324, 348})
		{
			const Note* note = noteAt(m_clip, at, 62);
			QVERIFY2(note != nullptr, qPrintable(QStringLiteral("no painted note at %1").arg(at)));
			QCOMPARE(static_cast<int>(note->length().getTicks()), 12);
		}
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
	}

	void altDragChangesTheSelectionsVelocityAsOneEdit()
	{
		select({m_clip->notes()[0], m_clip->notes()[1]});
		m_editor->setTool(NoteTool::Select);
		const QPoint from = m_editor->canvas()->noteRect(*m_clip->notes()[0]).center();
		drag(from, from - QPoint(0, 30), Qt::AltModifier);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 130);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 64)->getVolume()), 130);
		QVERIFY2(noteAt(m_clip, 0, 60) != nullptr, "a velocity drag does not move the note");
		QCOMPARE(m_editor->state().lastVelocity, 130);
		QVERIFY(undo());
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 100);
	}

	void keepInScaleDrawsOnTheNearestScaleKey()
	{
		QVERIFY(invoke(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), 0}}).ok);
		QVERIFY(invoke(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		m_editor->setClip(m_clip);  // reads the scale
		m_editor->setTool(NoteTool::Draw);
		m_editor->setKeepInScale(true);
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(100, 61));
		QVERIFY2(noteAt(m_clip, 96, 60) != nullptr && noteAt(m_clip, 96, 61) == nullptr, "C# is drawn as C in C major");
		m_editor->setKeepInScale(false);
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(148, 61));
		QVERIFY(noteAt(m_clip, 144, 61) != nullptr);
		QVERIFY(invoke(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok);
	}

	void theDrumViewShowsTheNamedKeysByName()
	{
		const QString track = control::trackIdOf(m_track);
		QVERIFY(invoke(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), track}, {QStringLiteral("preset"), QStringLiteral("gm_drums")}}).ok);
		m_editor->setFold(NoteFold::Drums);
		// GM 35..81 is keys 23..69, and both of the clip's keys (60, 64) are among them.
		QCOMPARE(m_editor->state().grid.rowCount(), 47);
		QCOMPARE(m_editor->keyLabel(24), QStringLiteral("Bass Drum 1"));
		QCOMPARE(m_editor->describe().value(QStringLiteral("fold")).toString(), QStringLiteral("drums"));
		QVERIFY(invoke(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), track}, {QStringLiteral("preset"), QStringLiteral("none")}}).ok);
		m_editor->setFold(NoteFold::Drums);
		QCOMPARE(m_editor->state().grid.rowCount(), 2);  // no names: only the used keys
		QVERIFY(m_editor->keyLabel(24).isEmpty());
	}

	void theNotesMenuOffersEveryNoteCommand()
	{
		QMenu* menu = notesMenu();
		QVERIFY(menu != nullptr);
		QStringList ids;
		for (QAction* action : test::leafActions(menu)) { ids << action->property("controlCommand").toString(); }
		for (const char* id : {"groove.quantize", "note.velocity_scale", "note.velocity_offset", "note.randomize", "note.scale",
			"note.ratchet", "note.transpose", "groove.apply", "groove.extract", "note.list", "note.transform_where",
			"note.select_where", "note.restore_timing",
			"key_names.preset", "window.note_editor_set", "script.run"})
		{
			QVERIFY2(ids.contains(QLatin1String(id)), id);
		}
		QAction* twice = test::menuAction(menu, {QStringLiteral("Timing"), QStringLiteral("Double Time (x2)")});
		QVERIFY(twice != nullptr);
		twice->trigger();
		QVERIFY2(noteAt(m_clip, 0, 60) != nullptr && noteAt(m_clip, 384, 64) != nullptr, "x2 doubles the positions");
		QCOMPARE(static_cast<int>(noteAt(m_clip, 384, 64)->length().getTicks()), 96);
		QVERIFY(undo());
		QVERIFY(noteAt(m_clip, 192, 64) != nullptr);
	}

	void theMuteToolTogglesTheNoteAndItsSelection()
	{
		m_editor->setTool(NoteTool::Mute);
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(10, 60));
		QVERIFY(noteAt(m_clip, 0, 60)->muted());
		QVERIFY2(!noteAt(m_clip, 192, 64)->muted(), "an unselected note elsewhere is left alone");
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(10, 60));
		QVERIFY(!noteAt(m_clip, 0, 60)->muted());
		select({m_clip->notes()[0], m_clip->notes()[1]});
		QTest::mouseClick(m_editor->canvas(), Qt::LeftButton, Qt::NoModifier, point(10, 60));
		QVERIFY(noteAt(m_clip, 0, 60)->muted() && noteAt(m_clip, 192, 64)->muted());
		QVERIFY(undo());  // the selection was one edit
		QVERIFY(!noteAt(m_clip, 0, 60)->muted() && !noteAt(m_clip, 192, 64)->muted());
		NoteLane release = NoteLane::Velocity;
		QVERIFY(notelanes::fromId(QStringLiteral("release"), release));
		const notelanes::Write write = notelanes::writeFor(release, control::clipIdOf(m_clip), control::noteIdOf(m_clip->notes()[0]), 80);
		QVERIFY(invoke(write.command, write.args).ok);
		QCOMPARE(m_clip->notes()[0]->releaseVelocity(), 80);
		QCOMPARE(notelanes::valueOf(*m_clip->notes()[0], release), 80);
	}

	void theNewViewFieldsAreDrivableOverTheSocket()
	{
		// window.note_editor_set needs the main window; the same fields go through the editor's setters.
		m_editor->setColorMode(NoteColor::Pitch);
		m_editor->setKeepInScale(true);
		m_editor->setTool(NoteTool::Paint);
		const QJsonObject state = m_editor->describe();
		QCOMPARE(state.value(QStringLiteral("color_mode")).toString(), QStringLiteral("pitch"));
		QCOMPARE(state.value(QStringLiteral("keep_in_scale")).toBool(), true);
		QCOMPARE(state.value(QStringLiteral("tool")).toString(), QStringLiteral("paint"));
		const ControlCommand* set = ControlRegistry::instance()->command(QStringLiteral("window.note_editor_set"));
		QVERIFY(set != nullptr);
		const QJsonObject props = set->argsSchema.value(QStringLiteral("properties")).toObject();
		QVERIFY(props.contains(QStringLiteral("color_mode")) && props.contains(QStringLiteral("keep_in_scale")));
		QTest::keyClick(m_editor, Qt::Key_6);
		QCOMPARE(m_editor->state().tool, NoteTool::Paint);
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

	//! The Notes menu, filled as it is when it opens.
	QMenu* notesMenu() const
	{
		auto* button = m_editor->findChild<QToolButton*>(QStringLiteral("noteEditorNotesMenu"));
		if (button == nullptr || button->menu() == nullptr) { return nullptr; }
		emit button->menu()->aboutToShow();
		return button->menu();
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
