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

#include <cmath>
#include <cfenv>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlScaleShared.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ProjectKey.h"
#include "ReversibilityTestSupport.h"
#include "Scale.h"
#include "Song.h"

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

bool makeOneNoteFixture(Fixture* fixture)
{
	if (!makeFixture(fixture)) { return false; }
	return run(QStringLiteral("note.add"),
		QJsonObject{{QStringLiteral("clip"), fixture->clip}, {QStringLiteral("key"), 60},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24},
			{QStringLiteral("velocity"), 100}}).ok;
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

	void scaleLoadIgnoresUnexpectedChildrenAndMalformedIntervals()
	{
		QDomDocument document;
		QDomElement scaleElement = document.createElement(QStringLiteral("scale"));
		document.appendChild(scaleElement);

		QDomElement interval = document.createElement(QStringLiteral("interval"));
		interval.setAttribute(QStringLiteral("num"), QStringLiteral("3"));
		interval.setAttribute(QStringLiteral("den"), QStringLiteral("2"));
		scaleElement.appendChild(interval);
		QDomElement centsInterval = document.createElement(QStringLiteral("interval"));
		centsInterval.setAttribute(QStringLiteral("cents"), QStringLiteral("100"));
		scaleElement.appendChild(centsInterval);
		scaleElement.appendChild(document.createComment(QStringLiteral("ignored")));
		scaleElement.appendChild(document.createTextNode(QStringLiteral("ignored")));
		scaleElement.appendChild(document.createElement(QStringLiteral("metadata")));
		scaleElement.appendChild(document.createElement(QStringLiteral("interval")));
		QDomElement incompleteInterval = document.createElement(QStringLiteral("interval"));
		incompleteInterval.setAttribute(QStringLiteral("num"), QStringLiteral("3"));
		scaleElement.appendChild(incompleteInterval);
		QDomElement invalidCentsInterval = document.createElement(QStringLiteral("interval"));
		invalidCentsInterval.setAttribute(QStringLiteral("cents"), QStringLiteral("invalid"));
		scaleElement.appendChild(invalidCentsInterval);
		QDomElement nonFiniteCentsInterval = document.createElement(QStringLiteral("interval"));
		nonFiniteCentsInterval.setAttribute(QStringLiteral("cents"), QStringLiteral("nan"));
		scaleElement.appendChild(nonFiniteCentsInterval);

		Scale scale;
		scale.loadSettings(scaleElement);

		QCOMPARE(static_cast<int>(scale.getIntervals().size()), 2);
		QCOMPARE(scale.getIntervals().front().getRatio(), 1.5f);
		QCOMPARE(scale.getIntervals().at(1).getRatio(), Interval(100.0f).getRatio());
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

	//! Luna cluster new-p19-7: the recorded randomize inverse is the argument-free
	//! control.undo operation, and replaying it restores the notes.
	void randomizeInverseCanBeReissuedThroughControlUndo()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		QVERIFY2(addRollFixtureNotes(fixture.clip), "could not add the roll fixture's notes");
		const QJsonArray before = run(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}}).result
			.value(QStringLiteral("notes")).toArray();

		const ControlResult rolled = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 1.0}, {QStringLiteral("seed"), 424242}});
		QVERIFY2(rolled.ok, qPrintable(rolled.errorMessage));
		QVERIFY(rolled.result.value(QStringLiteral("velocities_moved")).toInt() > 0);
		const ControlRegistry::Transaction* transaction = ControlRegistry::instance()->lastTransaction();
		QVERIFY(transaction != nullptr);
		QCOMPARE(transaction->command, QStringLiteral("note.randomize"));
		const QString inverseOp = transaction->inverse.value(QStringLiteral("op")).toString();
		const QJsonObject inverseArgs = transaction->inverse.value(QStringLiteral("args")).toObject();
		const ControlResult undone = ControlRegistry::instance()->invoke(inverseOp, inverseArgs);
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(inverseOp, QStringLiteral("control.undo"));
		QVERIFY(inverseArgs.isEmpty());
		const QJsonArray after = run(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}}).result
			.value(QStringLiteral("notes")).toArray();
		QCOMPARE(after, before);
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
		// Luna cluster 3918: the clip HAS notes, but none is selected, so scope
		// 'selection' is empty. The roll took a checkpoint and answered
		// notes_rolled:0 rather than refusing - "I edited nothing" and "there was
		// nothing to edit" have to be different answers, as note.transform already
		// makes them (ControlCommandsNoteTransform.cpp:110-117).
		const ControlResult emptyScope = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.5},
				{QStringLiteral("scope"), QStringLiteral("selection")}});
		QCOMPARE(emptyScope.errorKind, ControlErrorKind::Refused);
	}

	//! Luna cluster new-p8-41: NaN jitter is not a number this engine can mean. It
	//! passes the schema's "number" type check (isDouble is true for NaN) and BOTH
	//! range comparisons (every comparison with NaN is false), so it reached the
	//! roll, whose `> 0` guards then skipped the edits while the handler still
	//! checkpointed and answered success - a no-op reported as an edit.
	//! Non-finite jitter is refused with a typed InvalidArgs instead.
	void nonfiniteJitterIsRefused()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		QVERIFY2(addRollFixtureNotes(fixture.clip), "could not add the roll fixture's notes");
		const double nan = std::nan("0");
		const ControlResult velocityNan = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), nan}});
		QVERIFY2(!velocityNan.ok, "NaN velocity_jitter was accepted as a successful no-op roll");
		QCOMPARE(velocityNan.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult positionNan = run(QStringLiteral("note.randomize"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("velocity_jitter"), 0.5},
				{QStringLiteral("position_jitter"), nan}});
		QVERIFY2(!positionNan.ok, "NaN position_jitter was accepted as a successful no-op roll");
		QCOMPARE(positionNan.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! Luna cluster new-p22-42: a handler call that bypasses schema validation must
	//! still reject NaN before converting it to the engine's uint32 seed.
	void nonfiniteSeedIsRefusedByRandomizeHandler()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		QVERIFY2(addRollFixtureNotes(fixture.clip), "could not add the roll fixture's notes");
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* command = registry->command(QStringLiteral("note.randomize"));
		QVERIFY(command != nullptr);
		QVERIFY(static_cast<bool>(command->handler));
		const double nan = std::nan("0");
		const ControlResult rejected = command->handler(QJsonObject{
			{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("velocity_jitter"), 0.5},
			{QStringLiteral("seed"), nan}});
		QVERIFY2(!rejected.ok, "note.randomize handler accepted a NaN seed");
		QCOMPARE(rejected.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! The same readSeed helper serves random_seed_set, so its direct-handler path
	//! must reject NaN without changing the persisted project seed.
	void nonfiniteSeedIsRefusedBySeedSetHandler()
	{
		const double previous = run(QStringLiteral("note.random_seed_get")).result
			.value(QStringLiteral("seed")).toDouble();
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* command = registry->command(QStringLiteral("note.random_seed_set"));
		QVERIFY(command != nullptr);
		QVERIFY(static_cast<bool>(command->handler));
		const ControlResult rejected = command->handler(
			QJsonObject{{QStringLiteral("seed"), std::nan("0")}});
		QVERIFY2(!rejected.ok, "note.random_seed_set handler accepted a NaN seed");
		QCOMPARE(rejected.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("note.random_seed_get")).result
			.value(QStringLiteral("seed")).toDouble(), previous);
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

	//! Luna cluster 3904: slideClear took the MidiClip journal checkpoint before it knew
	//! whether anything would change, so clearing a scope with no slide notes in it answered
	//! slides_cleared:0 AND pushed a no-op onto the undo stack - and published dataChanged()
	//! only in the changed case, so the checkpoint was the only trace of an edit that never
	//! happened. An undo after that has to unwind nothing, but it consumed a step.
	void clearingSlidesOnAClipWithoutAnyRecordsNoUndoStep()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		const ControlResult added = run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		const ControlResult cleared = run(QStringLiteral("note.slide_clear"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("slides_before")).toInt(), 0);
		QCOMPARE(cleared.result.value(QStringLiteral("slides_cleared")).toInt(), 0);
		QCOMPARE(journal->undoDepth(), depthBefore);

		// The same call on a clip that DOES hold a slide note still takes its one step.
		const QString note = added.result.value(QStringLiteral("note")).toString();
		QVERIFY(run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("note"), note},
				{QStringLiteral("slide"), true}}).ok);
		const int depthWithSlide = journal->undoDepth();
		QVERIFY(run(QStringLiteral("note.slide_clear"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}}).ok);
		QCOMPARE(journal->undoDepth(), depthWithSlide + 1);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("slide")).toBool(), true);
	}

	//! A zero semitone shift must not leave a clip checkpoint to undo.
	void zeroSemitoneTransposeDoesNotTakeUndoStep()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		const ControlResult transposed = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("semitones"), 0}});
		QVERIFY2(transposed.ok, qPrintable(transposed.errorMessage));
		QCOMPARE(transposed.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		const int depthAfter = journal->undoDepth();
		while (journal->undoDepth() > depthBefore) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(depthAfter, depthBefore);
	}

	//! A zero velocity delta must not leave a clip checkpoint to undo.
	void zeroVelocityOffsetDoesNotTakeUndoStep()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		const ControlResult offset = run(QStringLiteral("note.velocity_offset"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("delta"), 0}});
		QVERIFY2(offset.ok, qPrintable(offset.errorMessage));
		QCOMPARE(offset.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		const int depthAfter = journal->undoDepth();
		while (journal->undoDepth() > depthBefore) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(depthAfter, depthBefore);
	}

	//! An identity velocity scale must not leave a clip checkpoint to undo.
	void identityVelocityScaleDoesNotTakeUndoStep()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		const ControlResult scaled = run(QStringLiteral("note.velocity_scale"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("factor"), 1.0}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(scaled.result.value(QStringLiteral("notes_changed")).toInt(), 0);
		const int depthAfter = journal->undoDepth();
		while (journal->undoDepth() > depthBefore) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(depthAfter, depthBefore);
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
				{QStringLiteral("notes"), QJsonArray{added.result.value(QStringLiteral("note"))}}}).ok,
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

	//! Luna cluster new-p19-7: transform transactions also record an argument-free
	//! control.undo inverse, which restores the transformed note through validation.
	void transformInverseCanBeReissuedThroughControlUndo()
	{
		Fixture fixture;
		QVERIFY2(makeOneNoteFixture(&fixture), "could not build a one-note fixture");
		const auto undoAsRecorded = [](const QString& command) {
			ControlRegistry* registry = ControlRegistry::instance();
			const ControlRegistry::Transaction* transaction = registry->lastTransaction();
			if (transaction == nullptr || transaction->command != command) { return false; }
			const QString inverseOp = transaction->inverse.value(QStringLiteral("op")).toString();
			const QJsonObject inverseArgs = transaction->inverse.value(QStringLiteral("args")).toObject();
			const ControlResult undone = registry->invoke(inverseOp, inverseArgs);
			return undone.ok && inverseOp == QLatin1String("control.undo") && inverseArgs.isEmpty();
		};

		const ControlResult transposed = run(QStringLiteral("note.transpose"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("semitones"), 12}});
		QVERIFY2(transposed.ok, qPrintable(transposed.errorMessage));
		QCOMPARE(noteKey(fixture.clip, 0), 72);
		QVERIFY2(undoAsRecorded(QStringLiteral("note.transpose")),
			"the transpose's recorded control.undo failed validation or retained arguments");
		QCOMPARE(noteKey(fixture.clip, 0), 60);

		const ControlResult offset = run(QStringLiteral("note.velocity_offset"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("delta"), 25}});
		QVERIFY2(offset.ok, qPrintable(offset.errorMessage));
		QCOMPARE(noteVelocity(fixture.clip, 0), 125.0);
		QVERIFY2(undoAsRecorded(QStringLiteral("note.velocity_offset")),
			"the velocity-offset control.undo inverse retained arguments or failed");
		QCOMPARE(noteVelocity(fixture.clip, 0), 100.0);

		const ControlResult scaled = run(QStringLiteral("note.velocity_scale"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}, {QStringLiteral("factor"), 0.5}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(noteVelocity(fixture.clip, 0), 50.0);
		QVERIFY2(undoAsRecorded(QStringLiteral("note.velocity_scale")),
			"the velocity-scale control.undo inverse retained arguments or failed");
		QCOMPARE(noteVelocity(fixture.clip, 0), 100.0);
	}

	//! A hand-edited scales/custom entry must not turn into a bogus "degree 0" scale.
	void malformedCustomScaleEntriesAreIgnored()
	{
		const QJsonObject before = lmms::control::customScales();
		QJsonObject edited = before;
		// an unconvertible value used toInt() into 0; a fractional one was truncated
		edited.insert(QStringLiteral("Odd"), QJsonArray{true, QStringLiteral("nope")});
		edited.insert(QStringLiteral("Ragged"), QJsonArray{0, 12, 99, 4});
		edited.insert(QStringLiteral("Huge"), QJsonArray{1.0e300, 2});
		// extreme negative, fractional and non-finite-looking inputs: only the whole 0..11 degree survives
		edited.insert(QStringLiteral("Extremes"), QJsonArray{-1.0e300, 2.5, 7, -0.5, 11.5, 12.0, 7});
		lmms::control::setCustomScales(edited);

		QVERIFY(lmms::control::scaleDegreesOf(QStringLiteral("Odd")).empty());  // nothing valid: not a "C" scale
		const std::vector<int> ragged = lmms::control::scaleDegreesOf(QStringLiteral("Ragged"));
		QCOMPARE(ragged.size(), size_t(2));  // only 0 and 4 survive the 0..11 window
		QCOMPARE(ragged[0], 0);
		QCOMPARE(ragged[1], 4);

		const std::vector<int> huge = lmms::control::scaleDegreesOf(QStringLiteral("Huge"));
		QCOMPARE(huge.size(), size_t(1));
		QCOMPARE(huge[0], 2);

		// -1e300, 2.5, -0.5, 11.5 and 12.0 are all refused; only the two whole 7s survive
		const std::vector<int> extremes = lmms::control::scaleDegreesOf(QStringLiteral("Extremes"));
		QCOMPARE(extremes.size(), size_t(2));
		QCOMPARE(extremes[0], 7);
		QCOMPARE(extremes[1], 7);

		// Keep the FP-exception check separate from 1e300: Qt's JSON parser may
		// itself raise FE_INVALID while decoding an exponent that large. This
		// fixture isolates the product's out-of-range casts, one past INT_MAX and
		// one past INT_MIN, both of which would raise FE_INVALID before the range check.
		QJsonObject overflowEdited = before;
		overflowEdited.insert(QStringLiteral("Overflow"), QJsonArray{2147483648.0, -2147483649.0, 2});
		lmms::control::setCustomScales(overflowEdited);

		// On Linux x86-64 the overflowing SSE conversion sets FE_INVALID. Check
		// that flag around the lookup so this regression detects the undefined
		// conversion even though its result is discarded by the scale validator.
#if defined(__linux__) && defined(__x86_64__)
		std::fenv_t originalEnvironment;
		QVERIFY(std::fegetenv(&originalEnvironment) == 0);
		QVERIFY(std::feclearexcept(FE_ALL_EXCEPT) == 0);
#endif
		const std::vector<int> overflow = lmms::control::scaleDegreesOf(QStringLiteral("Overflow"));
#if defined(__linux__) && defined(__x86_64__)
		const int invalidExceptions = std::fetestexcept(FE_INVALID);
		QVERIFY(std::fesetenv(&originalEnvironment) == 0);
#endif
		lmms::control::setCustomScales(before);
#if defined(__linux__) && defined(__x86_64__)
		QCOMPARE(invalidExceptions, 0);
#endif
		QCOMPARE(overflow.size(), size_t(1));
		QCOMPARE(overflow[0], 2);

		// a well-formed custom scale still wins over the engine's table
		QJsonObject good = edited;
		good.insert(QStringLiteral("Mine"), QJsonArray{0, 5, 7});
		lmms::control::setCustomScales(good);
		const std::vector<int> mine = lmms::control::scaleDegreesOf(QStringLiteral("Mine"));
		QCOMPARE(mine.size(), size_t(3));
		QCOMPARE(mine[1], 5);
		lmms::control::setCustomScales(before);
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
			// Index 0 = C: the major scale is C, D, E, F, G, A, B = ChordTable's own scale row {0,2,4,5,7,9,11} (InstrumentFunctions.cpp, the isScale() entries), so bits 5 and 7 are set; "101010110101" is the LYDIAN row {0,2,4,6,7,9,11} and was this expectation until 2026-09-16.
			QCOMPARE(entry.value(QStringLiteral("mask")).toString(),
				QStringLiteral("101011010101"));
		}
		QVERIFY2(sawMajor, "the engine's own scale vocabulary must carry Major");
		QCOMPARE(listed.result.value(QStringLiteral("roots")).toArray().size(), 12);

		// include_chords ADDS the chords to the scales - it must not replace the
		// scales with the chords.
		const ControlResult withChords = run(QStringLiteral("scale.list"),
			QJsonObject{{QStringLiteral("include_chords"), true}});
		QVERIFY2(withChords.ok, qPrintable(withChords.errorMessage));
		const QJsonArray mixed = withChords.result.value(QStringLiteral("scales")).toArray();
		int sawScale = 0;
		int sawChord = 0;
		for (const QJsonValue& value : mixed)
		{
			if (value.toObject().value(QStringLiteral("is_scale")).toBool()) { ++sawScale; }
			else { ++sawChord; }
		}
		QVERIFY2(sawScale >= 10,
			qPrintable(QStringLiteral("include_chords:true returned only %1 scales - the "
				"option ADDS chords to the scales").arg(sawScale)));
		QVERIFY2(sawChord > 0, "include_chords:true returned no chords at all");
		QCOMPARE(mixed.size(), sawScale + sawChord);

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

	//! W8: the scale context writers, their inverses, and the refusal a snap makes when
	//! no scale is set at all. The mask is the same ChordTable Major row as above.
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
			QStringLiteral("101011010101"));
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

		// ONE undo: the second snap moved nothing, so it took no step of its own
		// (Luna cluster new-p14-72) and the step under it is still the first snap's -
		// which restores the keys.
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 1), 61);
		QCOMPARE(noteKey(fixture.clip, 0), 60);
		QCOMPARE(noteKey(fixture.clip, 2), 62);
	}

	//! Luna cluster new-p14-72: a snap that moves NOTHING must take no undo step.
	//! The empty step it used to push sat ON TOP of the first snap's, so an undo
	//! after a second snap unwound the no-op - the clip stayed snapped and the real
	//! edit needed one undo too many.
	void aSecondSnapOfAClipAlreadyInScaleTakesNoUndoStep()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		// C (in C major), C# (out), D (in).
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

		const ControlResult snapped = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(snapped.ok, qPrintable(snapped.errorMessage));
		QCOMPARE(snapped.result.value(QStringLiteral("notes_moved")).toInt(), 1);

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthAfterTheSnap = journal->undoDepth();

		// The clip is in scale now: the second snap moves nothing, so it must leave
		// the undo stack exactly as it found it.
		const ControlResult again = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("notes_moved")).toInt(), 0);
		QCOMPARE(journal->undoDepth(), depthAfterTheSnap);

		// And the step that IS there is the first snap's: one undo takes the keys back.
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteKey(fixture.clip, 1), 61);
		QCOMPARE(noteKey(fixture.clip, 0), 60);
		QCOMPARE(noteKey(fixture.clip, 2), 62);
	}

	//! W9b: the empty-clip refusal of scale.snap_notes (Luna cluster new-p11-48). An
	//! empty clip with the DEFAULT scope 'clip' was blamed on the selection scope -
	//! "none is selected, so scope 'selection' would edit nothing" - which names a
	//! reason that was never asked for. note.transform already distinguishes the two
	//! empties (ControlCommandsNoteTransform.cpp:105-110); the snap must too.
	void snapOnAnEmptyClipNamesTheEmptyClipAsTheReason()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture), "could not build a track + clip fixture");
		// Set the scale first, so the refusal below is about the clip, not about a
		// missing scale context.
		QVERIFY2(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Major")}}).ok,
			"could not set the scale context");
		const ControlResult emptyClip = run(QStringLiteral("scale.snap_notes"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QCOMPARE(emptyClip.errorKind, ControlErrorKind::Refused);
		QVERIFY2(emptyClip.errorMessage.contains(QStringLiteral("no notes")),
			"the refusal must say the clip has no notes");
		QVERIFY2(!emptyClip.errorMessage.contains(QStringLiteral("none is selected")),
			"an empty clip must not be blamed on the selection scope");
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

	//! A call that changes NOTHING must not take an undo step. device.mpe_set
	//! answered `changed: false` for a value the flag already had, and still
	//! recorded a step - so the next control.undo unwound that no-op instead of
	//! the real edit behind it, and the edit stayed applied.
	void theMpeSwitchTakesNoUndoStepForAValueItAlreadyHas()
	{
		const ControlResult initial = run(QStringLiteral("device.mpe_get_state"));
		QVERIFY2(initial.ok, qPrintable(initial.errorMessage));
		const bool wasEnabled = initial.result.value(QStringLiteral("enabled")).toBool();

		// A real edit first, so there IS a step for a no-op to shadow.
		const bool edited = !wasEnabled;
		const ControlResult changed = run(QStringLiteral("device.mpe_set"),
			QJsonObject{{QStringLiteral("enabled"), edited}});
		QVERIFY2(changed.ok, qPrintable(changed.errorMessage));
		QCOMPARE(changed.result.value(QStringLiteral("changed")).toBool(), true);

		const ControlResult same = run(QStringLiteral("device.mpe_set"),
			QJsonObject{{QStringLiteral("enabled"), edited}});
		QVERIFY2(same.ok, qPrintable(same.errorMessage));
		QCOMPARE(same.result.value(QStringLiteral("changed")).toBool(), false);
		QCOMPARE(same.result.value(QStringLiteral("previous_enabled")).toBool(), edited);

		// ONE undo must take the real edit back, not the no-op.
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("device.mpe_get_state")).result
			.value(QStringLiteral("enabled")).toBool(), wasEnabled);
	}

	//! A seed the project already holds changes nothing, so it takes NO undo step.
	//! note.random_seed_set recorded one anyway, and the next control.undo unwound
	//! that no-op instead of the real seed edit behind it, leaving the edit applied.
	void theUnchangedSeedTakesNoUndoStep()
	{
		const ControlResult initial = run(QStringLiteral("note.random_seed_get"));
		QVERIFY2(initial.ok, qPrintable(initial.errorMessage));
		const double original = initial.result.value(QStringLiteral("seed")).toDouble();

		// A real edit first, so there IS a step for a no-op to shadow.
		const double edited = original == 424242.0 ? 515151.0 : 424242.0;
		const ControlResult changed = run(QStringLiteral("note.random_seed_set"),
			QJsonObject{{QStringLiteral("seed"), edited}});
		QVERIFY2(changed.ok, qPrintable(changed.errorMessage));
		QCOMPARE(changed.result.value(QStringLiteral("changed")).toBool(), true);

		const ControlResult same = run(QStringLiteral("note.random_seed_set"),
			QJsonObject{{QStringLiteral("seed"), edited}});
		QVERIFY2(same.ok, qPrintable(same.errorMessage));
		QCOMPARE(same.result.value(QStringLiteral("changed")).toBool(), false);
		QCOMPARE(same.result.value(QStringLiteral("previous")).toDouble(), edited);

		// ONE undo must take the real edit back, not the no-op.
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("note.random_seed_get")).result
			.value(QStringLiteral("seed")).toDouble(), original);
	}

	//! scale.from_detected_key's ADVERTISED inverse must be able to restore the
	//! context it replaced. Replaying the forward command as its own inverse
	//! cannot: that handler ignores the recorded root/scale and re-reads the
	//! project key, so the replay re-applies the detected key instead of the
	//! saved pair.
	void theDetectedKeyVerbAdvertisesAnInverseThatRestoresTheContext()
	{
		// A known context to be replaced, and a project key that names another.
		QVERIFY2(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("")}}).ok,
			"could not clear the scale");
		QVERIFY2(run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), 0}}).ok, "could not set the root");
		QVERIFY2(run(QStringLiteral("scale.root_set"),
			QJsonObject{{QStringLiteral("root"), 4}}).ok,
			"could not set the starting root (E)");
		QVERIFY2(run(QStringLiteral("scale.set"),
			QJsonObject{{QStringLiteral("scale"), QStringLiteral("Minor")}}).ok,
			"could not set the starting scale");

		Engine::getSong()->projectKey().set(QStringLiteral("D"), 2,
			QStringLiteral("Major"), 1.0, 0.5, QStringLiteral("test"),
			QStringLiteral("/tmp/test-key.json"));

		const ControlResult applied = run(QStringLiteral("scale.from_detected_key"));
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("root")).toInt(), 2);
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("scale")).toString(), QStringLiteral("Major"));

		// The context the command replaced is E minor - what the advertised
		// inverse must put back, however the project key stands now.
		const QJsonObject transaction = stateOf(QStringLiteral("scale.from_detected_key"));
		QVERIFY2(!transaction.isEmpty(),
			"scale.from_detected_key recorded no transaction to replay");
		const QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
		const QString inverseOp = inverse.value(QStringLiteral("op")).toString();
		const QJsonObject inverseArgs = inverse.value(QStringLiteral("args")).toObject();
		const ControlResult replayed = run(inverseOp, inverseArgs);
		QVERIFY2(replayed.ok, qPrintable(QStringLiteral("the advertised inverse '%1' refused: "
			"%2").arg(inverseOp, replayed.errorMessage)));
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("root")).toInt(), 4);
		QCOMPARE(run(QStringLiteral("scale.get_state")).result
			.value(QStringLiteral("scale")).toString(), QStringLiteral("Minor"));

		REV_UNDO_OR_FAIL();
	}
};

QTEST_GUILESS_MAIN(ControlNoteScaleVerbsTest)
#include "ControlNoteScaleVerbsTest.moc"
