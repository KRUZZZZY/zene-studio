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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

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

	//! track.set_arm is an honest typed refusal: this tree has no arm on a Track.
	void trackSetArmRefusesTyped()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult added = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY(added.ok);
		const ControlResult armed = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), added.result.value(QStringLiteral("track")).toString()},
				{QStringLiteral("armed"), true}});
		QVERIFY(!armed.ok);
		QCOMPARE(armed.errorKind, ControlErrorKind::Refused);
		QVERIFY(armed.errorMessage.contains(QStringLiteral("MultiTrackRecorder")));
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
};

QTEST_GUILESS_MAIN(ControlEditCommandsTest)
#include "ControlEditCommandsTest.moc"
