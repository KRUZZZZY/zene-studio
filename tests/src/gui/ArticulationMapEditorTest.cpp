/*
 * ArticulationMapEditorTest.cpp - Track > Articulations builds the map track.articulations_set builds
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

/*! Q050-015: the articulation map editor and the headless command leave the same map; every edit in it is one
 *  undo step; moving and removing entries take the notes with them; a refused edit changes nothing and says why. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QTableWidget>
#include <QTemporaryDir>

#include "ArticulationMapEditor.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteConditions.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject item(const QString& name, int keyswitch = -1, int program = -1, int controller = -1, int value = 0)
{
	QJsonObject out{{QStringLiteral("name"), name}};
	if (keyswitch >= 0) { out.insert(QStringLiteral("keyswitch"), keyswitch); }
	if (program >= 0) { out.insert(QStringLiteral("program"), program); }
	if (controller >= 0) { out.insert(QStringLiteral("controller"), controller); out.insert(QStringLiteral("value"), value); }
	return out;
}

} // namespace

class ArticulationMapEditorTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* newTrack()
	{
		return dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	}

	QJsonArray mapOf(const InstrumentTrack* track) { return track->articulations().toJson(); }

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void init() { Engine::getSong()->clearProject(); }

	//! The acceptance: the editor creates, renames, reorders and removes entries and fills in their MIDI, and the
	//! map it leaves is what one track.articulations_set with the same list leaves on another track.
	void theEditorAndTheCommandLeaveTheSameMap()
	{
		InstrumentTrack* viaEditor = newTrack();
		InstrumentTrack* viaCommand = newTrack();
		ArticulationMapEditor editor(control::trackIdOf(viaEditor));
		QCOMPARE(editor.rowCount(), 0);
		QVERIFY(editor.addEntry(QStringLiteral("Legato")));
		QVERIFY(editor.addEntry(QStringLiteral("Staccato")));
		QVERIFY(editor.addEntry());  // "Articulation 3"
		QCOMPARE(editor.rowCount(), 3);
		QVERIFY(editor.renameEntry(2, QStringLiteral("Pizzicato")));
		QVERIFY(editor.setNumber(0, ArticulationMapEditor::Field::Keyswitch, 24));
		QVERIFY(editor.setNumber(1, ArticulationMapEditor::Field::Program, 45));
		QVERIFY(editor.setNumber(2, ArticulationMapEditor::Field::Controller, 32));
		QVERIFY(editor.setNumber(2, ArticulationMapEditor::Field::Value, 127));
		QVERIFY(editor.moveEntry(2, -2));  // Pizzicato first
		QVERIFY(editor.duplicateEntry(0));  // "Pizzicato copy" second
		QVERIFY(editor.removeEntry(1));     // ... and gone again
		QVERIFY(editor.addEntry(QStringLiteral("Tremolo")));
		QVERIFY(editor.setNumber(3, ArticulationMapEditor::Field::Keyswitch, 30));
		QVERIFY(editor.setNumber(3, ArticulationMapEditor::Field::Keyswitch, -1));  // and blank again

		const QJsonArray wanted{item(QStringLiteral("Pizzicato"), -1, -1, 32, 127), item(QStringLiteral("Legato"), 24),
			item(QStringLiteral("Staccato"), -1, 45), item(QStringLiteral("Tremolo"))};
		QCOMPARE(mapOf(viaEditor), wanted);
		QCOMPARE(editor.rowCount(), 4);
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), control::trackIdOf(viaCommand)},
			{QStringLiteral("articulations"), wanted}}).ok);
		QCOMPARE(mapOf(viaCommand), mapOf(viaEditor));
		// The table shows what track.articulations answers.
		auto* table = editor.findChild<QTableWidget*>();
		QVERIFY(table != nullptr);
		QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Pizzicato"));
		QCOMPARE(table->item(0, 4)->text(), QStringLiteral("32"));
		QCOMPARE(table->item(0, 5)->text(), QStringLiteral("127"));
		QCOMPARE(table->item(1, 2)->text(), QStringLiteral("24"));
		QCOMPARE(table->item(2, 3)->text(), QStringLiteral("45"));
		QCOMPARE(table->item(3, 2)->text(), QString());
	}

	//! Typing into the table is the same edit as the methods: a name, a number, a blank.
	void editingACellIsTheSameEditAsTheMethod()
	{
		InstrumentTrack* track = newTrack();
		ArticulationMapEditor editor(control::trackIdOf(track));
		QVERIFY(editor.addEntry(QStringLiteral("Arco")));
		auto* table = editor.findChild<QTableWidget*>();
		table->item(0, 1)->setText(QStringLiteral("Arco con sordino"));
		table->item(0, 2)->setText(QStringLiteral("26"));
		table->item(0, 4)->setText(QStringLiteral("64"));
		table->item(0, 5)->setText(QStringLiteral("100"));
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("Arco con sordino"), 26, -1, 64, 100)}));
		table->item(0, 2)->setText(QString());
		table->item(0, 4)->setText(QStringLiteral("-"));
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("Arco con sordino"))}));
		table->item(0, 3)->setText(QStringLiteral("loud"));  // not a number: refused, the table shows the map again
		QVERIFY(!editor.message().isEmpty());
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("Arco con sordino"))}));
		QCOMPARE(editor.findChild<QTableWidget*>()->item(0, 3)->text(), QString());
	}

	//! Each edit is one undo step, and the editor shows the map again after it.
	void everyEditIsOneUndoStep()
	{
		InstrumentTrack* track = newTrack();
		ArticulationMapEditor editor(control::trackIdOf(track));
		QVERIFY(editor.addEntry(QStringLiteral("A")));
		QVERIFY(editor.addEntry(QStringLiteral("B")));
		QVERIFY(editor.moveEntry(1, -1));
		QVERIFY(editor.setNumber(0, ArticulationMapEditor::Field::Keyswitch, 12));
		QVERIFY(editor.removeEntry(1));
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("B"), 12)}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the removal
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("B"), 12), item(QStringLiteral("A"))}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the keyswitch
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("B")), item(QStringLiteral("A"))}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the move
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("A")), item(QStringLiteral("B"))}));
		editor.refresh();
		QCOMPARE(editor.rowCount(), 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(track->articulations().empty());
		editor.refresh();
		QCOMPARE(editor.rowCount(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(mapOf(track), (QJsonArray{item(QStringLiteral("A"))}));
	}

	//! Notes follow their articulation through a move and a removal made in the editor, and undo gives them back.
	void notesFollowTheirArticulation()
	{
		InstrumentTrack* track = newTrack();
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QList<Note*> notes;
		for (int i = 0; i < 3; ++i) { notes << clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
		ArticulationMapEditor editor(control::trackIdOf(track));
		for (const char* name : {"One", "Two", "Three"}) { QVERIFY(editor.addEntry(QString::fromLatin1(name))); }
		for (int i = 0; i < 3; ++i)
		{
			NoteConditions conditions = notes[i]->conditions();
			conditions.articulation = i + 1;
			notes[i]->setConditions(conditions);
		}
		const auto asked = [&notes] { return QList<int>{notes[0]->conditions().articulation, notes[1]->conditions().articulation,
			notes[2]->conditions().articulation}; };
		QVERIFY(editor.moveEntry(0, 2));  // Two, Three, One
		QCOMPARE(asked(), (QList<int>{3, 1, 2}));
		QVERIFY(editor.removeEntry(0));   // Three, One: the note that asked for "Two" asks for none
		QCOMPARE(asked(), (QList<int>{2, 0, 1}));
		QVERIFY(editor.renameEntry(0, QStringLiteral("Third")));  // a rename moves no note
		QCOMPARE(asked(), (QList<int>{2, 0, 1}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(asked(), (QList<int>{3, 1, 2}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(asked(), (QList<int>{1, 2, 3}));
	}

	//! A refused edit changes nothing and says why.
	void aRefusedEditChangesNothing()
	{
		InstrumentTrack* track = newTrack();
		ArticulationMapEditor editor(control::trackIdOf(track));
		QVERIFY(editor.addEntry(QStringLiteral("Legato")));
		QVERIFY(editor.addEntry(QStringLiteral("Staccato")));
		const QJsonArray before = mapOf(track);
		QVERIFY(!editor.addEntry(QStringLiteral("legato")));  // names are told apart by letters, not case
		QVERIFY(!editor.message().isEmpty());
		QVERIFY(!editor.renameEntry(1, QStringLiteral("LEGATO")));
		QVERIFY(!editor.renameEntry(1, QStringLiteral("   ")));
		QVERIFY(!editor.setNumber(0, ArticulationMapEditor::Field::Keyswitch, 200));
		QVERIFY(!editor.setNumber(0, ArticulationMapEditor::Field::Value, 5));  // no controller yet
		QVERIFY(!editor.moveEntry(0, -1));
		QVERIFY(!editor.moveEntry(1, 1));
		QVERIFY(!editor.removeEntry(7));
		QCOMPARE(mapOf(track), before);
		QVERIFY(editor.renameEntry(1, QStringLiteral("Staccato")));  // its own name is no clash
		QVERIFY(editor.message().isEmpty());
		// Sixty-four is the most a note can name.
		for (int i = 2; i < NoteConditions::MaxArticulations; ++i) { QVERIFY(editor.addEntry()); }
		QCOMPARE(editor.rowCount(), NoteConditions::MaxArticulations);
		QVERIFY(!editor.addEntry());
		QVERIFY(!editor.duplicateEntry(0));
	}

	//! Saved with the project, through the real project file: edit in the editor, project.save, clear the session,
	//! project.open - the same map comes back in the same order, the note keeps its articulation, and an editor
	//! opened on the reloaded track shows it.
	void theEditedMapSavesWithTheProject()
	{
		InstrumentTrack* track = newTrack();
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		Note* note = clip->addNote(Note(TimePos(24), TimePos(0), 60), false);
		const QString trackId = control::trackIdOf(track);
		QJsonArray saved;
		{
			ArticulationMapEditor editor(trackId);
			QVERIFY(editor.addEntry(QStringLiteral("Legato")));
			QVERIFY(editor.addEntry(QStringLiteral("Staccato")));
			QVERIFY(editor.setNumber(1, ArticulationMapEditor::Field::Controller, 1));
			QVERIFY(editor.setNumber(1, ArticulationMapEditor::Field::Value, 90));
			QVERIFY(editor.setNumber(1, ArticulationMapEditor::Field::Keyswitch, 24));
			NoteConditions conditions = note->conditions();
			conditions.articulation = 2;  // Staccato
			note->setConditions(conditions);
			QVERIFY(editor.moveEntry(1, -1));  // Staccato first: the note follows it
			QCOMPARE(note->conditions().articulation, 1);
			saved = mapOf(track);
		}
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = dir.filePath(QStringLiteral("articulation-editor.mmp"));
		const ControlResult wrote = run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}});
		QVERIFY2(wrote.ok, qPrintable(wrote.errorMessage));
		Engine::getSong()->clearProject();
		QCOMPARE(Engine::getSong()->tracks().size(), 0);
		Engine::getSong()->setLoadOnLaunch(false);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		InstrumentTrack* reloaded = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (auto* instrument = dynamic_cast<InstrumentTrack*>(candidate)) { reloaded = instrument; }
		}
		QVERIFY2(reloaded != nullptr, "the reopened project has no instrument track");
		QCOMPARE(mapOf(reloaded), saved);
		auto* reloadedClip = dynamic_cast<MidiClip*>(reloaded->getClips().front());
		QCOMPARE(reloadedClip->notes().front()->conditions().articulation, 1);
		ArticulationMapEditor editor(control::trackIdOf(reloaded));
		QCOMPARE(editor.rowCount(), 2);
		QCOMPARE(editor.entries().front().name, QStringLiteral("Staccato"));
		QCOMPARE(editor.entries().front().value, 90);
		QCOMPARE(editor.entries().front().keyswitch, 24);
	}
};

QTEST_MAIN(ArticulationMapEditorTest)
#include "ArticulationMapEditorTest.moc"
