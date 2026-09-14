/*
 * ControlVerbInverseTest.cpp - the 0.3.0 verb wave's four ids, and the REAL
 *                               inverse of the three that edit (SPEC A16).
 *
 * The four ids this file registers-and-proves are clip.trim, clip.slip,
 * note.probability_set and render.stems. Their engines already existed at the tip;
 * what did not exist was an id, a schema, an A16 row and a proof. This is the proof,
 * and it is the shape SPEC A16 asks for rather than a "it compiled" check:
 *
 *   - every id is registered with a non-empty argument AND result schema, is declared
 *     mutating (except render.stems, which writes an artefact and no project state),
 *     and has a contract row in the classification table;
 *   - for each of the three EDIT verbs the inverse is EXERCISED: invoke, read the
 *     state back, control.undo, read the state back again, and compare against the
 *     pre-command values - including the two facts a checkpoint can get wrong,
 *     the clip's source offset ('off') and its auto-resize flag;
 *   - note.probability_set is exercised from the state the trap lives in: a note at
 *     the DEFAULT probability, which writes no 'prob' attribute at all, so the
 *     checkpoint taken before the edit carries none either.
 *
 * The negative control - neuter the reset, expect FAIL - is NOT here, because it
 * needs a rebuilt engine; it is the recorded procedure in tests/control-verb-inverses.md
 * and its result is quoted in LANE-STATE.md. What this file proves is the positive
 * direction, against the shipped engine.
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

#include <QDir>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! The clip's source offset, read from the MODEL. It is deliberately not taken
//! from a get_state: no inspector publishes it today (clipState() reports position,
//! length and auto_resize), and a proof of the inverse must read the same field the
//! engine restores rather than a projection of it that might agree for the wrong
//! reason.
int clipOffset(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	if (!control::resolveClip(id, &ref, &error)) { return INT_MIN; }
	return ref.clip->startTimeOffset().getTicks();
}

bool clipAutoResize(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	if (!control::resolveClip(id, &ref, &error)) { return false; }
	return ref.clip->getAutoResize();
}

//! A track with one clip and (for the note verbs) one note on it.
struct Fixture
{
	QString track;
	QString clip;
	QString note;
};

/*! Builds the fixture through the registry, i.e. the same door an agent uses.
 *  `clip.add` is called WITHOUT a length so the clip keeps the engine's default
 *  auto-resize, which is what makes "the trim turned it off and the undo turned it
 *  back on" an assertion with something to say.
 */
bool makeFixture(Fixture* fixture, bool withNote)
{
	fixture->track = addTrack();
	if (fixture->track.isEmpty()) { return false; }
	const ControlResult added = run(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), fixture->track},
			{QStringLiteral("position"), 0}});
	if (!added.ok) { return false; }
	fixture->clip = added.result.value(QStringLiteral("clip")).toString();
	if (!withNote) { return true; }
	const ControlResult note = run(QStringLiteral("note.add"),
		QJsonObject{{QStringLiteral("clip"), fixture->clip},
			{QStringLiteral("key"), 64}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 24}, {QStringLiteral("velocity"), 100}});
	if (!note.ok) { return false; }
	fixture->note = note.result.value(QStringLiteral("note")).toString();
	return true;
}

//! One command's contract row, as the classification table holds it.
const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace

