/*
 * ControlNoteNoOpUndoTest.cpp - a note transform that changed NOTHING must not
 *                               advertise an undo (cluster new-p14-38 / new-p17-19).
 *
 * The note verbs take a MidiClip journal checkpoint only when they are about to
 * change something - the checkpoint IS their inverse. The reply, though, used to
 * carry the "__transaction" descriptor (the inverse that a caller may replay) in
 * EVERY case, so a call that changed nothing still advertised `control.undo`.
 * Replaying it unwound the journal: it took back the PREVIOUS edit, which the
 * caller never asked to undo. The rule this file pins down:
 *
 *   - a transform that changed nothing records no transaction and leaves no undo
 *     step, so the journal's top is still the last REAL edit;
 *   - a transform that changed something still records its transaction and still
 *     undoes in one step (the last test here is the positive control: a guard
 *     that never advertised anything would pass every other test in this file);
 *   - `note.invert` counts the notes it MOVED, not the notes it was given: a note
 *     that stays on the pivot key is not an inverted note.
 *
 * The seams are the registry's own: the transaction count from
 * `control.transactions`, the transaction on top from
 * `ControlRegistry::lastTransaction()`, and the notes themselves from
 * `roll.get_state`. Nothing here reads a clock, sleeps or waits: every
 * assertion is about state that is already there when the call returns.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <limits>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"

using namespace lmms;
using namespace revtest;

namespace
{

struct Fixture
{
	QString track;
	QString clip;
	QString note;
};

bool makeFixture(Fixture* fixture)
{
	fixture->track = addTrack();
	if (fixture->track.isEmpty()) { return false; }
	const ControlResult added = run(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), fixture->track},
			{QStringLiteral("position"), 0}});
	if (!added.ok) { return false; }
	fixture->clip = added.result.value(QStringLiteral("clip")).toString();
	return true;
}

//! One note at key 60 with velocity 100: a clip with something in it.
bool makeOneNoteFixture(Fixture* fixture)
{
	if (!makeFixture(fixture)) { return false; }
	const ControlResult added = run(QStringLiteral("note.add"),
		QJsonObject{{QStringLiteral("clip"), fixture->clip}, {QStringLiteral("key"), 60},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24},
			{QStringLiteral("velocity"), 100}});
	if (!added.ok) { return false; }
	fixture->note = added.result.value(QStringLiteral("note")).toString();
	return !fixture->note.isEmpty();
}

//! How many transactions the registry has recorded so far.
int transactionCount()
{
	return run(QStringLiteral("control.transactions"))
		.result.value(QStringLiteral("count")).toInt();
}

//! The command of the transaction on top of the registry's record, or an empty
//! string when it has none.
QString lastTransactionCommand()
{
	const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
	return tx == nullptr ? QString() : tx->command;
}

void markProjectClean()
{
	Song* song = Engine::getSong();
	song->noteProjectSaved(song->projectFileName());
}

void checkProjectDirtyAfter(const QString& command, const QJsonObject& args, QStringList* failures)
{
	markProjectClean();
	const ControlResult result = run(command, args);
	if (!result.ok)
	{
		failures->append(command + QStringLiteral(" failed: ") + result.errorMessage);
		return;
	}
	if (!Engine::getSong()->isModified())
	{
		failures->append(command + QStringLiteral(" left the project clean"));
	}
}

QJsonObject noteMutationArgs(const Fixture& fixture, const QString& field, const QJsonValue& value)
{
	return {{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note}, {field, value}};
}

struct UndoDepthRestore
{
	ProjectJournal* journal;
	int depth;

	~UndoDepthRestore() { journal->setMaxUndoStates(depth); }
};

} // namespace


class ControlNoteNoOpUndoTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The heart of it: a no-op invert records no transaction, so the journal's
	//! top is still the edit that came before it. Replaying the advertised
	//! `control.undo` would otherwise take that earlier edit back.
	void noOpInvertAdvertisesNoUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		QCOMPARE(lastTransactionCommand(), QStringLiteral("note.add"));

		const int before = transactionCount();
		const ControlResult inverted = run(QStringLiteral("note.invert"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(inverted.ok, qPrintable(inverted.errorMessage));
		QCOMPARE(transactionCount(), before);
		QCOMPARE(lastTransactionCommand(), QStringLiteral("note.add"));
	}

	//! The count a no-op invert reports: it inverted nothing.
	void noOpInvertReportsNoNotesInverted()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const ControlResult inverted = run(QStringLiteral("note.invert"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(inverted.ok, qPrintable(inverted.errorMessage));
		QCOMPARE(inverted.result.value(QStringLiteral("inverted")).toInt(), 0);
		QCOMPARE(noteKey(fixture.clip, 0), 60);
	}

	//! A note that stays on the pivot key is not an inverted note: of the three
	//! notes below, only the outer two move.
	void invertCountsOnlyTheNotesThatMove()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a fixture");
		for (int key : {60, 62, 64})
		{
			QVERIFY(run(QStringLiteral("note.add"),
				QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("key"), key},
					{QStringLiteral("position"), (key - 60) * 12}, {QStringLiteral("length"), 24}}).ok);
		}
		const ControlResult inverted = run(QStringLiteral("note.invert"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(inverted.ok, qPrintable(inverted.errorMessage));
		QCOMPARE(inverted.result.value(QStringLiteral("inverted")).toInt(), 2);
	}

	//! A zero-step transpose in key moves no note, so it advertises no undo.
	void noOpTransposeInKeyAdvertisesNoUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		// The scale the transpose walks in has to exist, or the call is refused
		// before it can be a no-op at all.
		QVERIFY2(run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), 0}}).ok, "could not set the root");
		QVERIFY2(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Major")}}).ok, "could not set the scale");

		const int before = transactionCount();
		const ControlResult moved = run(QStringLiteral("note.transpose_in_key"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("steps"), 0}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("moved")).toInt(), 0);
		QCOMPARE(transactionCount(), before);

		// The scale context is global: leave it as this test found it.
		QVERIFY(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("")}}).ok);
	}

	//! A zero-semitone transpose moves no note, so it advertises no undo.
	void noOpTransposeAdvertisesNoUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const int before = transactionCount();
		const ControlResult transposed = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("semitones"), 0}});
		QVERIFY2(transposed.ok, qPrintable(transposed.errorMessage));
		QCOMPARE(transposed.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		QCOMPARE(transactionCount(), before);
	}

	//! A zero velocity delta changes no velocity, so it advertises no undo.
	void noOpVelocityOffsetAdvertisesNoUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const int before = transactionCount();
		const ControlResult offset = run(QStringLiteral("note.velocity_offset"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("delta"), 0}});
		QVERIFY2(offset.ok, qPrintable(offset.errorMessage));
		QCOMPARE(offset.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		QCOMPARE(transactionCount(), before);
	}

	//! A factor of one changes no velocity, so it advertises no undo.
	void noOpVelocityScaleAdvertisesNoUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const int before = transactionCount();
		const ControlResult scaled = run(QStringLiteral("note.velocity_scale"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("factor"), 1.0}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(scaled.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		QCOMPARE(transactionCount(), before);
	}

	//! A note with the largest tick_t length is refused before a paste checkpoint.
	void clipboardPasteRefusesAnOverflowingEndpoint()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		control::ClipRef ref;
		ControlResult error;
		MidiClip* clip = control::resolveMidiClip(fixture.clip, &ref, &error);
		QVERIFY2(clip != nullptr, qPrintable(error.errorMessage));
		QCOMPARE(clip->notes().size(), std::size_t{1});
		Note* note = clip->notes().front();
		note->setLength(TimePos(std::numeric_limits<tick_t>::max()));
		QVERIFY2(run(QStringLiteral("note.copy"), {{QStringLiteral("clip"), fixture.clip}}).ok,
			"could not copy the wide note");
		const quint64 before = Engine::projectJournal()->topStepSerial();

		const ControlResult pasted = run(QStringLiteral("note.paste"), {{QStringLiteral("clip"), fixture.clip},
			{QStringLiteral("position"), 1}});
		QCOMPARE(pasted.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});
		QCOMPARE(note->pos().getTicks(), tick_t{0});
		QCOMPARE(note->length().getTicks(), std::numeric_limits<tick_t>::max());
		QCOMPARE(Engine::projectJournal()->topStepSerial(), before);
	}

	//! Duplicate shares the fit check, so its span-derived offset must refuse before checkpointing too.
	void duplicateRefusesAnOverflowingEndpoint()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		control::ClipRef ref;
		ControlResult error;
		MidiClip* clip = control::resolveMidiClip(fixture.clip, &ref, &error);
		QVERIFY2(clip != nullptr, qPrintable(error.errorMessage));
		QCOMPARE(clip->notes().size(), std::size_t{1});
		Note* note = clip->notes().front();
		note->setLength(TimePos(std::numeric_limits<tick_t>::max()));
		const quint64 before = Engine::projectJournal()->topStepSerial();

		const ControlResult duplicated = run(QStringLiteral("note.duplicate"),
			{{QStringLiteral("clip"), fixture.clip}});
		QCOMPARE(duplicated.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});
		QCOMPARE(note->pos().getTicks(), tick_t{0});
		QCOMPARE(note->length().getTicks(), std::numeric_limits<tick_t>::max());
		QCOMPARE(Engine::projectJournal()->topStepSerial(), before);
	}

	//! Resizing to the current length must not push a MidiClip journal checkpoint.
	void noOpResizeDoesNotAddCheckpoint()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int transactionsBefore = transactionCount();
		const QString lastTransactionBefore = lastTransactionCommand();
		markProjectClean();
		const quint64 before = journal->topStepSerial();

		const ControlResult resized = run(QStringLiteral("note.resize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("note"), fixture.note}, {QStringLiteral("length"), 24}});
		QVERIFY2(resized.ok, qPrintable(resized.errorMessage));
		QCOMPARE(resized.result.value(QStringLiteral("length")).toInt(), 24);
		QCOMPARE(journal->topStepSerial(), before);
		QCOMPARE(transactionCount(), transactionsBefore);
		QCOMPARE(lastTransactionCommand(), lastTransactionBefore);
		QVERIFY(!Engine::getSong()->isModified());
	}

	//! Setting the current velocity must not push a MidiClip journal checkpoint.
	void noOpVelocitySetDoesNotAddCheckpoint()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int transactionsBefore = transactionCount();
		const QString lastTransactionBefore = lastTransactionCommand();
		markProjectClean();
		const quint64 before = journal->topStepSerial();

		const ControlResult set = run(QStringLiteral("note.velocity_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("note"), fixture.note}, {QStringLiteral("velocity"), 100}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("velocity")).toInt(), 100);
		QCOMPARE(journal->topStepSerial(), before);
		QCOMPARE(transactionCount(), transactionsBefore);
		QCOMPARE(lastTransactionCommand(), lastTransactionBefore);
		QVERIFY(!Engine::getSong()->isModified());
	}

	//! Zero jitter is a valid identity roll and must not dirty the project.
	void zeroJitterRandomizeDoesNotDirtyProject()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		markProjectClean();

		const ControlResult rolled = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.0}, {QStringLiteral("position_jitter"), 0.0},
				{QStringLiteral("seed"), 123}, {QStringLiteral("scope"), QStringLiteral("clip")}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		QVERIFY(!Engine::getSong()->isModified());
	}

	//! Every named successful note mutation dirties the project; the existing
	//! stream, seed, harmony and move helpers are included as consistency controls.
	void successfulNoteMutationsMarkProjectDirty()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		QStringList failures;
		const QJsonObject noteRef{{QStringLiteral("clip"), fixture.clip},
			{QStringLiteral("note"), fixture.note}};

		checkProjectDirtyAfter(QStringLiteral("note.move"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note},
				{QStringLiteral("position"), 12}}, &failures);
		checkProjectDirtyAfter(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note},
				{QStringLiteral("length"), 48}}, &failures);
		checkProjectDirtyAfter(QStringLiteral("note.velocity_set"),
			noteMutationArgs(fixture, QStringLiteral("velocity"), 120), &failures);
		checkProjectDirtyAfter(QStringLiteral("note.probability_set"),
			noteMutationArgs(fixture, QStringLiteral("probability"), 0.25), &failures);
		checkProjectDirtyAfter(QStringLiteral("note.expression_set"),
			noteMutationArgs(fixture, QStringLiteral("pitch"), 120), &failures);

		markProjectClean();
		const ControlResult expression = run(QStringLiteral("note.expression_set"),
			noteMutationArgs(fixture, QStringLiteral("pitch"), 240));
		if (!expression.ok) { failures.append(QStringLiteral("expression_clear setup failed")); }
		checkProjectDirtyAfter(QStringLiteral("note.expression_clear"), noteRef, &failures);
		checkProjectDirtyAfter(QStringLiteral("note.expression_stream_set"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note},
				{QStringLiteral("pitch"),
					QJsonArray{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 300}}}}},
			&failures);
		checkProjectDirtyAfter(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.5}, {QStringLiteral("position_jitter"), 12},
				{QStringLiteral("seed"), 123}, {QStringLiteral("scope"), QStringLiteral("clip")}},
			&failures);
		checkProjectDirtyAfter(QStringLiteral("note.random_seed_set"),
			{{QStringLiteral("seed"), static_cast<int>(Engine::getSong()->midiSeed() + 1)}}, &failures);

		markProjectClean();
		const ControlResult chord = run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Major")},
				{QStringLiteral("root"), 2}, {QStringLiteral("octave"), 4}});
		if (!chord.ok) { failures.append(QStringLiteral("follow_chords setup failed")); }
		checkProjectDirtyAfter(QStringLiteral("note.follow_chords"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("scope"), QStringLiteral("clip")}},
			&failures);

		const QString destination = addTrack();
		if (destination.isEmpty()) { failures.append(QStringLiteral("move_to_track setup failed")); }
		else
		{
			checkProjectDirtyAfter(QStringLiteral("note.move_to_track"),
				{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("track"), destination},
					{QStringLiteral("scope"), QStringLiteral("clip")}}, &failures);
		}
		QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
	}

	//! An invalid note reference fails before the checkpoint or dirty-state update.
	void failedNoteMutationLeavesProjectClean()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		markProjectClean();
		const quint64 before = journal->topStepSerial();
		const ControlResult failed = run(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), QStringLiteral("note-missing")},
				{QStringLiteral("length"), 48}});
		QVERIFY(!failed.ok);
		QCOMPARE(journal->topStepSerial(), before);
		QVERIFY(!Engine::getSong()->isModified());
	}

	//! A one-step undo cap does not turn a no-op into a checkpoint or suppress a real edit.
	void boundedJournalKeepsMutationDirtyState()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		UndoDepthRestore restore{journal, journal->maxUndoStates()};
		QVERIFY(journal->setMaxUndoStates(1));
		markProjectClean();
		const quint64 before = journal->topStepSerial();

		QVERIFY(run(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note},
				{QStringLiteral("length"), 24}}).ok);
		QCOMPARE(journal->topStepSerial(), before);
		QVERIFY(!Engine::getSong()->isModified());
		const ControlResult changed = run(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), fixture.note},
				{QStringLiteral("length"), 48}});
		QVERIFY2(changed.ok, qPrintable(changed.errorMessage));
		QCOMPARE(journal->topStepSerial(), before + 1);
		QVERIFY(Engine::getSong()->isModified());
	}

	//! The positive control: a transform that DID change something still records
	//! its transaction, still advertises the undo that reverses it, and still
	//! undoes in one step. A guard that swallowed every transaction would pass
	//! every other test in this file and fail this one.
	void aTransformThatChangesSomethingStillAdvertisesItsUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const int before = transactionCount();
		const ControlResult transposed = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("semitones"), 12}});
		QVERIFY2(transposed.ok, qPrintable(transposed.errorMessage));
		QCOMPARE(transposed.result.value(QStringLiteral("notes_changed")).toInt(), 1);
		QCOMPARE(transactionCount(), before + 1);
		QCOMPARE(lastTransactionCommand(), QStringLiteral("note.transpose"));
		QCOMPARE(noteKey(fixture.clip, 0), 72);

		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 0), 60);
	}

};

QTEST_GUILESS_MAIN(ControlNoteNoOpUndoTest)
#include "ControlNoteNoOpUndoTest.moc"
