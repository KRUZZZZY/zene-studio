/*
 * NoteMelodyTest.cpp - note.generate_melody: seeded, in the active scale, one undo step, from the note editor
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
/*! 0.5.0 Q050-221.1: the same seed on the same starting clip writes the same notes and another seed
 *  another line; every key is in the active root and scale (a built-in and a custom one); a
 *  generation is one undo step; and Notes > Tools > Generate a Melody... runs the same command. */

#include <QtTest>

#include <QApplication>
#include <QJsonArray>
#include <QPushButton>
#include <QToolButton>
#include <algorithm>
#include <functional>
#include <tuple>
#include <vector>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

using NoteRow = std::tuple<int, int, int, int>;  // position, key, length, velocity

std::vector<NoteRow> rowsOf(const MidiClip* clip)
{
	std::vector<NoteRow> out;
	for (const Note* note : clip->notes())
	{
		out.emplace_back(note->pos().getTicks(), note->key(), note->length().getTicks(), static_cast<int>(note->getVolume()));
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<int> keysInTimeOrder(const MidiClip* clip)
{
	std::vector<int> out;
	for (const NoteRow& row : rowsOf(clip)) { out.push_back(std::get<1>(row)); }
	return out;
}

bool allIn(const std::vector<int>& keys, std::initializer_list<int> classes)
{
	return std::all_of(keys.begin(), keys.end(), [classes](int key) {
		return std::find(classes.begin(), classes.end(), key % 12) != classes.end();
	});
}

} // namespace

class NoteMelodyTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;

	MidiClip* emptyClip() { return dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0))); }

	//! A clip of eight C5 eighths - the same starting clip every time it is asked for.
	MidiClip* rhythmClip()
	{
		MidiClip* clip = emptyClip();
		for (int i = 0; i < 8; ++i) { clip->addNote(Note(TimePos(12), TimePos(i * 24), 60), false); }
		clip->rearrangeAllNotes();
		return clip;
	}

	QJsonObject melody(const MidiClip* clip, std::initializer_list<std::pair<QString, QJsonValue>> more) const
	{
		QJsonObject out{{QStringLiteral("clip"), control::clipIdOf(clip)}};
		for (const auto& [key, value] : more) { out.insert(key, value); }
		return out;
	}

	void setScale(const QString& root, const QString& scale)
	{
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), root}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), scale}}).ok);
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

	void cleanup() { QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok); }

	//! Acceptance 1: same seed + same starting clip = the same notes; another seed changes the pitch line - for every contour.
	void theSameSeedRepeatsAndAnotherSeedChangesTheLine()
	{
		setScale(QStringLiteral("C"), QStringLiteral("Major"));
		for (const char* contour : {"walk", "rise", "fall", "arch", "valley"})
		{
			MidiClip* first = emptyClip();
			MidiClip* again = emptyClip();
			MidiClip* other = emptyClip();
			const auto args = [this, contour](const MidiClip* clip, int seed) {
				return melody(clip, {{QStringLiteral("contour"), QLatin1String(contour)}, {QStringLiteral("seed"), seed}});
			};
			const ControlResult made = run(QStringLiteral("note.generate_melody"), args(first, 11));
			QVERIFY2(made.ok, qPrintable(made.errorMessage));
			QCOMPARE(made.result.value(QStringLiteral("added")).toInt(), 16);
			QVERIFY(run(QStringLiteral("note.generate_melody"), args(again, 11)).ok);
			QVERIFY(run(QStringLiteral("note.generate_melody"), args(other, 12)).ok);
			QCOMPARE(static_cast<int>(first->notes().size()), 16);
			QVERIFY2(rowsOf(first) == rowsOf(again), contour);
			QVERIFY2(keysInTimeOrder(first) != keysInTimeOrder(other), contour);
			const std::vector<int> keys = keysInTimeOrder(first);
			QVERIFY2(std::adjacent_find(keys.begin(), keys.end(), std::not_equal_to<int>()) != keys.end(), "the line is not one key");
			delete first;
			delete again;
			delete other;
		}
		// Re-pitching keeps the rhythm and is just as repeatable.
		MidiClip* first = rhythmClip();
		MidiClip* again = rhythmClip();
		MidiClip* other = rhythmClip();
		for (auto [clip, seed] : {std::pair{first, 5}, std::pair{again, 5}, std::pair{other, 6}})
		{
			QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("target"), QStringLiteral("existing")},
				{QStringLiteral("seed"), seed}})).ok);
		}
		QVERIFY(rowsOf(first) == rowsOf(again));
		QVERIFY(keysInTimeOrder(first) != keysInTimeOrder(other));
		for (int i = 0; i < 8; ++i) { QCOMPARE(std::get<0>(rowsOf(first)[static_cast<std::size_t>(i)]), i * 24); }
		delete first;
		delete again;
		delete other;
	}

	//! The draw is the same on every standard library: one line pinned exactly.
	void aSeedsLineIsPinned()
	{
		setScale(QStringLiteral("C"), QStringLiteral("Major"));
		MidiClip* clip = emptyClip();
		QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("seed"), 42}, {QStringLiteral("count"), 8}})).ok);
		QCOMPARE(keysInTimeOrder(clip), PINNED_LINE);
		delete clip;
	}

	//! With no seed named, the project's seed (note.random_seed_set) is the seed.
	void theProjectSeedIsTheDefault()
	{
		setScale(QStringLiteral("A"), QStringLiteral("Minor"));
		QVERIFY(run(QStringLiteral("note.random_seed_set"), {{QStringLiteral("seed"), 77}}).ok);
		MidiClip* named = emptyClip();
		MidiClip* project = emptyClip();
		QVERIFY(run(QStringLiteral("note.generate_melody"), melody(named, {{QStringLiteral("seed"), 77}})).ok);
		const ControlResult made = run(QStringLiteral("note.generate_melody"), melody(project, {}));
		QVERIFY(made.ok);
		QCOMPARE(made.result.value(QStringLiteral("seed")).toInt(), 77);
		QVERIFY(rowsOf(named) == rowsOf(project));
		QVERIFY(run(QStringLiteral("note.random_seed_set"), {{QStringLiteral("seed"), 0}}).ok);
		delete named;
		delete project;
	}

	//! Acceptance 2: every key is in the active root and scale - a built-in one and a custom one - and in low..high.
	void everyKeyIsInTheActiveScale()
	{
		setScale(QStringLiteral("D"), QStringLiteral("Major"));  // D E F# G A B C#
		for (int seed = 0; seed < 20; ++seed)
		{
			MidiClip* clip = emptyClip();
			QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("seed"), seed},
				{QStringLiteral("spread"), 5}, {QStringLiteral("low"), 40}, {QStringLiteral("high"), 80}})).ok);
			const std::vector<int> keys = keysInTimeOrder(clip);
			QVERIFY(allIn(keys, {2, 4, 6, 7, 9, 11, 1}));
			QVERIFY(*std::min_element(keys.begin(), keys.end()) >= 40 && *std::max_element(keys.begin(), keys.end()) <= 80);
			delete clip;
		}
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Melody Mine")},
			{QStringLiteral("degrees"), QJsonArray{0, 3, 5, 10}}}).ok);
		setScale(QStringLiteral("E"), QStringLiteral("Melody Mine"));  // E G A D
		for (const char* contour : {"walk", "arch"})
		{
			MidiClip* clip = emptyClip();
			QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("contour"), QLatin1String(contour)},
				{QStringLiteral("seed"), 3}, {QStringLiteral("count"), 64}})).ok);
			QVERIFY2(allIn(keysInTimeOrder(clip), {4, 7, 9, 2}), contour);
			delete clip;
		}
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Melody Mine")}}).ok);
	}

	//! Acceptance 2: one undo step; refusals take none.
	void aGenerationIsOneUndoStep()
	{
		ProjectJournal* journal = Engine::projectJournal();
		MidiClip* clip = rhythmClip();
		const std::vector<NoteRow> before = rowsOf(clip);
		const int unscaled = journal->undoDepth();
		QCOMPARE(run(QStringLiteral("note.generate_melody"), melody(clip, {})).errorKind, ControlErrorKind::Refused);  // no scale
		QCOMPARE(journal->undoDepth(), unscaled);
		setScale(QStringLiteral("G"), QStringLiteral("Minor"));  // scale.root_set / scale.set take their own undo steps
		const int depth = journal->undoDepth();
		QCOMPARE(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("low"), 60},
			{QStringLiteral("high"), 61}})).errorKind, ControlErrorKind::InvalidArgs);  // no two keys of G minor in 60..61
		QCOMPARE(journal->undoDepth(), depth);
		QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("target"), QStringLiteral("existing")}})).ok);
		QCOMPARE(journal->undoDepth(), depth + 1);
		QVERIFY(rowsOf(clip) != before);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(rowsOf(clip) == before);
		QVERIFY(run(QStringLiteral("note.generate_melody"), melody(clip, {{QStringLiteral("seed"), 9}})).ok);
		QCOMPARE(static_cast<int>(clip->notes().size()), 24);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(rowsOf(clip) == before);
		delete clip;
	}

	//! The GUI: Notes > Tools > Generate a Melody... opens the command's form; Run writes the line; undo takes it back.
	void theNoteEditorMenuRunsTheSameCommand()
	{
		setScale(QStringLiteral("F"), QStringLiteral("Major"));  // F G A Bb C D E
		MidiClip* clip = emptyClip();
		MidiClip* reference = emptyClip();
		QVERIFY(run(QStringLiteral("note.generate_melody"), melody(reference, {{QStringLiteral("seed"), 21}, {QStringLiteral("count"), 12}})).ok);
		auto* editor = new NoteEditor;
		editor->resize(1000, 600);
		editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(editor));
		editor->setClip(clip);
		auto* button = editor->findChild<QToolButton*>(QStringLiteral("noteEditorNotesMenu"));
		QVERIFY(button != nullptr && button->menu() != nullptr);
		emit button->menu()->aboutToShow();
		QAction* action = test::menuAction(button->menu(), {QStringLiteral("Tools"), QStringLiteral("Generate a Melody...")});
		QVERIFY(action != nullptr);
		QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("note.generate_melody"));
		action->trigger();
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = findForm()) != nullptr, 3000);
		QVERIFY(form->setField(QStringLiteral("seed"), 21));
		QVERIFY(form->setField(QStringLiteral("count"), 12));
		for (QPushButton* push : form->findChildren<QPushButton*>())
		{
			if (push->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { QTest::mouseClick(push, Qt::LeftButton); }
		}
		QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(clip->notes().size()), 12, 3000);
		QVERIFY(rowsOf(clip) == rowsOf(reference));
		QVERIFY(allIn(keysInTimeOrder(clip), {5, 7, 9, 10, 0, 2, 4}));
		form->close();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(clip->notes().size()), 0);
		delete editor;
		delete clip;
		delete reference;
	}

private:
	static CommandForm* findForm()
	{
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* form = qobject_cast<CommandForm*>(widget); form != nullptr && form->isVisible()) { return form; }
		}
		return nullptr;
	}

	//! seed 42, walk, C major over 48..72: recorded once; std::mt19937 is specified to the bit, the mapping is our own.
	const std::vector<int> PINNED_LINE{59, 62, 64, 67, 64, 62, 60, 62};
};

QTEST_MAIN(NoteMelodyTest)
#include "NoteMelodyTest.moc"
