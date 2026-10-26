/*
 * CanvasKeyboardTest.cpp - the editing canvases work without a mouse (0.5.0 UX decision, theme 10)
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

/*! Each canvas's keys asserted on what they leave and on their undo: the note area (moving between notes, resizing,
 *  velocity, inserting), the expression and controller lanes, the XY pad, the comp strip and the arranger's clip keys;
 *  and what a screen reader is told about a canvas - its role and its value. */

#include <QtTest>

#include <algorithm>

#include <QAccessible>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AccessibleCanvas.h"
#include "AccessibleWidgets.h"
#include "ArrangerClipKeys.h"
#include "AutomatableModel.h"
#include "CompSwipeStrip.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteEditorParts.h"
#include "NoteGrid.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "XyPad.h"

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

//! The notes selected, as "position:key" in the clip's order.
QStringList selected(const MidiClip* clip)
{
	QStringList out;
	for (const Note* note : clip->notes())
	{
		if (note->selected()) { out.append(QStringLiteral("%1:%2").arg(note->pos().getTicks()).arg(note->key())); }
	}
	out.sort();
	return out;
}

bool claimedBy(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers mods)
{
	QKeyEvent event(QEvent::ShortcutOverride, key, mods);
	event.ignore();
	QApplication::sendEvent(widget, &event);
	return event.isAccepted();
}

//! A key press sent to the widget, as the widget answered it (an unhandled one is not accepted).
bool handledBy(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers mods)
{
	QKeyEvent event(QEvent::KeyPress, key, mods);
	event.ignore();
	QApplication::sendEvent(widget, &event);
	return event.isAccepted();
}

} // namespace

class CanvasKeyboardTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
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
		// A: key 60 at 0..48, B: key 64 at 0..48 (a chord), C: key 62 at 192..288.
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(48), TimePos(0), 64, 100), false);
		m_clip->addNote(Note(TimePos(96), TimePos(192), 62, 100), false);
		m_clip->rearrangeAllNotes();
		m_editor = new NoteEditor;
		m_editor->resize(1200, 640);
		m_editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_editor));
		m_editor->setClip(m_clip);
		m_editor->setZoom(1.0);
		m_editor->setGridTicks(12);
	}

	void cleanup()
	{
		delete m_editor;
		m_editor = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void middleDragKeepsTheStartingKeyUnderThePointer()
	{
		auto* keyboard = m_editor->findChild<NoteKeyboardView*>(QStringLiteral("noteKeyboard"));
		QVERIFY(keyboard != nullptr);
		QVERIFY(keyboard->height() > 200);
		NoteGrid& grid = m_editor->state().grid;
		grid.setRowHeight(14);
		grid.setScrollY(0);
		const QPoint start(30, 200);
		const QPoint moved(30, 192);
		const int key = grid.keyAtY(start.y());
		QVERIFY(key >= 0);

		QTest::mousePress(keyboard, Qt::MiddleButton, Qt::NoModifier, start);
		QTest::mouseMove(keyboard, moved);

		QCOMPARE(grid.rowHeight(), 15);
		QCOMPARE(grid.keyAtY(moved.y()), key);
		QTest::mouseRelease(keyboard, Qt::MiddleButton, Qt::NoModifier, moved);
	}

	void altArrowsWalkTheNotesInTimeThenPitch()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::keyClick(canvas, Qt::Key_Right, Qt::AltModifier);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:60")});  // from none: the first note
		QTest::keyClick(canvas, Qt::Key_Right, Qt::AltModifier);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:64")});  // the chord, low to high
		QTest::keyClick(canvas, Qt::Key_Right, Qt::AltModifier);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("192:62")});
		QTest::keyClick(canvas, Qt::Key_Home);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:60")});
		QTest::keyClick(canvas, Qt::Key_Up, Qt::AltModifier);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:64")});  // the chord's note above
		QTest::keyClick(canvas, Qt::Key_Down, Qt::AltModifier);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:60")});
		QTest::keyClick(canvas, Qt::Key_End);
		QTest::keyClick(canvas, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier);  // Shift adds
		QCOMPARE(selected(m_clip), (QStringList{QStringLiteral("0:64"), QStringLiteral("192:62")}));
		// The note area's keys are its own over a window shortcut (the arranger's Alt+arrows).
		QVERIFY(claimedBy(canvas, Qt::Key_Right, Qt::AltModifier));
		QVERIFY(claimedBy(canvas, Qt::Key_Up, Qt::ControlModifier));
		QVERIFY(claimedBy(canvas, Qt::Key_Insert, Qt::NoModifier));
		QVERIFY(!claimedBy(canvas, Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier));
	}

	//! BUG 8 (cluster new-p12-154): with no clip the area keeps the focus and paints its no-clip line,
	//! so the cursor keys must find no clip to walk instead of reading through one.
	void theCursorKeysAreRefusedWhenNoClipIsOpen()
	{
		QWidget* canvas = m_editor->canvas();
		m_editor->setClip(nullptr);
		QCOMPARE(a11y::canvasValue(canvas), QStringLiteral("No clip open"));
		QTest::keyClick(canvas, Qt::Key_Right, Qt::AltModifier);
		QTest::keyClick(canvas, Qt::Key_Left, Qt::AltModifier);
		QTest::keyClick(canvas, Qt::Key_Up, Qt::AltModifier);
		QTest::keyClick(canvas, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
		QTest::keyClick(canvas, Qt::Key_Home);
		QTest::keyClick(canvas, Qt::Key_End);
		QTest::keyClick(canvas, Qt::Key_Right, Qt::ControlModifier);
		QTest::keyClick(canvas, Qt::Key_Insert);
		QTest::keyClick(canvas, Qt::Key_Delete);
		QVERIFY(m_editor->clip() == nullptr);  // and the area still reads no clip
		QCOMPARE(a11y::canvasValue(canvas), QStringLiteral("No clip open"));
		QVERIFY(!handledBy(canvas, Qt::Key_Right, Qt::AltModifier));  // the cursor key was not taken
		QVERIFY(!handledBy(canvas, Qt::Key_Home, Qt::NoModifier));
	}

	//! Q050-099: an MPE lane edits every SELECTED note in one undo step, not only the cursor
	//! note, and its readout says how many notes the value applies to.
	void anMpeLaneEditsEverySelectedNoteInOneStep()
	{
		m_editor->setLane(NoteLane::Timbre);
		QWidget* lane = m_editor->laneView();
		lane->setFocus();
		const auto selectTheChord = [this] {
			return invoke(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()},
				{QStringLiteral("notes"), QJsonArray{control::noteIdOf(noteAt(m_clip, 0, 60)), control::noteIdOf(noteAt(m_clip, 0, 64))}}}).ok;
		};
		QVERIFY(selectTheChord());
		QTest::keyClick(lane, Qt::Key_Up);
		QCOMPARE(noteAt(m_clip, 0, 60)->mpeTimbre(), 1);
		QCOMPARE(noteAt(m_clip, 0, 64)->mpeTimbre(), 1);
		QVERIFY2(a11y::canvasValue(lane).contains(QStringLiteral("2 selected")), qPrintable(a11y::canvasValue(lane)));
		QVERIFY(undo());
		QCOMPARE(noteAt(m_clip, 0, 60)->mpeTimbre(), 0);
		QCOMPARE(noteAt(m_clip, 0, 64)->mpeTimbre(), 0);

		// Micro-pitch uses the same batch path; a note outside the selection stays where it was.
		m_editor->setLane(NoteLane::Pitch);
		lane = m_editor->laneView();
		QVERIFY(selectTheChord());
		QTest::keyClick(lane, Qt::Key_Up);
		QCOMPARE(noteAt(m_clip, 0, 60)->mpePitchCents(), 1);
		QCOMPARE(noteAt(m_clip, 0, 64)->mpePitchCents(), 1);
		QCOMPARE(noteAt(m_clip, 192, 62)->mpePitchCents(), 0);
		QVERIFY(undo());
	}

	void ctrlArrowsResizeAndChangeVelocityOneStepEach()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::keyClick(canvas, Qt::Key_End);
		Note* c = noteAt(m_clip, 192, 62);
		QVERIFY(c != nullptr && c->selected());
		QTest::keyClick(canvas, Qt::Key_Right, Qt::ControlModifier);
		QCOMPARE(noteAt(m_clip, 192, 62)->length().getTicks(), 108);  // a grid step longer
		QCOMPARE(m_editor->state().lastLength, 108);
		QVERIFY(undo());
		QCOMPARE(noteAt(m_clip, 192, 62)->length().getTicks(), 96);
		QTest::keyClick(canvas, Qt::Key_End);
		QTest::keyClick(canvas, Qt::Key_Up, Qt::ControlModifier);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 105);
		QTest::keyClick(canvas, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 104);
	}

	void unsupportedModifiersDoNotNudgeTheSelection()
	{
		QVERIFY(invoke(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("notes"), QJsonArray{control::noteIdOf(noteAt(m_clip, 192, 62))}}}).ok);
		QTest::keyClick(m_editor->canvas(), Qt::Key_Up, Qt::MetaModifier);
		QVERIFY(noteAt(m_clip, 192, 62) != nullptr);
		QVERIFY(noteAt(m_clip, 192, 63) == nullptr);
		QTest::keyClick(m_editor->canvas(), Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier);
		QVERIFY(noteAt(m_clip, 192, 62) == nullptr);
		QVERIFY(noteAt(m_clip, 192, 63) != nullptr);
	}

	void insertAddsANoteAfterTheCursor()
	{
		QWidget* canvas = m_editor->canvas();
		QTest::keyClick(canvas, Qt::Key_End);
		m_editor->state().lastLength = 24;
		QTest::keyClick(canvas, Qt::Key_Insert);
		QCOMPARE(m_clip->notes().size(), std::size_t{4});
		Note* added = noteAt(m_clip, 288, 62);  // at the cursor note's end, on its key
		QVERIFY(added != nullptr);
		QCOMPARE(added->length().getTicks(), 24);
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("288:62")});  // and it is the cursor now
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
	}

	void theAreaIsReadAsACanvasWithTheCursorNote()
	{
		QWidget* canvas = m_editor->canvas();
		QCOMPARE(a11y::canvasRole(canvas), QAccessible::Canvas);
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(canvas);
		QVERIFY(face != nullptr);
		QCOMPARE(face->role(), QAccessible::Canvas);
		QVERIFY2(face->text(QAccessible::Value).contains(QStringLiteral("3 notes, 0 selected")), qPrintable(face->text(QAccessible::Value)));
		QTest::keyClick(canvas, Qt::Key_End);
		QVERIFY(face->text(QAccessible::Value).startsWith(NoteGrid::keyName(62)));
		QVERIFY2(face->text(QAccessible::Value).contains(musicaltime::positionText(192)), qPrintable(face->text(QAccessible::Value)));
		// CanvasKeyCursor has no Q_OBJECT, so findChild<CanvasKeyCursor*> matched any QFocusFrame (and Qt 6.12 refuses it).
		const auto frames = canvas->findChildren<QFocusFrame*>();
		QVERIFY(std::any_of(frames.begin(), frames.end(),
			[](QFocusFrame* frame) { return dynamic_cast<a11y::CanvasKeyCursor*>(frame) != nullptr; }));
		QVERIFY(!a11y::CanvasKeyCursor::of(canvas)->mark().isEmpty());  // the cursor note is marked
	}

	void invalidValueDoesNotChangeAValueControlModel()
	{
		a11y::installAccessibleWidgets();
		FloatModel model(50.f, 0.f, 100.f, 1.f, nullptr, QStringLiteral("Gain"));
		Knob knob(KnobType::Bright26);
		knob.setModel(&model);
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(&knob);
		QVERIFY(face != nullptr);
		QAccessibleValueInterface* value = face->valueInterface();
		QVERIFY(value != nullptr);

		value->setCurrentValue(QVariant(QStringLiteral("not numeric")));
		QCOMPARE(model.value(), 50.f);
		value->setCurrentValue(QVariant(25.f));
		QCOMPARE(model.value(), 25.f);
	}

	void theExpressionLaneStepsTheSelectionInOneStep()
	{
		QWidget* lane = m_editor->laneView();
		QCOMPARE(a11y::canvasRole(lane), QAccessible::Slider);
		QVERIFY((lane->focusPolicy() & Qt::TabFocus) != 0);
		const auto selectTheChord = [this] {
			return invoke(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()},
				{QStringLiteral("notes"), QJsonArray{control::noteIdOf(noteAt(m_clip, 0, 60)), control::noteIdOf(noteAt(m_clip, 0, 64))}}}).ok;
		};
		QVERIFY(selectTheChord());
		QTest::keyClick(lane, Qt::Key_PageUp);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 110);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 64)->getVolume()), 110);
		QVERIFY(undo());  // both, one step
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 60)->getVolume()), 100);
		QCOMPARE(static_cast<int>(noteAt(m_clip, 0, 64)->getVolume()), 100);
		// The undo restored the clip's checkpoint, whose notes come back unselected (MidiClip::loadSettings).
		QVERIFY(selectTheChord());
		QTest::keyClick(lane, Qt::Key_Right);  // from the first selected note to the next
		QCOMPARE(selected(m_clip), QStringList{QStringLiteral("0:64")});
		QVERIFY(a11y::canvasValue(lane).contains(QStringLiteral("100")));
	}

	void theControllerLaneEditsPointsAtItsCursor()
	{
		m_editor->setControllerLane(QStringLiteral("cc:1"));
		auto* lane = m_editor->findChild<QWidget*>(QStringLiteral("noteControllerLane"));
		QVERIFY(lane != nullptr);
		const auto points = [this] {
			const ControllerLane* shown = m_clip->controllers().lane(ControllerKind::ControlChange, 1);
			return shown != nullptr ? shown->points : std::vector<ControllerPoint>{};
		};
		QTest::keyClick(lane, Qt::Key_Up);  // no point at the cursor: one is made, a step above the middle
		QCOMPARE(points().size(), std::size_t{1});
		QCOMPARE(points().front().value, 65);
		QTest::keyClick(lane, Qt::Key_Right);
		QTest::keyClick(lane, Qt::Key_Insert);  // a point with the curve's value there
		QCOMPARE(points().size(), std::size_t{2});
		QCOMPARE(static_cast<int>(points().back().tick), 12);
		QCOMPARE(points().back().value, 65);
		QTest::keyClick(lane, Qt::Key_Delete);
		QCOMPARE(points().size(), std::size_t{1});
		QVERIFY(undo());
		QCOMPARE(points().size(), std::size_t{2});
		QVERIFY(a11y::canvasValue(lane).startsWith(QStringLiteral("cc:1")));
	}

	void theXyPadMovesFromTheKeys()
	{
		XyPadArea pad;
		QPointF reported(-1, -1);
		pad.moved = [&reported](QPointF point, unsigned) { reported = point; };
		pad.setPoint(QPointF(0.5, 0.5));
		pad.setAxisNames(QStringLiteral("Cutoff"), QStringLiteral("Drive"));
		QTest::keyClick(&pad, Qt::Key_Right);
		QVERIFY(std::abs(reported.x() - 0.51) < 1e-9);
		QTest::keyClick(&pad, Qt::Key_Up, Qt::ShiftModifier);
		QVERIFY(std::abs(reported.y() - 0.6) < 1e-9);
		QTest::keyClick(&pad, Qt::Key_Home);
		QCOMPARE(reported.x(), 0.0);
		QCOMPARE(a11y::canvasValue(&pad), QStringLiteral("X Cutoff 0 %, Y Drive 60 %"));
		QVERIFY(claimedBy(&pad, Qt::Key_Down, Qt::NoModifier));
	}

	void theCompStripHasAKeyboardCursor()
	{
		QWidget host;
		CompSwipeStrip strip(QStringLiteral("trk-999"), &host);
		strip.resize(400, 60);
		strip.setState({{QStringLiteral("lanes"), QJsonArray{QJsonObject{{QStringLiteral("lane"), 0}}, QJsonObject{{QStringLiteral("lane"), 1}}}},
			{QStringLiteral("composite"), QJsonObject{{QStringLiteral("begin"), 0}, {QStringLiteral("end"), 768},
				{QStringLiteral("segments"), QJsonArray{
					QJsonObject{{QStringLiteral("begin"), 0}, {QStringLiteral("end"), 384}, {QStringLiteral("lane"), 0}},
					QJsonObject{{QStringLiteral("begin"), 384}, {QStringLiteral("end"), 768}, {QStringLiteral("lane"), 1}}}}}}});
		QVERIFY((strip.focusPolicy() & Qt::TabFocus) != 0);
		host.resize(420, 80);
		host.show();
		QVERIFY(QTest::qWaitForWindowExposed(&host));
		QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QVERIFY(strip.hasFocus());
		QCOMPARE(strip.keyLane(), 0);
		QTest::keyClick(&strip, Qt::Key_Down);
		QCOMPARE(strip.keyLane(), 1);
		QTest::keyClick(&strip, Qt::Key_Right);
		const int beat = strip.keyTick();
		QVERIFY(beat > 0);
		QTest::keyClick(&strip, Qt::Key_Right, Qt::ShiftModifier);
		QCOMPARE(strip.keyAnchor(), beat);
		QVERIFY(strip.accessibleValue().contains(QStringLiteral("marked")));
		QTest::keyClick(&strip, Qt::Key_Escape);
		QCOMPARE(strip.keyAnchor(), -1);
		QVERIFY(strip.accessibleValue().contains(QStringLiteral("plays lane 0")));
		// A comp on a track that is not there is refused - and said, at once.
		QTest::keyClick(&strip, Qt::Key_Return);
		QVERIFY2(a11y::lastAnnouncement().startsWith(QStringLiteral("Not done")), qPrintable(a11y::lastAnnouncement()));
	}

	//! Only a left double-click cycles the take: a right or middle one is not the gesture the
	//! strip offers, so it must not reach comp.cycle (which changes the project).
	void onlyALeftDoubleClickCyclesTheTake()
	{
		QWidget host;
		CompSwipeStrip strip(QStringLiteral("trk-999"), &host);
		strip.resize(400, 60);
		QStringList ran;
		QObject::connect(&strip, &CompSwipeStrip::ran, [&ran](const QString& status) { ran.append(status); });
		const QPoint at(100, 20);
		for (const Qt::MouseButton button : {Qt::RightButton, Qt::MiddleButton})
		{
			const int before = ran.size();
			QTest::mouseDClick(&strip, button, Qt::NoModifier, at);
			QVERIFY2(ran.size() == before, "a non-left double-click reached comp.cycle");
		}
		// The left double-click still cycles: the gesture the strip offers.
		const int before = ran.size();
		QTest::mouseDClick(&strip, Qt::LeftButton, Qt::NoModifier, at);
		QCOMPARE(ran.size(), before + 1);
	}

	void enterAtExclusiveTakeSpanEndDoesNotExtendTheComp()
	{
		const QString trackId = control::trackIdOf(m_track);
		const ControlResult lane = invoke(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), trackId}});
		QVERIFY2(lane.ok, qPrintable(lane.errorMessage));
		QCOMPARE(lane.result.value(QStringLiteral("lane")).toInt(), 0);

		const ControlResult before = invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), trackId}});
		QVERIFY2(before.ok, qPrintable(before.errorMessage));
		QVERIFY(before.result.value(QStringLiteral("composite")).toObject()
			.value(QStringLiteral("segments")).toArray().isEmpty());

		QWidget host;
		CompSwipeStrip strip(trackId, &host);
		strip.resize(400, 60);
		strip.setState(before.result);
		const int spanEnd = m_clip->endPosition().getTicks();
		QVERIFY(spanEnd > 0);
		QTest::keyClick(&strip, Qt::Key_End);
		QCOMPARE(strip.keyTick(), spanEnd);
		QTest::keyClick(&strip, Qt::Key_Return);

		const ControlResult after = invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), trackId}});
		QVERIFY2(after.ok, qPrintable(after.errorMessage));
		QVERIFY2(after.result.value(QStringLiteral("composite")).toObject()
			.value(QStringLiteral("segments")).toArray().isEmpty(),
			"Enter at the exclusive take-span end must not create a segment past the takes");
	}

	void theArrangerAddsItsOwnClipKeys()
	{
		QKeyEvent across(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
		QKeyEvent resize(QEvent::KeyPress, Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
		QKeyEvent menus(QEvent::KeyPress, Qt::Key_Down, Qt::AltModifier);
		QVERIFY(arrangerkeys::claims(&across));
		QVERIFY(arrangerkeys::claims(&resize));
		QVERIFY2(!arrangerkeys::claims(&menus), "Alt+Down stays the Edit menu's Next Clip");
		QVERIFY(!arrangerkeys::handle(nullptr, &across));
		m_clip->setName(QStringLiteral("Hook"));
		QVERIFY(arrangerkeys::describeClip(m_clip).startsWith(QStringLiteral("Hook, on ")));
	}

private:
	QTemporaryDir m_home;
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	NoteEditor* m_editor = nullptr;
};

QTEST_MAIN(CanvasKeyboardTest)
#include "CanvasKeyboardTest.moc"
