/*
 * SessionLibraryMidiTest.cpp - Q050-273.2: a MIDI launcher cell saved into the shared library and reused
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

/*! Q050-273.2, driven the way a person drives it: right-click a populated MIDI launcher cell, choose
 *  "Save to Library...", confirm the name in the command form's own Run button, and a Standard MIDI
 *  File appears in the shared Clips library holding every note of the pattern the cell references -
 *  across all of the PatternStore's tracks, not just the one the cell's track header names. The file
 *  is then read back with the product's own defensive SMF reader, the project is saved and reopened,
 *  and the entry is dragged from the browser onto another cell - the grid's own drop path, which runs
 *  session.import_midi_cell - where it becomes a playable pattern with the same notes. Saving THAT
 *  cell produces the same notes again, so the round trip is stable.
 *
 *  The menu entry runs browser.save_clip with {track, scene} fixed, so the GUI and an agent on the
 *  socket are the same command; the agent half (no window at all) is tests/control-session-library-midi.py.
 *  Failure matrix, undo/redo and the command's own refusals live in that agent half; this file proves
 *  the interface's two routes and the note content.
 */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QToolButton>

#include <algorithm>
#include <limits>
#include <span>
#include <vector>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "Midi.h"
#include "MidiClip.h"
#include "MidiSmfWrite.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SessionGridView.h"
#include "SmfNoteRead.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject slotState(int track, int scene)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == track && slot.value(QStringLiteral("scene")).toInt() == scene) { return slot; }
	}
	return {};
}

CommandForm* visibleForm()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* form = qobject_cast<CommandForm*>(widget); form != nullptr && form->isVisible()) { return form; }
	}
	return nullptr;
}

void clickRun(CommandForm* form)
{
	for (QPushButton* button : form->findChildren<QPushButton*>())
	{
		if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { QTest::mouseClick(button, Qt::LeftButton); }
	}
}

//! One note read back from a saved file: start and length in LMMS ticks, key, MIDI velocity.
struct SavedNote
{
	long long start = 0;
	long long length = 0;
	int key = 0;
	int velocity = 0;
	bool operator<(const SavedNote& other) const
	{
		if (start != other.start) { return start < other.start; }
		if (key != other.key) { return key < other.key; }
		if (length != other.length) { return length < other.length; }
		return velocity < other.velocity;
	}
	bool operator==(const SavedNote& other) const
	{
		return start == other.start && length == other.length && key == other.key && velocity == other.velocity;
	}
};

//! The notes of a saved MIDI file, through the same defensive reader an import uses.
bool notesInFile(const QString& path, std::vector<SavedNote>* notes, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("cannot open %1").arg(path);
		return false;
	}
	const interchange::SmfNoteRead read = interchange::readSmfNotes(file.readAll());
	if (!read.ok)
	{
		*error = read.error;
		return false;
	}
	for (const interchange::SmfNote& note : read.notes)
	{
		notes->push_back(SavedNote{note.start, note.length, note.key, note.velocity});
	}
	std::sort(notes->begin(), notes->end());
	return true;
}

//! Every note of @a pattern across the PatternStore's tracks.
int notesInPattern(int pattern)
{
	int count = 0;
	for (Track* track : Engine::patternStore()->tracks())
	{
		if (pattern < track->numOfClips())
		{
			if (auto* clip = dynamic_cast<MidiClip*>(track->getClip(static_cast<std::size_t>(pattern)))) { count += static_cast<int>(clip->notes().size()); }
		}
	}
	return count;
}

//! Put a MIDI file on the grid by the browser's own drag payload.
void dropMidi(SessionGridView* view, int track, int scene, const QString& path)
{
	QToolButton* target = view->cellButton(track, scene);
	QVERIFY(target != nullptr);
	QMimeData* mime = StringPairDrag::createMimeData(QStringLiteral("importedproject"), path);
	QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(target, &enter);
	QVERIFY2(enter.isAccepted(), "the grid refused the browser's own MIDI drag");
	QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(target, &drop);
	delete mime;
	view->refresh();
}

