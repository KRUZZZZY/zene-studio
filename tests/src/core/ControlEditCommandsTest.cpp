/*
 * ControlEditCommandsTest.cpp - unit tests for the notes / clips / tracks command
 *                               group (SPEC-zene-studio.md A11-A16).
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
 */

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlEdit.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "Note.h"

using namespace lmms;

//! The track's own mute flag, as track.get_state reports it.
static bool muted(const QString& track)
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("track.get_state"),
		QJsonObject{{QStringLiteral("track"), track}});
	return state.ok && state.result.value(QStringLiteral("muted")).toBool();
}

class ControlEditCommandsTest : public QObject
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

	//! Every command of the group is registered with the contract's parts: a
	//! group.verb id, both schemas and a requires declaration (empty = headless).
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList required = {
			QStringLiteral("note.add"), QStringLiteral("note.remove"), QStringLiteral("note.move"),
			QStringLiteral("note.resize"), QStringLiteral("note.velocity_set"),
			QStringLiteral("note.select"), QStringLiteral("roll.get_state"),
			QStringLiteral("clip.add"), QStringLiteral("clip.move"), QStringLiteral("clip.resize"),
			QStringLiteral("clip.split"), QStringLiteral("clip.delete"),
			QStringLiteral("clip.duplicate"), QStringLiteral("clip.select"),
			QStringLiteral("track.add"), QStringLiteral("track.remove"),
			QStringLiteral("track.rename"), QStringLiteral("track.set_mute"),
			QStringLiteral("track.set_solo"), QStringLiteral("track.set_arm"),
			QStringLiteral("arrangement.get_state"),
		};
		for (const QString& id : required)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QVERIFY(!cmd->group.isEmpty());
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			// SPEC A13 headless parity: the declaration exists and is empty.
			QVERIFY2(cmd->requiresDecl.isEmpty(),
				qPrintable(QStringLiteral("%1 declares a requirement").arg(id)));
		}
	}

	//! Out-of-range note values are rejected by the schema, before any handler.
	void outOfRangeNoteValuesAreInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject keyArgs;
		keyArgs.insert(QStringLiteral("clip"), QStringLiteral("clip-0"));
		keyArgs.insert(QStringLiteral("key"), 300);
		keyArgs.insert(QStringLiteral("position"), 0);
		keyArgs.insert(QStringLiteral("length"), 12);
		const ControlResult keyResult = registry->invoke(QStringLiteral("note.add"), keyArgs);
		QVERIFY(!keyResult.ok);
		QCOMPARE(keyResult.errorKind, ControlErrorKind::InvalidArgs);

		QJsonObject velocityArgs;
		velocityArgs.insert(QStringLiteral("clip"), QStringLiteral("clip-0"));
		velocityArgs.insert(QStringLiteral("note"), QStringLiteral("note-0"));
		velocityArgs.insert(QStringLiteral("velocity"), 9999);
		const ControlResult velocityResult =
			registry->invoke(QStringLiteral("note.velocity_set"), velocityArgs);
		QVERIFY(!velocityResult.ok);
		QCOMPARE(velocityResult.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! note.move's schema bounds the new START (0..MaxSongLength), but the note's
	//! END must fit inside the song too: a positive-length note moved to MaxSongLength
	//! would end past the limit. The move is refused, typed, with nothing changed.
	void noteMovePastSongLimitIsRefused()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const QString track = addedTrack.result.value(QStringLiteral("track")).toString();

		const ControlResult addedClip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
		const QString clip = addedClip.result.value(QStringLiteral("clip")).toString();

		const ControlResult addedNote = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(addedNote.ok, qPrintable(addedNote.errorMessage));
		const QString noteId = addedNote.result.value(QStringLiteral("note")).toString();

		// The schema accepts the start (it is exactly MaxSongLength), but the note's
		// end would sit 24 ticks past the limit: the handler must refuse the move.
		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), noteId},
				{QStringLiteral("position"), MaxSongLength}});
		QVERIFY2(!moved.ok, "a move ending past the song limit was applied");
		QCOMPARE(moved.errorKind, ControlErrorKind::InvalidArgs);

		// Nothing moved: the note still starts at 0.
		const ControlResult rolled = registry->invoke(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		const QJsonArray notes = rolled.result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 1);
		QCOMPARE(notes.first().toObject().value(QStringLiteral("position")).toInt(), 0);
	}

	//! The end guard must survive lengths the schema never sees: Note::loadSettings
	//! reads a note's length straight from XML without a MaxSongLength clamp, so a
	//! loaded/model note can carry a positive length near INT_MAX. Adding that to a
	//! MaxSongLength start in 32-bit wraps negative and slips past an int32 check;
	//! the handler compares in a wide type, so the move is refused and the note is
	//! untouched.
	void noteMoveWithHugeLoadedLengthIsRefused()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const QString track = addedTrack.result.value(QStringLiteral("track")).toString();

		const ControlResult addedClip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
		const QString clip = addedClip.result.value(QStringLiteral("clip")).toString();

		const ControlResult addedNote = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(addedNote.ok, qPrintable(addedNote.errorMessage));
		const QString noteId = addedNote.result.value(QStringLiteral("note")).toString();

		// What Note::loadSettings would produce: a positive length near INT_MAX, far
		// past MaxSongLength, set straight on the model.
		control::ClipRef ref;
		ControlResult error;
		MidiClip* clipObj = control::resolveMidiClip(clip, &ref, &error);
		QVERIFY2(clipObj != nullptr, qPrintable(error.errorMessage));
		int index = -1;
		Note* modelNote = control::resolveNote(clipObj, noteId, &index, &error);
		QVERIFY2(modelNote != nullptr, qPrintable(error.errorMessage));
		const tick_t hugeLength = 2147483000; // INT_MAX-ish
		modelNote->setLength(TimePos(hugeLength));

		// MaxSongLength + hugeLength overflows int32 to a negative value; a wide-typed
		// guard still sees the end past the limit and refuses.
		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), noteId},
				{QStringLiteral("position"), MaxSongLength}});
		QVERIFY2(!moved.ok, "a move ending past the song limit was applied");
		QCOMPARE(moved.errorKind, ControlErrorKind::InvalidArgs);

		// Nothing changed: the note still starts at 0 with its huge length.
		QCOMPARE(modelNote->pos().getTicks(), 0);
		QCOMPARE(modelNote->length().getTicks(), hugeLength);
	}

	void selectionSaveRejectsNonStringClipIds()
	{
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("selection.save"),
			QJsonObject{{QStringLiteral("name"), QStringLiteral("malformed")},
				{QStringLiteral("clips"), QJsonArray{17}}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! A malformed id and an absent id are different, typed failures.
	void malformedAndAbsentIdsAreTyped()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult malformed = registry->invoke(QStringLiteral("track.rename"),
			QJsonObject{{QStringLiteral("track"), QStringLiteral("track-one")},
				{QStringLiteral("name"), QStringLiteral("x")}});
		QVERIFY(!malformed.ok);
		QCOMPARE(malformed.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult absent = registry->invoke(QStringLiteral("track.rename"),
			QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-9999")},
				{QStringLiteral("name"), QStringLiteral("x")}});
		QVERIFY(!absent.ok);
		QCOMPARE(absent.errorKind, ControlErrorKind::NotFound);

		const ControlResult noClip = registry->invoke(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), QStringLiteral("clip-9999")},
				{QStringLiteral("position"), 0}});
		QVERIFY(!noClip.ok);
		QCOMPARE(noClip.errorKind, ControlErrorKind::NotFound);

		// A malformed clip id is invalid_args whatever the song holds.
		const ControlResult badClip = registry->invoke(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), QStringLiteral("clip-x")},
				{QStringLiteral("position"), 0}});
		QVERIFY(!badClip.ok);
		QCOMPARE(badClip.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! The whole editing flow, in process: add a track, a clip, notes, edit one,
	//! read it back, delete it and undo. The engine paths are the real ones.
	void editingFlowAddsEditsAndUndoes()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const QString track = addedTrack.result.value(QStringLiteral("track")).toString();
		QVERIFY(track.startsWith(QStringLiteral("trk-")));

		const ControlResult addedClip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(addedClip.ok, qPrintable(addedClip.errorMessage));
		const QString clip = addedClip.result.value(QStringLiteral("clip")).toString();
		QVERIFY(clip.startsWith(QStringLiteral("clip-")));

		QJsonObject firstNote{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}};
		const ControlResult addedNote = registry->invoke(QStringLiteral("note.add"), firstNote);
		QVERIFY2(addedNote.ok, qPrintable(addedNote.errorMessage));
		const QString noteId = addedNote.result.value(QStringLiteral("note")).toString();
		QVERIFY(noteId.startsWith(QStringLiteral("note-")));

		const ControlResult rolled = registry->invoke(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		const QJsonArray notes = rolled.result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 1);
		QCOMPARE(notes.first().toObject().value(QStringLiteral("key")).toInt(), 60);
		QCOMPARE(notes.first().toObject().value(QStringLiteral("clip")).toString(), clip);

		// A malformed note id is invalid_args once the clip exists (id form first,
		// then existence: the same shape the other groups' errors have).
		const ControlResult badNote = registry->invoke(QStringLiteral("note.remove"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), QStringLiteral("note-x")}});
		QVERIFY(!badNote.ok);
		QCOMPARE(badNote.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), noteId},
				{QStringLiteral("position"), 48}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("position")).toInt(), 48);

		const ControlResult removed = registry->invoke(QStringLiteral("note.remove"),
			QJsonObject{{QStringLiteral("clip"), clip},
				{QStringLiteral("note"), moved.result.value(QStringLiteral("note")).toString()}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("note_count")).toInt(), 0);

		// SPEC A16: the note edit is reversible through the engine's ProjectJournal.
		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY(undo.ok);
		QVERIFY(undo.result.value(QStringLiteral("undone")).toBool());
		const ControlResult rolledBack = registry->invoke(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolledBack.ok, qPrintable(rolledBack.errorMessage));
		QCOMPARE(rolledBack.result.value(QStringLiteral("notes")).toArray().size(), 1);

		// And the transactions say what actually happened.
		const ControlResult transactions = registry->invoke(QStringLiteral("control.transactions"));
		QVERIFY(transactions.ok);
		bool sawNoteRemove = false;
		for (const QJsonValue& value : transactions.result.value(QStringLiteral("transactions")).toArray())
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("command")).toString() == QLatin1String("note.remove"))
			{
				sawNoteRemove = true;
				QVERIFY(entry.value(QStringLiteral("reversible")).toBool());
			}
		}
		QVERIFY(sawNoteRemove);
	}

	/*! BEFORE the fix, the selection was stored as POSITIONS in the note list
	 *  (control::selectNotes, src/core/ControlEditSupport.cpp) and roll.get_state
	 *  reported the ids of the notes AT those positions. Removing a note BEFORE a
	 *  selected one shifted every later note down one, so the stored position named
	 *  a different note: after removing note A, `selected_notes` named B before the
	 *  remove and named C after. The store keeps the selected notes' ids now.
	 */
	void aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote()
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

		// Three notes at distinct positions: note.add keeps the list ordered.
		QStringList ids;
		for (int i = 0; i < 3; ++i)
		{
			const ControlResult added = registry->invoke(QStringLiteral("note.add"),
				QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60 + i},
					{QStringLiteral("position"), i * 24}, {QStringLiteral("length"), 12}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			ids.append(added.result.value(QStringLiteral("note")).toString());
		}
		QCOMPARE(ids.size(), 3);

		// Select the LAST note - the one most likely to shift.
		const ControlResult selected = registry->invoke(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), QJsonArray{ids.at(2)}}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QCOMPARE(selected.result.value(QStringLiteral("selected_notes")).toArray().size(), 1);
		QCOMPARE(selected.result.value(QStringLiteral("selected_notes")).toArray().first().toString(),
			ids.at(2));

		// Remove the FIRST note. The selection was on the note whose id is
		// ids.at(2); after the removal that note is at position 1, so the selection
		// must follow the note - not stay at 2 and name whatever landed there.
		QVERIFY2(registry->invoke(QStringLiteral("note.remove"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), ids.at(0)}}).ok,
			"the first note did not remove");
		const ControlResult rolled = registry->invoke(QStringLiteral("roll.get_state"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		QCOMPARE(rolled.result.value(QStringLiteral("notes")).toArray().size(), 2);
		const QJsonArray selectedNotes = rolled.result.value(QStringLiteral("selected_notes")).toArray();
		QCOMPARE(selectedNotes.size(), 1);
		QVERIFY2(selectedNotes.first().toString() == ids.at(2),
			qPrintable(QStringLiteral("after removing the first note the selection reported "
				"note \"%1\", but the note that WAS selected is \"%2\" - the selection landed "
				"on a note the client never selected")
				.arg(selectedNotes.first().toString(), ids.at(2))));
	}

	/*! The same defect as the removal case above, reached the other way.
	 *  BEFORE the fix, note.move re-sorted the clip (clip->rearrangeAllNotes())
	 *  without remapping the stored positions, so a selected index silently
	 *  started naming a DIFFERENT note: selecting the first note and moving it
	 *  past the second left the selection pointing at position 0, which was
	 *  then the second note.
	 */
	void aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote()
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
		for (int i = 0; i < 2; ++i)
		{
			const ControlResult added = registry->invoke(QStringLiteral("note.add"),
				QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), 60},
					{QStringLiteral("position"), i * 48}, {QStringLiteral("length"), 12}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			ids.append(added.result.value(QStringLiteral("note")).toString());
		}

		// Select the FIRST note, then move it past the second.
		const ControlResult selected = registry->invoke(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), QJsonArray{ids.at(0)}}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QCOMPARE(selected.result.value(QStringLiteral("selected_notes")).toArray().first().toString(),
			ids.at(0));

		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), ids.at(0)},
				{QStringLiteral("position"), 120}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("note")).toString(), ids.at(0));

		// The note that was selected did not change; the list order around it did.
		const ControlResult rolled = registry->invoke(QStringLiteral("roll.get_state"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		const QJsonArray notes = rolled.result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 2);
		QCOMPARE(notes.at(0).toObject().value(QStringLiteral("id")).toString(), ids.at(1));
		const QJsonArray selectedNotes = rolled.result.value(QStringLiteral("selected_notes")).toArray();
		QCOMPARE(selectedNotes.size(), 1);
		QVERIFY2(selectedNotes.first().toString() == ids.at(0),
			qPrintable(QStringLiteral("after moving the selected note past the other one the "
				"selection reported note \"%1\", but the note that WAS selected is \"%2\" - the "
				"selection landed on a note the client never selected")
				.arg(selectedNotes.first().toString(), ids.at(0))));
	}

	//! track.set_arm is a REAL arm verb now (0.3.0, feature row 14): it starts a
	//! capture on the record route the track's position maps to, writes the take
	//! and journals it. This replaces the old "honest typed refusal" assertion
	//! that used to sit here - the refusal was correct about the tree it
	//! described and was itself the defect the feature list recorded.
	void trackSetArmStartsARealCapture()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult added = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString track = added.result.value(QStringLiteral("track")).toString();

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString take = dir.filePath(QStringLiteral("unit-take.wav"));

		const ControlResult armed = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track},
				{QStringLiteral("armed"), true},
				{QStringLiteral("file"), take}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("armed")).toBool(), true);
		QVERIFY(armed.result.value(QStringLiteral("route")).toInt() >= 0);
		// The take file and its journal are on disk: arming is a real capture,
		// not a flag.
		QVERIFY(QFileInfo::exists(take));
		QCOMPARE(armed.result.value(QStringLiteral("journal")).toString(),
			QString(take + QStringLiteral(".rec-journal")));
		QVERIFY(QFileInfo::exists(armed.result.value(QStringLiteral("journal")).toString()));

		// A second arm of the same route is a typed busy, not a second take.
		const ControlResult again = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track},
				{QStringLiteral("armed"), true},
				{QStringLiteral("file"), dir.filePath(QStringLiteral("second.wav"))}});
		QVERIFY(!again.ok);
		QCOMPARE(again.errorKind, ControlErrorKind::Busy);

		// Disarming retires the journal - a clean stop leaves none - and the take
		// itself stays.
		const ControlResult disarmed = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track},
				{QStringLiteral("armed"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(disarmed.result.value(QStringLiteral("armed")).toBool(), false);
		QCOMPARE(disarmed.result.value(QStringLiteral("journal")).toString(), QString());
		QVERIFY(QFileInfo::exists(take));

		// An unknown track is still a typed not_found.
		const ControlResult missing = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-99999")},
				{QStringLiteral("armed"), true}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
	}
	//! track.set_mute is a plain model write; track.set_solo is the product's whole
	//! solo action, so it unmutes the soloed track (Track::toggleSolo). Asserting
	//! both keeps the semantics of the two commands pinned.
	void trackMuteAndSoloReadBack()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult added = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY(added.ok);
		const QString track = added.result.value(QStringLiteral("track")).toString();

		const ControlResult muted = registry->invoke(QStringLiteral("track.set_mute"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("muted"), true}});
		QVERIFY(muted.ok);
		QVERIFY(muted.result.value(QStringLiteral("muted")).toBool());
		const ControlResult readBack = registry->invoke(QStringLiteral("track.get_state"),
			QJsonObject{{QStringLiteral("track"), track}});
		QVERIFY(readBack.ok);
		QVERIFY(readBack.result.value(QStringLiteral("muted")).toBool());

		const ControlResult soloed = registry->invoke(QStringLiteral("track.set_solo"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("solo"), true}});
		QVERIFY2(soloed.ok, qPrintable(soloed.errorMessage));
		QVERIFY(soloed.result.value(QStringLiteral("soloed")).toBool());
		const ControlResult afterSolo = registry->invoke(QStringLiteral("track.get_state"),
			QJsonObject{{QStringLiteral("track"), track}});
		QVERIFY(afterSolo.ok);
		QVERIFY(afterSolo.result.value(QStringLiteral("soloed")).toBool());
		// The solo action unmutes the soloed track, headless as well as on display.
		QVERIFY(!afterSolo.result.value(QStringLiteral("muted")).toBool());

		// track.remove deletes the track. Headless there is no view to remove
		// first; on a display the command takes the product's own order
		// (TrackContainerView::deleteTrackView removes the view, then the track).
		const ControlResult before = registry->invoke(QStringLiteral("arrangement.get_state"));
		QVERIFY(before.ok);
		const int countBefore = before.result.value(QStringLiteral("track_count")).toInt();
		const ControlResult removed = registry->invoke(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), track}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("removed")).toString(), track);
		QCOMPARE(removed.result.value(QStringLiteral("track_count")).toInt(), countBefore - 1);
	}

	//! In a headless process the surface can name one clip, but the arranger selection did not come back.
	void headlessSelectionApplyReportsClipsSkipped()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
		const QString track = addedTrack.result.value(QStringLiteral("track")).toString();

		const ControlResult firstAdded = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(firstAdded.ok, qPrintable(firstAdded.errorMessage));
		const ControlResult secondAdded = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 384},
				{QStringLiteral("length"), 192}});
		QVERIFY2(secondAdded.ok, qPrintable(secondAdded.errorMessage));
		const QJsonArray clips{firstAdded.result.value(QStringLiteral("clip")),
			secondAdded.result.value(QStringLiteral("clip"))};

		const ControlResult saved = registry->invoke(QStringLiteral("selection.save"),
			QJsonObject{{QStringLiteral("name"), QStringLiteral("headless-multiple-clips")},
				{QStringLiteral("clips"), clips}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		const ControlResult applied = registry->invoke(QStringLiteral("selection.apply"),
			QJsonObject{{QStringLiteral("name"), QStringLiteral("headless-multiple-clips")}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));

		QCOMPARE(applied.result.value(QStringLiteral("clips")).toArray(), clips);
		QCOMPARE(applied.result.value(QStringLiteral("missing")).toInt(), 0);
		QVERIFY(!applied.result.value(QStringLiteral("restored")).toArray().contains(QStringLiteral("clips")));
		QCOMPARE(applied.result.value(QStringLiteral("skipped")).toArray(), QJsonArray{QStringLiteral("clips")});

		const ControlResult state = registry->invoke(QStringLiteral("arrangement.get_state"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("selected_clip")).toString(), clips.first().toString());
	}

	//! track.add's undo finds its track by id when it runs: track.remove and its undo rebuild the
	//! track as a new object, and the step used to remove the freed one it kept a pointer to.
	void trackAddUndoFindsItsTrackAfterARemovalIsUndone()
	{
		const QString track = revtest::addTrack(QStringLiteral("sample"));
		QVERIFY(!track.isEmpty());
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(track));
	}

	//! Headless, track.set_solo has to drive Track::toggleSolo() itself (a display-ful
	//! instance gets it from TrackView's dataChanged hook). A request that changes
	//! nothing must not run it again: the second solo re-captured the ALREADY SOLOED
	//! mute states as the "before solo" snapshot, so the unsolo that follows restored
	//! them instead of the pre-solo ones.
	void trackSetSoloRepeatedInHeadlessModeIsIdempotent()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString lead = revtest::addTrack();
		const QString other = revtest::addTrack();
		QVERIFY(!lead.isEmpty());
		QVERIFY(!other.isEmpty());

		// Pre-solo: both unmuted, so the unsolo at the end has something to restore.
		const ControlResult mutedOther = registry->invoke(QStringLiteral("track.set_mute"),
			{{QStringLiteral("track"), other}, {QStringLiteral("muted"), false}});
		QVERIFY2(mutedOther.ok, qPrintable(mutedOther.errorMessage));

		const ControlResult soloed = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), true}});
		QVERIFY2(soloed.ok, qPrintable(soloed.errorMessage));
		QVERIFY(muted(other));
		QVERIFY(!muted(lead));

		// The same request again: the track is already soloed, so nothing may change.
		const ControlResult again = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), true}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));

		const ControlResult unsoloed = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), false}});
		QVERIFY2(unsoloed.ok, qPrintable(unsoloed.errorMessage));
		QVERIFY2(!muted(other), "the second solo recaptured the post-solo mute states as "
			"the before-solo snapshot, so unsoloing could not restore them");
	}

	//! track.set_arm_many answers 'tracks' = the ones it CHANGED. Disarming is
	//! not a refusal when the route is already disarmed, so the batch has to
	//! recognise that state itself - and a repeated id names one track, so it can
	//! be reported once at most.
	void trackSetArmManyReportsOnlyTheTracksItChanged()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString first = revtest::addTrack();
		const QString second = revtest::addTrack();
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());
		const QJsonArray both{first, second};

		const ControlResult armed = registry->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("tracks")).toArray().size(), 2);

		const ControlResult disarmed = registry->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), false}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(disarmed.result.value(QStringLiteral("tracks")).toArray().size(), 2);

		// Both routes are disarmed already: nothing changed, so nothing may be
		// listed as changed.
		const ControlResult noop = registry->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), false}});
		QVERIFY2(!noop.ok, "a disarm of already-disarmed routes reported changed tracks");
		QCOMPARE(noop.errorKind, ControlErrorKind::Busy);

		// A repeated id is one track: armed once, disarmed once, reported once.
		const QString third = revtest::addTrack();
		QVERIFY(!third.isEmpty());
		const QJsonArray twice{third, third};
		QVERIFY(registry->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), twice}, {QStringLiteral("armed"), true}}).ok);
		const ControlResult once = registry->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), twice}, {QStringLiteral("armed"), false}});
		QVERIFY2(once.ok, qPrintable(once.errorMessage));
		const QJsonArray reported = once.result.value(QStringLiteral("tracks")).toArray();
		QCOMPARE(reported.size(), 1);
		QCOMPARE(reported.first().toString(), third);
	}

	//! Batch arming rejects a non-string ID before changing any record route.
	void trackSetArmManyRejectsNonStringIdsBeforeArming()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString track = revtest::addTrack();
		QVERIFY(!track.isEmpty());
		QTemporaryDir recoveryDir;
		QVERIFY(recoveryDir.isValid());
		const QString oldWorkingDir = ConfigManager::inst()->workingDir();
		ConfigManager::inst()->setWorkingDir(recoveryDir.path());

		const ControlResult malformed = registry->invoke(QStringLiteral("track.set_arm_many"),
			QJsonObject{{QStringLiteral("tracks"), QJsonArray{track, 7}},
				{QStringLiteral("armed"), true}});
		// Capture whether the malformed call armed the valid first entry, then clean up
		// before asserting so the RED run leaves the recorder disarmed as well.
		const ControlResult cleanup = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}});
		ConfigManager::inst()->setWorkingDir(oldWorkingDir);

		QVERIFY2(!malformed.ok, qPrintable(QStringLiteral("malformed batch unexpectedly succeeded; valid route was armed before cleanup: %1")
			.arg(cleanup.result.value(QStringLiteral("armed_before")).toBool()
				? QStringLiteral("yes") : QStringLiteral("no"))));
		QCOMPARE(malformed.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(cleanup.ok, qPrintable(cleanup.errorMessage));
		QVERIFY(!cleanup.result.value(QStringLiteral("armed_before")).toBool());
	}

	//! An idempotent track.set_solo records no change, so its inverse has to name the
	//! state it found - not !value, which would unsolo the track the no-op request
	//! asked to keep soloed.
	void trackSetSoloNoOpRequestInvertsToTheStateItFound()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString track = revtest::addTrack();
		QVERIFY(!track.isEmpty());
		QVERIFY(registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), track}, {QStringLiteral("solo"), true}}).ok);

		const ControlResult repeat = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), track}, {QStringLiteral("solo"), true}});
		QVERIFY2(repeat.ok, qPrintable(repeat.errorMessage));
		// The reply itself never carries the transaction (finishResult strips
		// __transaction), so the recorded one is what a client reads back.
		QCOMPARE(revtest::stateOf(QStringLiteral("track.set_solo"))
			.value(QStringLiteral("inverse")).toObject()
			.value(QStringLiteral("args")).toObject()
			.value(QStringLiteral("solo")).toBool(), true);

		const ControlResult unsoloed = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), track}, {QStringLiteral("solo"), false}});
		QVERIFY(unsoloed.ok);
		const ControlResult repeatUnsolo = registry->invoke(QStringLiteral("track.set_solo"),
			{{QStringLiteral("track"), track}, {QStringLiteral("solo"), false}});
		QVERIFY2(repeatUnsolo.ok, qPrintable(repeatUnsolo.errorMessage));
		QCOMPARE(revtest::stateOf(QStringLiteral("track.set_solo"))
			.value(QStringLiteral("inverse")).toObject()
			.value(QStringLiteral("args")).toObject()
			.value(QStringLiteral("solo")).toBool(), false);
	}

	//! A folder track is a container with no clips of its own: Track::createClip()
	//! returns null for it. clip.add used to dereference that null (a crash). It must
	//! refuse the add instead, and the refusal must leave no undo step behind - the
	//! checkpoint is taken only for a track that can hold a clip.
	void clipAddOnAFolderTrackIsRefusedWithoutAnUndoStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult addedFolder = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("folder")}});
		QVERIFY2(addedFolder.ok, qPrintable(addedFolder.errorMessage));
		const QString folder = addedFolder.result.value(QStringLiteral("track")).toString();

		const ControlResult depthBefore = registry->invoke(QStringLiteral("control.undo_depth"), QJsonObject{});
		QVERIFY2(depthBefore.ok, qPrintable(depthBefore.errorMessage));

		// A name and a length are the handler's own dereferences (setName, changeLength):
		// without them the null clip is never touched and the old code only got as far
		// as a NotFound reply.
		const ControlResult added = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), folder}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}, {QStringLiteral("name"), QStringLiteral("Kick")}});
		QVERIFY2(!added.ok, "a clip was added to a folder track");
		QCOMPARE(added.errorKind, ControlErrorKind::Refused);

		const ControlResult depthAfter = registry->invoke(QStringLiteral("control.undo_depth"), QJsonObject{});
		QVERIFY2(depthAfter.ok, qPrintable(depthAfter.errorMessage));
		QCOMPARE(depthAfter.result.value(QStringLiteral("depth")).toInt(),
			depthBefore.result.value(QStringLiteral("depth")).toInt());
	}
};

QTEST_GUILESS_MAIN(ControlEditCommandsTest)
#include "ControlEditCommandsTest.moc"
