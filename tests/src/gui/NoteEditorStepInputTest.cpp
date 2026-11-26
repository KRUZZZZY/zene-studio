/*
 * NoteEditorStepInputTest.cpp - the note editor's Step input mode, driven in the real application
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

/*! Q050-211: step input in the note editor (docs/NOTE-EDITOR.md §3a).
 *
 *  The REAL application: the shell's own note editor, the toolbar's Step input and Record buttons
 *  clicked with the mouse, the grid chosen in the header's grid box, the note length set through the
 *  command the New Note Length menu uses, MIDI keys arriving as the instrument track's midiNoteOn and
 *  midiNoteOff (the signals a MIDI keyboard makes - the same ones PianoRollRecordTest injects) and a
 *  computer key typed at the note area.
 *
 *  Step input (docs/NOTE-EDITOR.md §3a): every key is one registered `note.step_input`, so one undo
 *  takes one step back, and the stop paths (transport stop, disarm, clip change) write nothing and
 *  leave nothing pending. Record (§3b): a key opens a note at the playhead and its release closes it,
 *  ONE undo step covers the whole take, the transport's stop ends it, and a disarm or a clip change
 *  drops both MIDI edges. The two entry modes share this executable because they share the editor's
 *  one MIDI path and its one set of mode switches.
 *
 *  The test reads the mode and the cursor through NoteEditor::describe() - the object window.get_state
 *  reports as `note_editor` - so it compiles against a tree without the change and fails on behaviour, not
 *  on a missing symbol.
 */

#include <QtTest>

#include <QComboBox>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "ShellChrome.h"
#include "Song.h"
#include "Track.h"

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

//! Step input's own state, read the way window.get_state reads it - the editor's describe() object.
bool stepArmed(NoteEditor* editor) { return editor->describe().value(QStringLiteral("step_input")).toBool(); }
int stepCursor(NoteEditor* editor) { return editor->describe().value(QStringLiteral("step_cursor")).toInt(); }

//! The toolbar's Step input button: the ShellButton whose action text is the mode's name.
ShellButton* stepButton(NoteEditor* editor)
{
	for (ShellButton* button : editor->findChildren<ShellButton*>())
	{
		if (button->text() == QStringLiteral("Step input")) { return button; }
	}
	return nullptr;
}

//! The header's grid box, by the label it shows: the user's own control for the snap (and the step).
void chooseGrid(NoteEditor* editor, const QString& label)
{
	for (QComboBox* box : editor->findChildren<QComboBox*>())
	{
		const int index = box->findText(label);
		if (index >= 0) { box->setCurrentIndex(index); return; }
	}
	QFAIL("the header carries no grid box with that label");
}

//! Arm or disarm Step input the way the user does: a mouse click on the toolbar button.
void clickStepButton(NoteEditor* editor)
{
	ShellButton* button = stepButton(editor);
	QVERIFY(button != nullptr);
	QVERIFY2(button->isVisible(), "the Step input button is on the toolbar, where the user clicks it");
	QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, button->rect().center());
}

//! A MIDI keyboard's note-on, delivered to the editor's queued slot. The note is the live handle a
//! midi-input NotePlayHandle would carry; only its key matters to a step.
void midiNoteOn(InstrumentTrack* track, int key)
{
	Q_EMIT track->midiNoteOn(Note(TimePos(0), TimePos(0), key));
	QCoreApplication::processEvents();  // the connection is queued: the slot runs on this thread's next pass
}

void midiNoteOff(InstrumentTrack* track, int key)
{
	Q_EMIT track->midiNoteOff(Note(TimePos(0), TimePos(0), key));
	QCoreApplication::processEvents();
}

//! Record mode's own state, read the way window.get_state reads it - the editor's describe() object.
bool recording(NoteEditor* editor) { return editor->describe().value(QStringLiteral("record")).toBool(); }

//! The toolbar's Record button: the ShellButton whose action text is the mode's name.
ShellButton* recordButton(NoteEditor* editor)
{
	for (ShellButton* button : editor->findChildren<ShellButton*>())
	{
		if (button->text() == QStringLiteral("Record")) { return button; }
	}
	return nullptr;
}

//! Arm or disarm Record the way the user does: a mouse click on the toolbar button.
void clickRecordButton(NoteEditor* editor)
{
	ShellButton* button = recordButton(editor);
	QVERIFY(button != nullptr);
	QVERIFY2(button->isVisible(), "the Record button is on the toolbar, where the user clicks it");
	QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, button->rect().center());
}

//! The transport's playhead, moved without playing: where a take recorded while stopped starts.
void seekTo(int ticks) { Engine::getSong()->setPlayPos(TimePos(ticks), Song::PlayMode::Song); }