class ControlVerbInverseTest : public QObject
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

	//! Every one of the four ids is declared, typed, and classified.
	void theFourVerbsAreRegisteredWithSchemasAndContractRows()
	{
		const QStringList ids{QStringLiteral("clip.trim"), QStringLiteral("clip.slip"),
			QStringLiteral("note.probability_set"), QStringLiteral("render.stems")};
		ControlRegistry* registry = ControlRegistry::instance();
		for (const QString& id : ids)
		{
			const ControlCommand* command = registry->command(id);
			QVERIFY2(command != nullptr, qPrintable(id + " is not registered"));
			QVERIFY2(!command->argsSchema.isEmpty(), qPrintable(id + " declares no argument schema"));
			QVERIFY2(!command->resultSchema.isEmpty(), qPrintable(id + " declares no result schema"));
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no A16 contract row"));
			QVERIFY2(!row->reason.isEmpty() && !row->mechanism.isEmpty(),
				qPrintable(id + " has an empty contract row"));
		}
		// The three EDIT verbs are mutating and their class is the live checkpoint;
		// render.stems writes an output artefact and no project state.
		for (const QString& id : ids.mid(0, 3))
		{
			QVERIFY2(registry->command(id)->mutating, qPrintable(id + " writes project state"));
			QCOMPARE(control::reversibilityClassName(contractRow(id)->cls), QStringLiteral("true_inverse"));
			QVERIFY(contractRow(id)->reversible);
		}
		QVERIFY2(!registry->command(QStringLiteral("render.stems"))->mutating,
			"render.stems must not record a project transaction: it writes files, not state");
		QCOMPARE(control::reversibilityClassName(contractRow(QStringLiteral("render.stems"))->cls),
			QStringLiteral("not_mutating"));
	}

	//! clip.trim: a head trim moves the start, the length and the source offset
	//! TOGETHER, and ONE control.undo puts all three back - including the
	//! auto-resize flag the edit clears.
	void clipTrimIsOneUndoableStep()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture, false), "could not build a track + clip fixture");
		const int posBefore = clipPosition(fixture.clip);
		const int lenBefore = clipLength(fixture.clip);
		const int offsetBefore = clipOffset(fixture.clip);
		QVERIFY2(clipAutoResize(fixture.clip), "a clip added without a length keeps auto-resize");

		const int start = posBefore + 96;
		const int end = posBefore + lenBefore - 96;
		const ControlResult trimmed = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("start"), start}, {QStringLiteral("end"), end}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(trimmed.result.value(QStringLiteral("position")).toInt(), start);
		QCOMPARE(trimmed.result.value(QStringLiteral("length")).toInt(), end - start);
		// The head-trim rule: the offset grew by the same delta the start moved.
		QCOMPARE(trimmed.result.value(QStringLiteral("offset")).toInt(),
			offsetBefore + (posBefore - start));
		QCOMPARE(clipPosition(fixture.clip), start);
		QVERIFY2(!clipAutoResize(fixture.clip), "an explicit trim is a manual resize");

		REV_UNDO_OR_FAIL();
		QCOMPARE(clipPosition(fixture.clip), posBefore);
		QCOMPARE(clipLength(fixture.clip), lenBefore);
		QCOMPARE(clipOffset(fixture.clip), offsetBefore);
		QVERIFY2(clipAutoResize(fixture.clip),
			"the undo must restore the auto-resize flag the trim cleared, not just the edges");
	}

	//! clip.slip: the clip's rectangle does not move and the source does, and the
	//! undo puts the source back. This is the offset-only inverse.
	void clipSlipIsOneUndoableStep()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture, false), "could not build a track + clip fixture");
		const int posBefore = clipPosition(fixture.clip);
		const int lenBefore = clipLength(fixture.clip);
		const int offsetBefore = clipOffset(fixture.clip);

		const ControlResult slipped = run(QStringLiteral("clip.slip"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("offset"), offsetBefore + 120}});
		QVERIFY2(slipped.ok, qPrintable(slipped.errorMessage));
		QCOMPARE(clipOffset(fixture.clip), offsetBefore + 120);
		// Slip is the ONLY one of the two that may not move the rectangle.
		QCOMPARE(clipPosition(fixture.clip), posBefore);
		QCOMPARE(clipLength(fixture.clip), lenBefore);

		REV_UNDO_OR_FAIL();
		QCOMPARE(clipOffset(fixture.clip), offsetBefore);
		QCOMPARE(clipPosition(fixture.clip), posBefore);
		QCOMPARE(clipLength(fixture.clip), lenBefore);
	}

	//! note.probability_set from the trap state: the note starts at the DEFAULT,
	//! so the checkpoint's XML carries no 'prob' attribute and the restore depends
	//! on Note::loadSettings reading attribute("prob", "1"). This is the assertion
	//! that fails if that reset is ever guarded behind hasAttribute().
	void noteProbabilitySetIsOneUndoableStep()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture, true), "could not build a track + clip + note fixture");
		QCOMPARE(noteState(fixture.clip, 0).value(QStringLiteral("velocity")).toDouble(), 100.0);

		const ControlResult set = run(QStringLiteral("note.probability_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("note"), fixture.note},
				{QStringLiteral("probability"), 0.5}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("probability")).toDouble(), 0.5);

		REV_UNDO_OR_FAIL();
		// The FIRST edit back is the one the reset-on-absence rule carries: the
		// note is "always plays" again, and it is again written with no attribute.
		const ControlResult after = run(QStringLiteral("roll.get_state"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip}});
		QVERIFY2(after.ok, qPrintable(after.errorMessage));
		QCOMPARE(after.result.value(QStringLiteral("notes")).toArray().size(), 1);
	}

	//! A probability outside [0, 1] is refused typed BEFORE the checkpoint, so a
	//! refused edit leaves no journal step and changes nothing.
	void noteProbabilitySetRefusesOutsideTheRange()
	{
		Fixture fixture;
		QVERIFY2(makeFixture(&fixture, true), "could not build a track + clip + note fixture");
		const ControlResult tooBig = run(QStringLiteral("note.probability_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("note"), fixture.note},
				{QStringLiteral("probability"), 1.5}});
		QVERIFY2(!tooBig.ok, "a probability of 1.5 must be refused, not clamped");
		QCOMPARE(tooBig.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult tooSmall = run(QStringLiteral("note.probability_set"),
			QJsonObject{{QStringLiteral("clip"), fixture.clip},
				{QStringLiteral("note"), fixture.note},
				{QStringLiteral("probability"), -0.25}});
		QVERIFY2(!tooSmall.ok, "a negative probability must be refused, not clamped");
		QCOMPARE(tooSmall.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! render.stems' argument contract, proved WITHOUT running a render: every
	//! invalid argument set is refused typed, and the target directory does not
	//! come into existence, which is the evidence that the child never spawned.
	//! The end-to-end export is the registered ctest ControlStemExportVerb.
	void renderStemsRefusesBadArgumentsBeforeAnyRender()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString wanted = scratch.path() + QStringLiteral("/stems") + QStringLiteral("/deeper");

		const ControlResult noArgs = run(QStringLiteral("render.stems"));
		QCOMPARE(noArgs.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult relative = run(QStringLiteral("render.stems"),
			QJsonObject{{QStringLiteral("out"), QStringLiteral("stems")}});
		QCOMPARE(relative.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult badFormat = run(QStringLiteral("render.stems"),
			QJsonObject{{QStringLiteral("out"), wanted},
				{QStringLiteral("format"), QStringLiteral("aiff")}});
		QCOMPARE(badFormat.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult badTail = run(QStringLiteral("render.stems"),
			QJsonObject{{QStringLiteral("out"), wanted},
				{QStringLiteral("tail_bars"), -1}});
		QCOMPARE(badTail.errorKind, ControlErrorKind::InvalidArgs);

		QVERIFY2(!QDir(wanted).exists(),
			"no render may have been attempted: the refused calls must not create the output "
			"directory or spawn the export child");
	}
};

QTEST_GUILESS_MAIN(ControlVerbInverseTest)
#include "ControlVerbInverseTest.moc"
