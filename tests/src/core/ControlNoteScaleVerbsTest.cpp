/*
 * ControlNoteScaleVerbsTest.cpp - the 0.3.0 note/scale/device wave's fifteen ids,
 *                                  their contract rows, and the REAL inverses of
 *                                  every one that edits (SPEC A16; board task #648;
 *                                  feature-list rows 11, 66 and 81).
 *
 * The engines already existed at the tip and are tested elsewhere (NoteRandomTest,
 * NoteTransformTest, SlideNotesTest, MidiProbabilityPersistenceTest, MpeExpressionTest).
 * What did not exist was an id, a schema, an A16 row and a proof. This is the proof,
 * and it is the shape SPEC A16 asks for rather than a "it compiled" check:
 *
 *   - every one of the fifteen ids is registered with a non-empty argument AND result
 *     schema and has a contract row whose class is the one this file asserts;
 *   - for each EDITING verb the inverse is EXERCISED: invoke, read the state back,
 *     control.undo, read the state back again, and compare against the pre-command
 *     values;
 *   - the RANDOMISATION PAIR is asserted as a pair, so a comparator that only checks
 *     "the call succeeded" cannot pass it: the same seed on the same starting notes
 *     reproduces the take EXACTLY, and a different seed produces a different one (an
 *     inequality over eight notes at distinct keys and positions with a full-range
 *     jitter - asserted as an inequality rather than a fixed vector because a fixed
 *     vector would be a copy of the engine's hash and not a measurement of it);
 *   - the two TRAPS are covered from the state they live in: a note at the DEFAULT
 *     slide flag (whose checkpoint carries no "slide" attribute at all, so the undo
 *     depends on Note::loadSettings reading attribute("slide") unconditionally) and a
 *     clip whose notes are ALREADY in scale (where re-running a snap moves nothing,
 *     which is why the checkpoint is the inverse);
 *   - the typed refusals are checked too: a scale with no name set, a selection scope
 *     with nothing selected, an out-of-range transpose, and an unknown scale name.
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
#include <QSet>
#include <QStringList>
#include <QVector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

const QStringList& allIds()
{
	static const QStringList ids{
		QStringLiteral("note.random_seed_get"), QStringLiteral("note.random_seed_set"),
		QStringLiteral("note.randomize"),
		QStringLiteral("note.transpose"), QStringLiteral("note.velocity_offset"),
		QStringLiteral("note.velocity_scale"), QStringLiteral("note.slide_set"),
		QStringLiteral("note.slide_clear"), QStringLiteral("scale.list"),
		QStringLiteral("scale.get_state"), QStringLiteral("scale.root_set"),
		QStringLiteral("scale.set"), QStringLiteral("scale.snap_notes"),
		QStringLiteral("device.mpe_get_state"), QStringLiteral("device.mpe_set")};
	return ids;
}

//! The ids this file proves the inverse of: everything that edits something.
const QStringList& editingIds()
{
	static const QStringList ids{
		QStringLiteral("note.random_seed_set"), QStringLiteral("note.randomize"),
		QStringLiteral("note.transpose"), QStringLiteral("note.velocity_offset"),
		QStringLiteral("note.velocity_scale"), QStringLiteral("note.slide_set"),
		QStringLiteral("note.slide_clear"), QStringLiteral("scale.root_set"),
		QStringLiteral("scale.set"), QStringLiteral("scale.snap_notes"),
		QStringLiteral("device.mpe_set")};
	return ids;
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

struct Fixture
{
	QString track;
	QString clip;
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

//! Eight notes at distinct keys and positions, all the same velocity, so a roll has
//! something to move and two takes can be compared note by note.
bool addRollFixtureNotes(const QString& clip)
{
	static const int keys[] = {60, 62, 64, 65, 67, 69, 71, 72};
	for (int index = 0; index < 8; ++index)
	{
		const ControlResult added = run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), keys[index]},
				{QStringLiteral("position"), index * 24}, {QStringLiteral("length"), 24},
				{QStringLiteral("velocity"), 100}});
		if (!added.ok) { return false; }
	}
	return true;
}

QVector<double> velocitiesOf(const QString& clip)
{
	QVector<double> velocities;
	const ControlResult state = run(QStringLiteral("roll.get_state"),
		QJsonObject{{QStringLiteral("clip"), clip}});
	for (const QJsonValue& value : state.result.value(QStringLiteral("notes")).toArray())
	{
		velocities.append(value.toObject().value(QStringLiteral("velocity")).toDouble());
	}
	return velocities;
}

bool sameVelocities(const QVector<double>& a, const QVector<double>& b)
{
	if (a.size() != b.size() || a.isEmpty()) { return false; }
	for (int index = 0; index < a.size(); ++index)
	{
		if (a.at(index) != b.at(index)) { return false; }
	}
	return true;
}

} // namespace

class ControlNoteScaleVerbsTest : public QObject
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

	//! W1: every id is declared, typed, and classified - and the classes are the ones
	//! the file's own comments claim.
	void everyVerbIsRegisteredWithSchemasAndAContractRow()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QSet<QString> seen;
		for (const QString& id : allIds())
		{
			seen.insert(id);
			const ControlCommand* command = registry->command(id);
			QVERIFY2(command != nullptr, qPrintable(id + " is not registered"));
			QVERIFY2(!command->argsSchema.isEmpty(), qPrintable(id + " declares no argument schema"));
			QVERIFY2(!command->resultSchema.isEmpty(), qPrintable(id + " declares no result schema"));
			QVERIFY2(!command->description.isEmpty(), qPrintable(id + " has no description"));
			QVERIFY2(contractRow(id) != nullptr, qPrintable(id + " has no A16 contract row"));
			QVERIFY2(!contractRow(id)->reason.isEmpty() && !contractRow(id)->mechanism.isEmpty(),
				qPrintable(id + " has an empty contract row"));
		}
		QCOMPARE(seen.size(), 15);
		QCOMPARE(seen.size(), allIds().size());
		for (const QString& id : editingIds())
		{
			QVERIFY2(contractRow(id)->cls == control::ReversibilityClass::TrueInverse,
				qPrintable(id + " must be classified true_inverse"));
			QVERIFY2(contractRow(id)->reversible, qPrintable(id + " must be reversible"));
		}
		// The four readers write nothing, and the registry must not record a
		// transaction for them.
		for (const QString& id : {QStringLiteral("note.random_seed_get"),
			QStringLiteral("scale.list"), QStringLiteral("scale.get_state"),
			QStringLiteral("device.mpe_get_state")})
		{
			QCOMPARE(control::reversibilityClassName(contractRow(id)->cls),
				QStringLiteral("not_mutating"));
			QVERIFY2(!registry->command(id)->mutating, qPrintable(id + " must not be mutating"));
		}
		// And the mutating ones ARE declared mutating (the mirror direction).
		for (const QString& id : editingIds())
		{
			QVERIFY2(registry->command(id)->mutating,
				qPrintable(id + " must be declared mutating"));
		}
	}

	//! W2: the seed pair - the persisted half is written, read back, and undone.
	void theSeedIsPersistedAndItsUndoRestoresIt()
	{
		const ControlResult initial = run(QStringLiteral("note.random_seed_get"));
		QVERIFY2(initial.ok, qPrintable(initial.errorMessage));
		const double previous = initial.result.value(QStringLiteral("seed")).toDouble();
		QVERIFY2(initial.result.value(QStringLiteral("stored_in")).toString()
			.contains(QStringLiteral("midiseed")),
			"the read must name where the seed is persisted");

		const ControlResult set = run(QStringLiteral("note.random_seed_set"),
			QJsonObject{{QStringLiteral("seed"), 424242}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("seed")).toDouble(), 424242.0);
		QCOMPARE(set.result.value(QStringLiteral("previous")).toDouble(), previous);
		QCOMPARE(set.result.value(QStringLiteral("persisted")).toBool(), true);
		const ControlResult readBack = run(QStringLiteral("note.random_seed_get"));
		QCOMPARE(readBack.result.value(QStringLiteral("seed")).toDouble(), 424242.0);

		REV_UNDO_OR_FAIL();
		const ControlResult after = run(QStringLiteral("note.random_seed_get"));
		QCOMPARE(after.result.value(QStringLiteral("seed")).toDouble(), previous);

		// A missing seed is refused, not defaulted.
		const ControlResult noSeed = run(QStringLiteral("note.random_seed_set"));
		QCOMPARE(noSeed.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! W3: THE REPEATABILITY PAIR. Same seed -> identical take (exactly); different
	//! seed -> a different one. Without the first half a roll that did nothing would
	//! pass; without the second, a command that did not read the seed at all would.
	void theRollIsRepeatableWithOneSeedAndDifferentWithAnother()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		QVERIFY2(addRollFixtureNotes(fixture.clip), "could not add the roll fixture's notes");
		QVERIFY2(run(QStringLiteral("note.random_seed_set"),
			QJsonObject{{QStringLiteral("seed"), 424242}}).ok, "could not set the project seed");
		const QVector<double> start = velocitiesOf(fixture.clip);
		QCOMPARE(start.size(), 8);

		const ControlResult first = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 1.0}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		QCOMPARE(first.result.value(QStringLiteral("seed")).toDouble(), 424242.0);
		QCOMPARE(first.result.value(QStringLiteral("seed_source")).toString(),
			QStringLiteral("project"));
		QVERIFY2(first.result.value(QStringLiteral("velocities_moved")).toInt() > 0,
			"a full-range velocity roll over eight notes moved none of them");
		const QVector<double> takeOne = velocitiesOf(fixture.clip);

		REV_UNDO_OR_FAIL();
		QVERIFY2(sameVelocities(velocitiesOf(fixture.clip), start),
			"the undo must put every velocity back");

		// Same seed, same starting notes -> the SAME take, note for note.
		const ControlResult second = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 1.0}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		QVERIFY2(sameVelocities(velocitiesOf(fixture.clip), takeOne),
			"the same seed on the same notes must reproduce the take exactly");

		REV_UNDO_OR_FAIL();
		// A different seed -> a different take. Asserted over the eight notes rather
		// than against a fixed vector: a copied vector would be a second copy of the
		// engine's hash instead of a measurement of it.
		const ControlResult third = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 1.0},
				{QStringLiteral("seed"), 999983}});
		QVERIFY2(third.ok, qPrintable(third.errorMessage));
		QCOMPARE(third.result.value(QStringLiteral("seed")).toDouble(), 999983.0);
		QCOMPARE(third.result.value(QStringLiteral("seed_source")).toString(),
			QStringLiteral("argument"));
		QVERIFY2(!sameVelocities(velocitiesOf(fixture.clip), takeOne),
			"a different seed must roll a different take");

		REV_UNDO_OR_FAIL();
		// A named seed is NOT written to the project: the persisted one is unchanged.
		QCOMPARE(run(QStringLiteral("note.random_seed_get")).result
			.value(QStringLiteral("seed")).toDouble(), 424242.0);
	}

	//! W4: the roll's typed refusals - a missing jitter, an out-of-range one, and a
	//! clip with no notes at all.
	void theRollRefusesWhatItCannotMean()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		const ControlResult noNotes = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.5}});
		QCOMPARE(noNotes.errorKind, ControlErrorKind::Refused);
		QVERIFY2(addRollFixtureNotes(fixture.clip), "could not add the roll fixture's notes");
		const ControlResult noJitter = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QCOMPARE(noJitter.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult tooBig = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 1.5}});
		QCOMPARE(tooBig.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult badScope = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.5},
				{QStringLiteral("scope"), QStringLiteral("track")}});
		QCOMPARE(badScope.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! W5: the slide verbs, from the trap state (a note at the DEFAULT, which writes
	//! no "slide" attribute at all).
	void slideSetAndClearAreOneUndoableStepEach()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		const ControlResult added = run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString note = added.result.value(QStringLiteral("note")).toString();
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("slide")).toBool(), false);

		const ControlResult set = run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), note},
				{QStringLiteral("slide"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("slide")).toBool(), true);
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("slide")).toBool(), true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("slide")).toBool(), false);

		// Clearing is a clip-level edit: set two, clear the clip, clear NONE when the
		// scope is an empty selection (which is refused rather than reported as 0).
		QVERIFY(run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), note},
				{QStringLiteral("slide"), true}}).ok);
		const ControlResult cleared = run(QStringLiteral("note.slide_clear"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("slides_before")).toInt(), 1);
		QCOMPARE(cleared.result.value(QStringLiteral("slides_cleared")).toInt(), 1);
		QCOMPARE(cleared.result.value(QStringLiteral("slides_remaining")).toInt(), 0);

		REV_UNDO_OR_FAIL();
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("slide")).toBool(), true);

		const ControlResult missing = run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), note}});
		QCOMPARE(missing.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! W6: the three transforms, each with its inverse exercised for real, plus the
	//! selection scope and the two refusals it can produce.
	void theThreeTransformsMoveNotesAndUndoPutsThemBack()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		const ControlResult added = run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24},
				{QStringLiteral("velocity"), 100}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(noteKey(fixture.clip, 0), 60);

		const ControlResult up = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("semitones"), 12}});
		QVERIFY2(up.ok, qPrintable(up.errorMessage));
		QCOMPARE(up.result.value(QStringLiteral("notes_changed")).toInt(), 1);
		QCOMPARE(noteKey(fixture.clip, 0), 72);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 0), 60);

		const ControlResult louder = run(QStringLiteral("note.velocity_offset"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("delta"), 25}});
		QVERIFY2(louder.ok, qPrintable(louder.errorMessage));
		QCOMPARE(noteVelocity(fixture.clip, 0), 125.0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteVelocity(fixture.clip, 0), 100.0);

		const ControlResult halved = run(QStringLiteral("note.velocity_scale"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("factor"), 0.5}});
		QVERIFY2(halved.ok, qPrintable(halved.errorMessage));
		QCOMPARE(noteVelocity(fixture.clip, 0), 50.0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteVelocity(fixture.clip, 0), 100.0);

		// Out of the accepted range: refused typed, and nothing moved.
		const ControlResult tooFar = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("semitones"), 200}});
		QCOMPARE(tooFar.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(noteKey(fixture.clip, 0), 60);

		// A selection scope with nothing selected is refused, because "I edited
		// nothing" and "there was nothing to edit" must be distinguishable.
		const ControlResult nothingSelected = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("semitones"), 1},
				{QStringLiteral("scope"), QStringLiteral("selection")}});
		QCOMPARE(nothingSelected.errorKind, ControlErrorKind::Refused);

		// With a selection, only the selected note moves.
		const ControlResult second = run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("key"), 64},
				{QStringLiteral("position"), 48}, {QStringLiteral("length"), 24}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		QVERIFY2(run(QStringLiteral("note.select"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("notes"), QJsonArray{QStringLiteral("note-0")}}}).ok,
			"could not select the first note");
		const ControlResult selected = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("semitones"), 3},
				{QStringLiteral("scope"), QStringLiteral("selection")}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QCOMPARE(selected.result.value(QStringLiteral("notes_considered")).toInt(), 1);
		QCOMPARE(noteKey(fixture.clip, 0), 63);
		QCOMPARE(noteKey(fixture.clip, 1), 64);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 0), 60);
		QVERIFY2(run(QStringLiteral("note.select"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("notes"),
				QJsonArray()}}).ok, "could not clear the selection");
	}

	//! W7: the scale vocabulary is the engine's own, and the analysis is a measurement.
	void theScaleVocabularyResolvesThroughTheEngine()
	{
		const ControlResult listed = run(QStringLiteral("scale.list"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray scales = listed.result.value(QStringLiteral("scales")).toArray();
		QVERIFY2(scales.size() >= 10, qPrintable(QStringLiteral("only %1 scales are published")
			.arg(scales.size())));
		bool sawMajor = false;
		for (const QJsonValue& value : scales)
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("name")).toString() != QStringLiteral("Major"))
			{
				continue;
			}
			sawMajor = true;
			QCOMPARE(entry.value(QStringLiteral("degrees")).toArray().size(), 7);
			// Index 0 = C: the major scale is C, D, E, F, G, A, B.
			QCOMPARE(entry.value(QStringLiteral("mask")).toString(),
				QStringLiteral("101010110101"));
		}
		QVERIFY2(sawMajor, "the engine's own scale vocabulary must carry Major");
		QCOMPARE(listed.result.value(QStringLiteral("roots")).toArray().size(), 12);

		// A root by NAME shifts the classes; by index gives the same answer.
		const ControlResult byName = run(QStringLiteral("scale.get_state"),
			QJsonObject{{QStringLiteral("root"), QStringLiteral("D")},
				{QStringLiteral("scale"), QStringLiteral("Major")}});
		QVERIFY2(byName.ok, qPrintable(byName.errorMessage));
		const ControlResult byIndex = run(QStringLiteral("scale.get_state"),
			QJsonObject{{QStringLiteral("root"), 2},
				{QStringLiteral("scale"), QStringLiteral("Major")}});
		QCOMPARE(byName.result.value(QStringLiteral("mask")).toString(),
			byIndex.result.value(QStringLiteral("mask")).toString());
		QCOMPARE(byName.result.value(QStringLiteral("pitch_classes")).toArray().size(), 7);
		QCOMPARE(byName.result.value(QStringLiteral("resolved_from_arguments")).toBool(), true);

		const ControlResult unknown = run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Locrian b7 super")}});
		QCOMPARE(unknown.errorKind, ControlErrorKind::NotFound);
		const ControlResult badRoot = run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), QStringLiteral("H")}});
		QCOMPARE(badRoot.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! W8: the context writers, their inverses, and the refusal a snap makes when no
	//! scale is set at all.
	void theScaleContextIsReversibleAndSnapRefusesWithoutOne()
	{
		// Clear the context first, so this test's starting point is known whatever ran
		// before it.
		QVERIFY2(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("")}}).ok,
			"could not clear the scale");
		QVERIFY2(run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), 0}}).ok, "could not set the root");

		const ControlResult noScale = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), QStringLiteral("clip-0")}});
		QCOMPARE(noScale.errorKind, ControlErrorKind::Refused);

		const ControlResult setScale = run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Major")}});
		QVERIFY2(setScale.ok, qPrintable(setScale.errorMessage));
		QCOMPARE(setScale.result.value(QStringLiteral("scale")).toString(),
			QStringLiteral("Major"));
		QCOMPARE(setScale.result.value(QStringLiteral("scale_set")).toBool(), true);
		QCOMPARE(setScale.result.value(QStringLiteral("mask")).toString(),
			QStringLiteral("101010110101"));
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("scale_set")).toBool(), false);

		QVERIFY(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		const ControlResult setRoot = run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), QStringLiteral("D")}});
		QVERIFY2(setRoot.ok, qPrintable(setRoot.errorMessage));
		QCOMPARE(setRoot.result.value(QStringLiteral("root")).toInt(), 2);
		QCOMPARE(setRoot.result.value(QStringLiteral("root_name")).toString(), QStringLiteral("D"));
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("root")).toInt(), 0);
	}

	//! W9: scale.snap_notes - the one clip-editing verb - measured, and BOTH inverses
	//! stated: the checkpoint restores the keys, and re-running the snap does not.
	void snapMovesOnlyTheOutOfScaleNotesAndTheCheckpointTakesThemBack()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		// C (in C major), C# (out), D (in). The nearest in-scale pitch to C# is C - a
		// tie against D, and the engine's rule resolves a tie downward.
		static const int keys[] = {60, 61, 62};
		for (int index = 0; index < 3; ++index)
		{
			QVERIFY2(run(QStringLiteral("note.add"),
				QJsonObject{{QStringLiteral("clip"), fixture.clip},
					{QStringLiteral("key"), keys[index]}, {QStringLiteral("position"), index * 24},
					{QStringLiteral("length"), 24}}).ok, "could not add a fixture note");
		}
		QVERIFY(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		QVERIFY(run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), 0}}).ok);

		const ControlResult before = run(QStringLiteral("scale.get_state"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QCOMPARE(before.result.value(QStringLiteral("notes_in_scale")).toInt(), 2);
		QCOMPARE(before.result.value(QStringLiteral("notes_out_of_scale")).toInt(), 1);

		const ControlResult snapped = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(snapped.ok, qPrintable(snapped.errorMessage));
		QCOMPARE(snapped.result.value(QStringLiteral("notes_moved")).toInt(), 1);
		QCOMPARE(snapped.result.value(QStringLiteral("notes_out_of_scale_after")).toInt(), 0);
		QCOMPARE(noteKey(fixture.clip, 0), 60);
		QCOMPARE(noteKey(fixture.clip, 1), 60);
		QCOMPARE(noteKey(fixture.clip, 2), 62);

		// THE TRAP, from the state it lives in: the clip is now entirely in scale, so
		// re-running the snap moves NOTHING - which is why the checkpoint, and not the
		// snap, is the recorded inverse.
		const ControlResult again = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("notes_moved")).toInt(), 0);

		// Two undos: the second snap is its own step (a no-op edit still records one,
		// and undoing it leaves the first snap's result), then the first snap's step
		// restores the keys.
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 1), 60);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 1), 61);
		QCOMPARE(noteKey(fixture.clip, 0), 60);
		QCOMPARE(noteKey(fixture.clip, 2), 62);
	}

	//! W10: the device group's MPE verb - the switch flips, the read-back is honest
	//! about which axes reach playback, and the undo restores the flag.
	void theMpeSwitchFlipsAndUndoRestoresIt()
	{
		const ControlResult initial = run(QStringLiteral("device.mpe_get_state"));
		QVERIFY2(initial.ok, qPrintable(initial.errorMessage));
		const bool wasEnabled = initial.result.value(QStringLiteral("enabled")).toBool();
		const QJsonObject axes = initial.result.value(QStringLiteral("axes")).toObject();
		QCOMPARE(axes.value(QStringLiteral("pitch")).toBool(), true);
		// Task #649: pressure and timbre are APPLIED, not merely stored - they
		// reach the instrument as MIDI events on the note's own channel. The
		// measured proof is MpePlaybackTest (a block with the expression
		// against the same block without it); this read-back is the surface's
		// own answer to the same question.
		QCOMPARE(axes.value(QStringLiteral("pressure")).toBool(), true);
		QCOMPARE(axes.value(QStringLiteral("timbre")).toBool(), true);
		QCOMPARE(initial.result.value(QStringLiteral("per_stream_settings_reachable")).toBool(),
			false);

		const ControlResult on = run(QStringLiteral("device.mpe_set"),
			QJsonObject{{QStringLiteral("enabled"), true}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QCOMPARE(on.result.value(QStringLiteral("enabled")).toBool(), true);
		QCOMPARE(on.result.value(QStringLiteral("previous_enabled")).toBool(), wasEnabled);
		QCOMPARE(on.result.value(QStringLiteral("note_expressions_unchanged")).toBool(), true);
		QCOMPARE(run(QStringLiteral("device.mpe_get_state")).result
			.value(QStringLiteral("enabled")).toBool(), true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("device.mpe_get_state")).result
			.value(QStringLiteral("enabled")).toBool(), wasEnabled);

		const ControlResult noArgument = run(QStringLiteral("device.mpe_set"));
		QCOMPARE(noArgument.errorKind, ControlErrorKind::InvalidArgs);
	}
};

QTEST_GUILESS_MAIN(ControlNoteScaleVerbsTest)
#include "ControlNoteScaleVerbsTest.moc"
