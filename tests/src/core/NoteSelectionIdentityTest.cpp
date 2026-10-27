/*
 * NoteSelectionIdentityTest.cpp - the control selection store keeps the SELECTED NOTES'
 *                               identities, so a removal cannot shift it onto another note.
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
 * License along with this program (if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/*! The control selection store (control::selectNotes) used to keep the selected notes as
 *  POSITIONS in the clip's note list. A position names whichever note slides into it, so any
 *  removal that shortens the list moved the selection onto a note the client never selected:
 *  select the middle note of three, remove the first one, and the selection named the LAST
 *  note. note.remove remapped its own shift, but every other removal path (note.remove_many,
 *  note.slice, the clipboard verbs) did not, so the defect was reachable from the plain
 *  command surface.
 *
 *  The store keeps the selected notes' ids now (Note::id(), SPEC-stable-ids.md slice 2), which
 *  travel with the note objects: a removal leaves every surviving selected note selected, and
 *  the removed one is simply no longer in the clip.
 *
 *  The removal path through note.remove is covered by
 *  tests/src/core/ControlEditCommandsTest.cpp (aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote)
 *  and the re-sort through note.move by aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

class NoteSelectionIdentityTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! The same defect as aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote,
	 *  reached through a removal that does not hand-remap the store: note.remove
	 *  fixes its own shift (src/core/ControlCommandsNotes.cpp), but note.remove_many
	 *  just deletes the notes it was given, so the stored position still names
	 *  whichever note slid into it. Selecting the middle note of three and
	 *  removing the first leaves the selection naming the LAST note - a note the
	 *  client never selected.
	 */
	void removingAnEarlierNoteManyTimesKeepsTheSelectionOnTheSameNote()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const ControlResult addedClip = registry->invoke(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), addedTrack.result.value(QStringLiteral("track")).toString()},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}});
		QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
		const QString clip = addedClip.result.value(QStringLiteral("clip")).toString();

		QStringList ids;
		for (int i = 0; i < 3; ++i)
		{
			const ControlResult added = registry->invoke(QStringLiteral("note.add"),
				QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60 + i},
					{QStringLiteral("position"), i * 24}, {QStringLiteral("length"), 12}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			ids.append(added.result.value(QStringLiteral("note")).toString());
		}

		// Select the MIDDLE note.
		const ControlResult selected = registry->invoke(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), QJsonArray{ids.at(1)}}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QCOMPARE(selected.result.value(QStringLiteral("selected_notes")).toArray().first().toString(),
			ids.at(1));

		// Remove the FIRST note. The selected note keeps its id; only its position moves.
		QVERIFY2(registry->invoke(QStringLiteral("note.remove_many"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), QJsonArray{ids.at(0)}}}).ok,
			"the first note did not remove");

		const ControlResult rolled = registry->invoke(QStringLiteral("roll.get_state"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		QCOMPARE(rolled.result.value(QStringLiteral("notes")).toArray().size(), 2);
		const QJsonArray selectedNotes = rolled.result.value(QStringLiteral("selected_notes")).toArray();
		QCOMPARE(selectedNotes.size(), 1);
		QVERIFY2(selectedNotes.first().toString() == ids.at(1),
			qPrintable(QStringLiteral("after removing the first note the selection reported "
				"note \"%1\", but the note that WAS selected is \"%2\" - the selection landed on "
				"a note the client never selected")
				.arg(selectedNotes.first().toString(), ids.at(1))));
	}

	//! Luna cluster new-p15-14: the deterministic position roll reverses the two
	//! notes' list order, but roll.get_state must keep reporting the selected note id.
	void randomizeReorderingNotesKeepsTheSelectionOnItsIdentity()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const ControlResult addedClip = registry->invoke(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), addedTrack.result.value(QStringLiteral("track")).toString()},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}});
		QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
		const QString clip = addedClip.result.value(QStringLiteral("clip")).toString();

		const ControlResult first = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 24}, {QStringLiteral("length"), 24}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		const ControlResult second = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 62},
				{QStringLiteral("position"), 48}, {QStringLiteral("length"), 24}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		const QString firstId = first.result.value(QStringLiteral("note")).toString();
		const QString secondId = second.result.value(QStringLiteral("note")).toString();
		QVERIFY2(registry->invoke(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), QJsonArray{firstId}}}).ok,
			"could not select the first note");

		const ControlResult rolled = registry->invoke(QStringLiteral("note.randomize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("velocity_jitter"), 0.0},
				{QStringLiteral("position_jitter"), 48}, {QStringLiteral("seed"), 2}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		const ControlResult state = registry->invoke(QStringLiteral("roll.get_state"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		const QJsonArray notes = state.result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 2);
		QCOMPARE(notes.at(0).toObject().value(QStringLiteral("id")).toString(), secondId);
		QCOMPARE(notes.at(1).toObject().value(QStringLiteral("id")).toString(), firstId);
		QVERIFY(!notes.at(0).toObject().value(QStringLiteral("selected")).toBool());
		QVERIFY(notes.at(1).toObject().value(QStringLiteral("selected")).toBool());
		const QJsonArray selected = state.result.value(QStringLiteral("selected_notes")).toArray();
		QCOMPARE(selected.size(), 1);
		QCOMPARE(selected.first().toString(), firstId);
	}
};

QTEST_MAIN(NoteSelectionIdentityTest)
#include "NoteSelectionIdentityTest.moc"