//! A note's length at @a position and @a key, or -1 when there is none.
int lengthOf(MidiClip* clip, int position, int key)
{
	const Note* note = noteAt(clip, position, key);
	return note == nullptr ? -1 : static_cast<int>(note->length().getTicks());
}

} // namespace

class NoteEditorStepInputTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr && getGUI()->noteEditor() != nullptr);
		NotePlayHandleManager::init();  // the transport plays the clip's notes
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
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
		Engine::getSong()->stop();
		Engine::getSong()->setPlayPos(0, Song::PlayMode::Song);
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		m_editor = getGUI()->noteEditor();
		// The editor the shell shows, opened on this clip the way a double-click on it does.
		getGUI()->mainWindow()->revealPanel(QStringLiteral("detail"));
		m_editor->setClip(m_clip);
		// Every case starts from a known view: Step input off, a sixteenth grid, the last note's length.
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("step_input"), false},
			{QStringLiteral("record"), false}, {QStringLiteral("grid"), 12},
			{QStringLiteral("new_length"), 0}}).ok);
		QVERIFY(!stepArmed(m_editor));
		QVERIFY(!recording(m_editor));
	}

	void cleanup()
	{
		m_editor->setClip(nullptr);
		delete m_clip;
		m_clip = nullptr;
		Engine::getSong()->stop();
	}

	//! The toolbar button arms the mode through the registered command, and only the mode (no undo step).
	void clickingTheStepButtonArmsTheModeThroughItsCommand()
	{
		ShellButton* button = stepButton(m_editor);
		QVERIFY(button != nullptr);
		QVERIFY2(!button->isChecked(), "Step input starts off");
		QVERIFY(!stepArmed(m_editor));
		const int depth = Engine::projectJournal()->undoDepth();

		clickStepButton(m_editor);
		QVERIFY2(stepArmed(m_editor), "a click on the button arms Step input");
		QVERIFY2(button->isChecked(), "and the button shows it");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);  // window.note_editor_set journals nothing
		// The agent's view of the same switch: window.get_state reports the editor's own object.
		const QJsonObject state = invoke(QStringLiteral("window.get_state")).result.value(QStringLiteral("note_editor")).toObject();
		QVERIFY2(state.value(QStringLiteral("step_input")).toBool(), "window.get_state does not report Step input");

		// A command from elsewhere moves the same switch: the button follows the state, not only its own clicks.
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("step_input"), false}}).ok);
		QVERIFY(!stepArmed(m_editor));
		QVERIFY2(!button->isChecked(), "and the button follows a command that disarmed it");
	}

	//! Criterion 1: a MIDI key enters a note at the cursor, the cursor advances by the grid step, and a
	//! short phrase is recorded - each step one note.step_input and one undo step.
	void aMidiKeyboardEntersAStepAndAdvancesByTheSelectedGrid()
	{
		clickStepButton(m_editor);
		QVERIFY(stepArmed(m_editor));
		chooseGrid(m_editor, QStringLiteral("1/8"));  // the user's grid: 24 ticks
		QCOMPARE(m_editor->state().gridTicks, 24);
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"),
			{{QStringLiteral("new_length"), 30}}).ok);  // New Note Length, as the menu sets it

		const int depth = Engine::projectJournal()->undoDepth();
		midiNoteOn(m_track, 60);
		QCOMPARE(m_clip->notes().size(), std::size_t{1});
		const Note* first = noteAt(m_clip, 0, 60);
		QVERIFY2(first != nullptr, "the first key landed at the clip start");
		QCOMPARE(static_cast<int>(first->length().getTicks()), 30);
		QVERIFY2(first->selected(), "the step's note is the selection");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // one step, one undo step
		QCOMPARE(stepCursor(m_editor), 24);

		midiNoteOn(m_track, 64);
		midiNoteOn(m_track, 67);
		QVERIFY2(noteAt(m_clip, 24, 64) != nullptr, "the second key landed one grid step on");
		QVERIFY2(noteAt(m_clip, 48, 67) != nullptr, "the third landed one grid step after that");
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		QCOMPARE(stepCursor(m_editor), 72);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 3);
		QVERIFY2(!noteAt(m_clip, 24, 64)->selected(), "the new note is the selection, not the old one");

		// ONE undo takes back ONE step: the short phrase unwinds a step at a time.
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QVERIFY(noteAt(m_clip, 48, 67) == nullptr);
		QVERIFY(undo());
		QVERIFY(undo());
		QVERIFY2(m_clip->notes().empty(), "three steps, three undos");
	}

	//! Criterion 2: a computer key and a MIDI key enter the same note length on the same grid - both
	//! read the editor's settings, and the letters reach the editor from the focused note area.
	void computerKeysAndTheMidiKeyboardShareTheGridAndLength()
	{
		clickStepButton(m_editor);
		QVERIFY(stepArmed(m_editor));
		chooseGrid(m_editor, QStringLiteral("1/8"));
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("new_length"), 30}}).ok);
		QCOMPARE(m_editor->state().tool, NoteTool::Draw);

		// QTest::keyClick reaches the note area first; a letter the mode claims is entered as a step.
		QTest::keyClick(m_editor->canvas(), Qt::Key_Z);  // Z = C at the piano view's octave
		const int letterKey = DefaultOctave + Key::C - KeysPerOctave;
		const Note* typed = noteAt(m_clip, 0, letterKey);
		QVERIFY2(typed != nullptr, "the computer key entered a note at the cursor");
		QCOMPARE(static_cast<int>(typed->length().getTicks()), 30);

		midiNoteOn(m_track, 64);
		const Note* played = noteAt(m_clip, 24, 64);
		QVERIFY2(played != nullptr, "the MIDI key landed on the next step");
		QCOMPARE(static_cast<int>(played->length().getTicks()), 30);  // the SAME note length
		QCOMPARE(static_cast<int>(played->pos().getTicks())
			- static_cast<int>(typed->pos().getTicks()), 24);         // the SAME grid step
		QCOMPARE(m_clip->notes().size(), std::size_t{2});

		// The letters are the tools again once the mode is off (the tool did not silently change here).
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("step_input"), false}}).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QTest::keyClick(m_editor->canvas(), Qt::Key_B);  // Paint, unarmed
		QCOMPARE(m_editor->state().tool, NoteTool::Paint);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});  // a letter never writes a note when the mode is off
	}

	//! Criterion 3: the transport's stop, the mode's disarm and a clip change are safe - a key held
	//! across the stop leaves nothing pending, and no key pressed afterwards writes into a clip the
	//! editor no longer holds.
	void stoppingTheTransportLeavesNothingPending()
	{
		clickStepButton(m_editor);
		QVERIFY(stepArmed(m_editor));
		chooseGrid(m_editor, QStringLiteral("1/16"));
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("new_length"), 12}}).ok);
		Engine::getSong()->playSong();
		QVERIFY(Engine::getSong()->isPlaying());

		midiNoteOn(m_track, 60);
		midiNoteOn(m_track, 64);  // and its key-up never comes: the transport stops under it
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		const int cursor = stepCursor(m_editor);
		QCOMPARE(cursor, 24);

		Engine::getSong()->stop();
		QVERIFY(!Engine::getSong()->isPlaying());
		QCOMPARE(m_clip->notes().size(), std::size_t{2});  // stopping wrote nothing
		QCOMPARE(stepCursor(m_editor), cursor);
		QVERIFY2(stepArmed(m_editor), "the mode survives a stop");

		// The key-up arrives after the stop: there was no pending start, so it pairs with nothing.
		midiNoteOff(m_track, 64);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});

		// Disarming drops the MIDI connection: a key pressed now reaches no editor.
		clickStepButton(m_editor);
		QVERIFY(!stepArmed(m_editor));
		midiNoteOn(m_track, 67);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});

		// Armed again, but the clip goes: no key may write through the old connection.
		clickStepButton(m_editor);
		QVERIFY(stepArmed(m_editor));
		m_editor->setClip(nullptr);
		midiNoteOn(m_track, 72);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QCOMPARE(stepCursor(m_editor), 0);  // no clip, no cursor
	}

	//! Record: the toolbar button arms the mode through the registered command, journals nothing, and the
	//! two entry modes exclude each other.
	void clickingTheRecordButtonArmsTheModeThroughItsCommand()
	{
		ShellButton* button = recordButton(m_editor);
		QVERIFY(button != nullptr);
		QVERIFY2(!button->isChecked(), "Record starts off");
		QVERIFY(!recording(m_editor));
		const int depth = Engine::projectJournal()->undoDepth();

		clickRecordButton(m_editor);
		QVERIFY2(recording(m_editor), "a click on the button arms Record");
		QVERIFY2(button->isChecked(), "and the button shows it");
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);  // window.note_editor_set journals nothing
		const QJsonObject state = invoke(QStringLiteral("window.get_state")).result.value(QStringLiteral("note_editor")).toObject();
		QVERIFY2(state.value(QStringLiteral("record")).toBool(), "window.get_state does not report Record");

		// Recording and step input are the editor's two entry modes: arming one turns the other off.
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("step_input"), true}}).ok);
		QVERIFY(stepArmed(m_editor));
		QVERIFY2(!recording(m_editor), "arming Step input disarms Record");
		QVERIFY2(!button->isChecked(), "and the Record button follows");
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("record"), true}}).ok);
		QVERIFY(recording(m_editor));
		QVERIFY2(button->isChecked(), "the button follows a command that armed Record");
		QVERIFY2(!stepArmed(m_editor), "and Step input went off with it");
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("record"), false}}).ok);
		QVERIFY2(!button->isChecked(), "and one that disarmed it");
	}

	//! Criterion 1 for the recording half: what a MIDI keyboard plays is written where the playhead
	//! is, on the grid, and ONE undo takes the whole take back.
	void aPlayedPhraseIsRecordedAsOneTake()
	{
		clickRecordButton(m_editor);
		QVERIFY(recording(m_editor));
		QVERIFY(invoke(QStringLiteral("window.note_editor_set"), {{QStringLiteral("grid"), 24}}).ok);  // 1/8
		const int depth = Engine::projectJournal()->undoDepth();

		seekTo(48);
		midiNoteOn(m_track, 60);
		QVERIFY2(noteAt(m_clip, 48, 60) != nullptr, "the key opened a note where the playhead was");
		QCOMPARE(lengthOf(m_clip, 48, 60), 24);  // the note-on opens it one grid step long
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // the take's one checkpoint
		seekTo(96);
		midiNoteOff(m_track, 60);
		QCOMPARE(lengthOf(m_clip, 48, 60), 48);  // the key-up closed it where the playhead was

		midiNoteOn(m_track, 64);  // at 96, where the seek above left the playhead
		QVERIFY(noteAt(m_clip, 96, 64) != nullptr);
		seekTo(144);
		midiNoteOff(m_track, 64);
		QCOMPARE(lengthOf(m_clip, 96, 64), 48);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // still ONE step for the take

		// A note of the phrase goes down while the transport runs, and it exists at once.
		Engine::getSong()->playSong();
		QVERIFY(Engine::getSong()->isPlaying());
		midiNoteOn(m_track, 67);
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		Engine::getSong()->stop();

		// ONE undo takes the take back - all of it, the note the stop closed included.
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(m_clip->notes().empty(), "one undo takes the whole take back");
	}

	//! The transport's stop finalises the take: a key still held is kept (one grid step), the take is
	//! closed, and the next key starts a take of its own.
	void stoppingTheTransportFinishesTheTake()
	{
		clickRecordButton(m_editor);
		QVERIFY(recording(m_editor));
		seekTo(0);
		midiNoteOn(m_track, 60);
		QVERIFY(noteAt(m_clip, 0, 60) != nullptr);
		QCOMPARE(lengthOf(m_clip, 0, 60), 12);

		Engine::getSong()->playSong();
		Engine::getSong()->stop();
		QVERIFY2(!Engine::getSong()->isPlaying(), "the transport stopped");
		QCOMPARE(m_clip->notes().size(), std::size_t{1});
		QCOMPARE(lengthOf(m_clip, 0, 60), 12);  // kept as the step it was given, not lost
		QVERIFY2(recording(m_editor), "the mode survives a stop");

		// The take is closed: a late key-up has nothing to close, and the next key opens a new step.
		const QJsonObject late = invoke(QStringLiteral("note.record_off"),
			{{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("key"), 60}}).result;
		QVERIFY2(late.value(QStringLiteral("closed")).toBool() == false, "the stop closed the take");
		seekTo(0);
		midiNoteOn(m_track, 64);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QVERIFY2(noteAt(m_clip, 0, 60) != nullptr, "the first take is its own undo step");
		QVERIFY2(noteAt(m_clip, 0, 64) == nullptr, "and the second take is the one undone");
	}

	//! Disarming and opening another clip end the take and drop the MIDI edges: a key pressed
	//! afterwards writes nothing.
	void disarmingAndAnotherClipEndTheTake()
	{
		clickRecordButton(m_editor);
		QVERIFY(recording(m_editor));
		seekTo(24);
		midiNoteOn(m_track, 72);
		QVERIFY(noteAt(m_clip, 24, 72) != nullptr);
		const std::size_t written = m_clip->notes().size();

		clickRecordButton(m_editor);  // disarm, the way the user does
		QVERIFY(!recording(m_editor));
		const QJsonObject late = invoke(QStringLiteral("note.record_off"),
			{{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("key"), 72}}).result;
		QVERIFY2(late.value(QStringLiteral("closed")).toBool() == false, "disarming closed the take");
		QCOMPARE(m_clip->notes().size(), written);
		midiNoteOn(m_track, 74);
		QCOMPARE(m_clip->notes().size(), written);  // the connection is gone

		// Armed again, but the clip goes: no key may write through the old connection.
		clickRecordButton(m_editor);
		QVERIFY(recording(m_editor));
		m_editor->setClip(nullptr);
		QCOMPARE(m_clip->notes().size(), written);
		midiNoteOn(m_track, 76);
		QCOMPARE(m_clip->notes().size(), written);
	}

private:
	QTemporaryDir m_home;
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	NoteEditor* m_editor = nullptr;  // the shell's editor: it owns it
};

QTEST_MAIN(NoteEditorStepInputTest)
#include "NoteEditorStepInputTest.moc"