//! The pattern's notes as four numbers each, for an exact round-trip comparison.
std::vector<SavedNote> patternNotes(int pattern)
{
	std::vector<SavedNote> notes;
	for (Track* track : Engine::patternStore()->tracks())
	{
		if (pattern >= track->numOfClips()) { continue; }
		auto* clip = dynamic_cast<MidiClip*>(track->getClip(static_cast<std::size_t>(pattern)));
		if (clip == nullptr) { continue; }
		for (const Note* note : clip->notes())
		{
			notes.push_back(SavedNote{note->pos().getTicks(), note->length().getTicks(), note->key(),
				note->midiVelocity(MidiDefaultVelocity)});
		}
	}
	std::sort(notes.begin(), notes.end());
	return notes;
}

} // namespace

class SessionLibraryMidiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		Engine::init(true);
		ConfigManager::inst()->setWorkingDir(m_home.path());
		ControlRegistry::setReady(true);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
			{QStringLiteral("name"), QStringLiteral("Groove")}}).ok);

		// Two store instruments, so "all notes of the pattern" is measured across tracks: the project's
		// kicker, then a second instrument, both given a clip for pattern 0.
		auto* second = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(second != nullptr);
		Engine::patternStore()->createClipsForPattern(0);
		const auto& tracks = Engine::patternStore()->tracks();
		QVERIFY(tracks.size() >= 2);
		auto* first = dynamic_cast<MidiClip*>(tracks[0]->getClip(0));
		auto* other = dynamic_cast<MidiClip*>(tracks[1]->getClip(0));
		QVERIFY(first != nullptr);
		QVERIFY(other != nullptr);
		first->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		first->addNote(Note(TimePos(48), TimePos(48), 64, 100), false);
		other->addNote(Note(TimePos(96), TimePos(96), 67, 80), false);
		m_expected = patternNotes(0);
		QCOMPARE(m_expected.size(), std::size_t{3});
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Criterion 1: a populated MIDI cell's menu saves the pattern's notes - every track's - as one
	//! Standard MIDI File, and the save is one undo step that redo writes again.
	void aMidiCellSavesEveryPatternNoteFromItsMenu()
	{
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* save = menuAction(&menu, {QStringLiteral("Save to Library...")});
		QVERIFY2(save != nullptr, "a populated MIDI cell's menu has no Save to Library... entry");
		QCOMPARE(save->property("controlCommand").toString(), QStringLiteral("browser.save_clip"));
		QVERIFY(save->isEnabled());

		save->trigger();
		CommandForm* form = visibleForm();
		QVERIFY2(form != nullptr, "Save to Library... opened no form");
		QVERIFY2(form->findChild<QWidget*>(QStringLiteral("fixed:track")) != nullptr, "the cell's track is not fixed in the form");
		QVERIFY2(form->findChild<QWidget*>(QStringLiteral("fixed:scene")) != nullptr, "the cell's scene is not fixed in the form");
		QJsonObject args;
		QString error;
		QVERIFY2(form->collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("track")).toInt(), 0);
		QCOMPARE(args.value(QStringLiteral("scene")).toInt(), 0);
		QCOMPARE(args.value(QStringLiteral("name")).toString(), QStringLiteral("Groove"));
		clickRun(form);

		const QString saved = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/Groove.mid"));
		QVERIFY2(QFileInfo::exists(saved), qPrintable(saved));
		std::vector<SavedNote> read;
		QString why;
		QVERIFY2(notesInFile(saved, &read, &why), qPrintable(why));
		QVERIFY2(read == m_expected, "the saved file does not hold the pattern's notes");

		const ControlResult listed = run(QStringLiteral("browser.query"), {{QStringLiteral("root"), QStringLiteral("samples")},
			{QStringLiteral("text"), QStringLiteral("Groove.mid")}, {QStringLiteral("kind"), QStringLiteral("midi")}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("items")).toArray().size(), 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!QFileInfo::exists(saved), "undo left the MIDI library file behind");
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		std::vector<SavedNote> again;
		QVERIFY2(notesInFile(saved, &again, &why), qPrintable(why));
		QVERIFY2(again == m_expected, "redo wrote a different note set");
	}

	//! Criterion 2: after a save and reopen, the entry can populate ANOTHER cell through the grid's
	//! drop path, as a playable pattern whose notes are the saved file's; saving that cell again gives
	//! the same notes, and its two edits (import, save) are each one undo step.
	void theSavedEntryIsReusedInAnotherCellAfterReopening()
	{
		const QString clips = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips"));
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* save = menuAction(&menu, {QStringLiteral("Save to Library...")});
		QVERIFY(save != nullptr);
		save->trigger();
		CommandForm* form = visibleForm();
		QVERIFY(form != nullptr);
		QVERIFY(form->setField(QStringLiteral("name"), QStringLiteral("groove-saved")));
		clickRun(form);
		const QString saved = QDir(clips).filePath(QStringLiteral("groove-saved.mid"));
		QVERIFY2(QFileInfo::exists(saved), qPrintable(saved));

		const QString project = m_home.filePath(QStringLiteral("midi-roundtrip.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY2(run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray().isEmpty(),
			"a new project inherited the previous grid");
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		const QJsonObject source = slotState(0, 0);
		QCOMPARE(source.value(QStringLiteral("type")).toString(), QStringLiteral("midi"));
		QCOMPARE(source.value(QStringLiteral("name")).toString(), QStringLiteral("Groove"));

		const ControlResult listed = run(QStringLiteral("browser.query"), {{QStringLiteral("root"), QStringLiteral("samples")},
			{QStringLiteral("text"), QStringLiteral("groove-saved.mid")}, {QStringLiteral("kind"), QStringLiteral("midi")}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("items")).toArray().size(), 1);

		view.refresh();
		const int patternsBefore = Engine::patternStore()->numOfPatterns();
		dropMidi(&view, 1, 1, saved);
		const QJsonObject reused = slotState(1, 1);
		QCOMPARE(reused.value(QStringLiteral("type")).toString(), QStringLiteral("midi"));
		QCOMPARE(reused.value(QStringLiteral("name")).toString(), QStringLiteral("groove-saved"));
		const int pattern = reused.value(QStringLiteral("pattern")).toInt();
		QVERIFY(pattern >= 0);
		QCOMPARE(Engine::patternStore()->numOfPatterns(), patternsBefore + 1);
		QVERIFY2(patternNotes(pattern) == m_expected, "the imported pattern does not hold the saved notes");
		QCOMPARE(notesInPattern(pattern), 3);
		PatternTrack* importedTrack = PatternTrack::findPatternTrack(pattern);
		QVERIFY(importedTrack != nullptr);
		QCOMPARE(importedTrack->name(), QStringLiteral("groove-saved"));

		// Save the reused cell again through the same menu: the notes survive the round trip exactly.
		QMenu secondMenu;
		view.populateMenu(&secondMenu, view.cellButton(1, 1));
		QAction* saveAgain = menuAction(&secondMenu, {QStringLiteral("Save to Library...")});
		QVERIFY(saveAgain != nullptr);
		saveAgain->trigger();
		CommandForm* secondForm = visibleForm();
		QVERIFY(secondForm != nullptr);
		QVERIFY(secondForm->setField(QStringLiteral("name"), QStringLiteral("groove-reused")));
		clickRun(secondForm);
		const QString reusedPath = QDir(clips).filePath(QStringLiteral("groove-reused.mid"));
		QVERIFY2(QFileInfo::exists(reusedPath), qPrintable(reusedPath));
		std::vector<SavedNote> reusedNotes;
		QString why;
		QVERIFY2(notesInFile(reusedPath, &reusedNotes, &why), qPrintable(why));
		QVERIFY2(reusedNotes == m_expected, "the re-saved file does not hold the saved notes");

		// One undo for the second save, one for the import: the file goes, then the cell and its pattern.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!QFileInfo::exists(reusedPath), "undo left the second MIDI library file behind");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(slotState(1, 1).isEmpty(), "undoing the import did not clear the target cell");
		QCOMPARE(Engine::patternStore()->numOfPatterns(), patternsBefore);
	}

	/*! The Standard MIDI File writer's own limits, and the command that meets them: a delta time that
	 *  needs more than the four bytes a variable-length quantity has, or a note whose end is past the
	 *  largest tick, is refused with nothing written - not a truncated file. The command form of it:
	 *  a pattern whose notes are too far apart is refused by browser.save_clip, leaves no file and no
	 *  tag, and adds no undo step. */
	void aSaveThatCannotBeWrittenFailsWithoutAFile()
	{
		using SmfNote = midismf::Note;
		QVERIFY(midismf::write({SmfNote{0, 48, 60, 100}, SmfNote{48, 48, 62, 100}}, 120).ok);
		const midismf::Result sparse = midismf::write({SmfNote{0, 1, 60, 100}, SmfNote{0x10000001, 1, 62, 100}}, 120);
		QVERIFY2(!sparse.ok, "a delta of more than four variable-length bytes was written");
		QVERIFY(sparse.bytes.isEmpty());
		QVERIFY2(sparse.error.contains(QStringLiteral("four bytes")), qPrintable(sparse.error));
		const midismf::Result past = midismf::write({SmfNote{std::numeric_limits<tick_t>::max(), 48, 60, 100}}, 120);
		QVERIFY2(!past.ok, "a note ending past the largest tick was written");
		QVERIFY(past.bytes.isEmpty());

		auto* patternTrack = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, Engine::getSong()));
		QVERIFY(patternTrack != nullptr);
		const int pattern = patternTrack->patternIndex();
		Engine::patternStore()->createClipsForPattern(pattern);
		auto* clip = dynamic_cast<MidiClip*>(Engine::patternStore()->tracks()[0]->getClip(static_cast<std::size_t>(pattern)));
		QVERIFY(clip != nullptr);
		clip->addNote(Note(TimePos(1), TimePos(0), 60, 100), false);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), pattern},
			{QStringLiteral("name"), QStringLiteral("too-sparse")}}).ok);
		const QString target = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/too-sparse.mid"));
		// The control: with one note the same cell saves, so the refusal below is the notes' spacing and
		// nothing else about the cell.
		QVERIFY2(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0}}).ok,
			"the one-note control pattern was not saved");
		QVERIFY(QFileInfo::exists(target));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!QFileInfo::exists(target), "undo left the control file behind");
		clip->addNote(Note(TimePos(1), TimePos(0x10000001), 62, 100), false);
		const QJsonArray tagsBefore = run(QStringLiteral("browser.tags")).result.value(QStringLiteral("tags")).toArray();
		const ControlResult refused = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0}});
		QVERIFY2(!refused.ok, "a pattern whose notes cannot fit an SMF was saved");
		QVERIFY2(!QFileInfo::exists(target), "a refused save left a file behind");
		QCOMPARE(run(QStringLiteral("browser.tags")).result.value(QStringLiteral("tags")).toArray(), tagsBefore);
		// The failed save recorded no undo step: undo now takes back the set_slot above, not a save.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(slotState(1, 0).isEmpty());
		control::removeTrack(patternTrack);
	}

	/*! The realtime rule: saving and importing run on the calling (control / UI) thread, as registered
	 *  commands, never on the audio thread. While a MIDI cell is launched, a save and an import are made
	 *  and the engine keeps rendering periods; the audio path's allocations over the same number of
	 *  periods do not grow because of them. */
	void savingAndImportingWhileAMidiCellPlaysLeavesTheAudioPathAlone()
	{
		Engine::audioEngine()->audioDev()->stopProcessing();  // this test drives the periods itself
		const auto restart = qScopeGuard([] { Engine::audioEngine()->audioDev()->startProcessing(); });
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 20; ++i) { period(); }
		const std::uint64_t before = countedPeriods(200);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		const ControlResult saved = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("while-playing")}});
		const std::uint64_t saveAllocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QVERIFY2(saveAllocations > 0, "the probe did not see the save's own allocations");
		const QString file = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/while-playing.mid"));
		const ControlResult imported = run(QStringLiteral("session.import_midi_cell"), {{QStringLiteral("track"), 1},
			{QStringLiteral("scene"), 1}, {QStringLiteral("path"), file}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));

		const std::size_t rendered = m_out.size();
		for (int i = 0; i < 20; ++i) { period(); }
		QVERIFY2(m_out.size() == rendered + 20 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()),
			"the engine stopped rendering after the save and import");
		const std::uint64_t after = countedPeriods(200);
		QVERIFY2(after <= before, qPrintable(QStringLiteral("the audio path allocated %1 times after the save and import; "
			"%2 before").arg(after).arg(before)));
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the import
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the save
	}

private:
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		engine->pushInputFrames(block.data(), fpp);
		const std::span<const SampleFrame> out = engine->renderNextPeriod();
		m_out.insert(m_out.end(), out.begin(), out.end());
	}

	//! The allocations the audio path makes over @a periods rendered periods.
	std::uint64_t countedPeriods(int periods)
	{
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < periods; ++i) { period(); }
		const std::uint64_t count = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		return count;
	}

	std::vector<SampleFrame> m_out;
	QTemporaryDir m_home;
	std::vector<SavedNote> m_expected;
};

QTEST_MAIN(SessionLibraryMidiTest)
#include "SessionLibraryMidiTest.moc"
