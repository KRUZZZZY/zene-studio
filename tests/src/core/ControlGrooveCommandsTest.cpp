/*
 * ControlGrooveCommandsTest.cpp - the groove.* command group's SURFACE
 *                                 (SPEC-zene-studio.md A11-A16, the release
 *                                 contract section 3.1, docs/GROOVE-POOL.md).
 *
 * The ENGINE arithmetic is held to account by tests/src/core/GrooveTemplateTest.cpp
 * with no Engine at all; this file holds the SURFACE to account: seven
 * registered commands with argument and result schemas, typed refusals that
 * change nothing, and - the part that is not optional - two inverses that
 * really work, of two DIFFERENT kinds (a clip edit reverses through the
 * MidiClip's own journal checkpoint, a pool edit through a recorded action
 * checkpoint that writes the captured <groove-pool> element back).
 *
 * The numbers are asserted, not the success of a call: the note positions and
 * velocities an apply or a quantise produces are read back through
 * roll.get_state and compared with the values the arithmetic must give, and the
 * pool is read back through groove.list. The end-to-end proof that an AGENT can
 * drive all of this over a real socket is the registered ctest
 * `ControlGrooveCommands` (tests/control-groove-commands.py).
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

#include <QDomDocument>
#include <QDomNodeList>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "GroovePool.h"
#include "GrooveTemplate.h"
#include "GrooveTestSupport.h"
#include "Song.h"

using namespace lmms;
using namespace groovetest;

class ControlGrooveCommandsTest : public QObject
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

	//! Every command of the group declares the contract's parts.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("groove.extract"), QStringLiteral("groove.set"),
			QStringLiteral("groove.apply"), QStringLiteral("groove.quantize"),
			QStringLiteral("groove.remove"), QStringLiteral("groove.rename")};
		for (const QString& id : grooveIds())
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("groove"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
	}

	//! The contract table classifies the group: six true_inverse rows (two on
	//! the clip's checkpoint, four on a recorded action) and one read-only row.
	void contractRowsClassifyTheGroup()
	{
		for (const QString& id : grooveIds())
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
			QCOMPARE(control::reversibilityClassName(row->cls),
				id == QStringLiteral("groove.list") ? QStringLiteral("not_mutating")
					: QStringLiteral("true_inverse"));
		}
		QVERIFY(contractRow(QStringLiteral("groove.apply"))->mechanism.contains(
			QStringLiteral("MidiClip checkpoint")));
		QVERIFY(contractRow(QStringLiteral("groove.extract"))->mechanism.contains(
			QStringLiteral("action checkpoint")));
	}

	/*! THE EXTRACTION RULE ON THE WIRE: a clip whose notes sit 3 / +2 / -2 / +3
	 *  ticks off a 12-tick grid, with velocities 120 / 80 / 100 / 100, must read
	 *  back as exactly those numbers relative to the clip's own mean velocity -
	 *  so the template is a shape, not a loudness. */
	void extractCapturesTheClipsFeel()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 9, 120));
		QVERIFY(addNote(clip, 62, 26, 80));
		QVERIFY(addNote(clip, 64, 34, 100));
		QVERIFY(addNote(clip, 65, 51, 100));

		const ControlResult extracted = run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("feel")},
				{QStringLiteral("grid"), 12}, {QStringLiteral("length"), 48}});
		QVERIFY2(extracted.ok, qPrintable(extracted.errorMessage));
		QCOMPARE(extracted.result.value(QStringLiteral("notes_read")).toInt(), 4);
		QCOMPARE(extracted.result.value(QStringLiteral("replaced")).toBool(), false);
		QCOMPARE(extracted.result.value(QStringLiteral("count")).toInt(), projectPool().size());
		QCOMPARE(namesInPool(), QStringList{QStringLiteral("feel")});
		const QVector<NoteAt> steps = stepsOf(QStringLiteral("feel"));
		QVERIFY2((steps == QVector<NoteAt>{{3, 0}, {-3, 20}, {2, -20}, {-2, 0}}),
			qPrintable(describe(steps)));

		// A second extract over the SAME name REPLACES it: the pool still holds
		// one groove, and the result says so (the name is the key).
		const ControlResult again = run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("feel")},
				{QStringLiteral("grid"), 12}, {QStringLiteral("length"), 24}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("replaced")).toBool(), true);
		QCOMPARE(namesInPool(), QStringList{QStringLiteral("feel")});
		// The recorded inverse of a REPLACING extract is groove.set with the
		// template it replaced, not "remove the new one" - which would lose it.
		// Read from the registry's OWN record: the wire result carries no
		// "__transaction" (the registry strips it and records it).
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->command, QStringLiteral("groove.extract"));
		QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
		QCOMPARE(tx->reversible, true);
		QCOMPARE(tx->inverse.value(QStringLiteral("op")).toString(),
			QStringLiteral("groove.set"));
	}

	/*! THE APPLY IS REAL: the same groove written onto a clip that sits ON the
	 *  grid moves its notes to the groove's own ticks and velocities, and the
	 *  counts the result reports are the numbers that actually changed. */
	void applyMovesNotesReally()
	{
		QVERIFY(run(QStringLiteral("groove.set"),
			{{QStringLiteral("name"), QStringLiteral("feel")},
				{QStringLiteral("length_ticks"), 48}, {QStringLiteral("step_ticks"), 12},
				{QStringLiteral("steps"), feelSteps()}}).ok);

		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 0, 100));
		QVERIFY(addNote(clip, 62, 12, 100));
		QVERIFY(addNote(clip, 64, 24, 100));
		QVERIFY(addNote(clip, 65, 36, 100));
		QVERIFY((takeOf(clip) == QVector<NoteAt>{{0, 100}, {12, 100}, {24, 100}, {36, 100}}));

		const ControlResult applied = run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("feel")}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(applied.result.value(QStringLiteral("positions_moved")).toInt(), 4);
		QCOMPARE(applied.result.value(QStringLiteral("velocities_moved")).toInt(), 2);
		QCOMPARE(applied.result.value(QStringLiteral("notes_moved")).toInt(), 4);

		const QVector<NoteAt> after = takeOf(clip);
		QVERIFY2((after == QVector<NoteAt>{{3, 100}, {9, 120}, {26, 80}, {34, 100}}),
			qPrintable(describe(after)));
		// A second apply has nothing left to do - the groove is a fixed point.
		const ControlResult twice = run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("feel")}});
		QVERIFY2(twice.ok, qPrintable(twice.errorMessage));
		QCOMPARE(twice.result.value(QStringLiteral("notes_moved")).toInt(), 0);
		QVERIFY(takeOf(clip) == after);
	}

	/*! THE QUANTISE IS REAL, and its two knobs are two different facts: the
	 *  strength is how far a note travels (an exact number at each setting) and
	 *  the humanise is a bounded, SEEDED jitter added afterwards. */
	void quantizeStrengthAndHumaniseAreMeasured()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 5, 90));
		QVERIFY(addNote(clip, 62, 17, 130));
		QVERIFY(addNote(clip, 64, 29, 110));
		QVERIFY(addNote(clip, 65, 41, 70));

		// Half strength onto a 12-tick grid: 5 -> 0 is -5, so 5 + round(-2.5) = 2.
		const ControlResult half = run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
				{QStringLiteral("strength"), 0.5}});
		QVERIFY2(half.ok, qPrintable(half.errorMessage));
		QCOMPARE(half.result.value(QStringLiteral("positions_moved")).toInt(), 4);
		QCOMPARE(half.result.value(QStringLiteral("velocities_moved")).toInt(), 0);
		const QVector<NoteAt> halved = takeOf(clip);
		QVERIFY2((halved == QVector<NoteAt>{{2, 90}, {14, 130}, {26, 110}, {38, 70}}),
			qPrintable(describe(halved)));

		// Full strength then lands them on the grid exactly, leaving the
		// velocities alone (no humanise was asked for).
		QVERIFY(run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
				{QStringLiteral("strength"), 1.0}}).ok);
		const QVector<NoteAt> exact = takeOf(clip);
		QVERIFY2((exact == QVector<NoteAt>{{0, 90}, {12, 130}, {24, 110}, {36, 70}}),
			qPrintable(describe(exact)));

		// The humanise is bounded by what was asked for, reproducible from the
		// seed, and a different take under a different seed.
		const QJsonObject take{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
			{QStringLiteral("humanise_ticks"), 3}, {QStringLiteral("humanise_velocity"), 5},
			{QStringLiteral("seed"), 7}};
		QVERIFY(run(QStringLiteral("groove.quantize"), take).ok);
		const QVector<NoteAt> humanised = takeOf(clip);
		QVERIFY2(humanised != exact, qPrintable(describe(humanised)));
		for (int index = 0; index < humanised.size(); ++index)
		{
			QVERIFY2(qAbs(humanised[index].position - exact[index].position) <= 3,
				qPrintable(describe(humanised)));
			QVERIFY2(qAbs(humanised[index].velocity - exact[index].velocity) <= 5,
				qPrintable(describe(humanised)));
		}
		QVERIFY(run(QStringLiteral("groove.quantize"), QJsonObject{
			{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
			{QStringLiteral("strength"), 1.0}, {QStringLiteral("humanise_ticks"), 3},
			{QStringLiteral("humanise_velocity"), 5}, {QStringLiteral("seed"), 7}}).ok);
		QVERIFY2(takeOf(clip) == humanised,
			"the same seed and amounts did not reproduce the same take");
		QVERIFY(run(QStringLiteral("groove.quantize"), QJsonObject{
			{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
			{QStringLiteral("strength"), 1.0}, {QStringLiteral("humanise_ticks"), 3},
			{QStringLiteral("humanise_velocity"), 5}, {QStringLiteral("seed"), 8}}).ok);
		QVERIFY2(takeOf(clip) != humanised, "a second seed produced the same take");
	}

	//! A pool edit reverses through the RECORDED ACTION checkpoint: extract,
	//! rename and remove each come back off with one control.undo.
	void poolEditsAreReversibleThroughTheRecordedAction()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 5, 100));
		QVERIFY(addNote(clip, 62, 17, 100));

		QVERIFY(run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("be")},
				{QStringLiteral("grid"), 12}}).ok);
		QVERIFY(namesInPool().contains(QStringLiteral("be")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!namesInPool().contains(QStringLiteral("be")),
			"control.undo left the extracted groove in the pool");

		QVERIFY(run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("be")},
				{QStringLiteral("grid"), 12}}).ok);
		QVERIFY(run(QStringLiteral("groove.rename"),
			{{QStringLiteral("name"), QStringLiteral("be")},
				{QStringLiteral("to"), QStringLiteral("swing")}}).ok);
		QVERIFY(namesInPool().contains(QStringLiteral("swing")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!namesInPool().contains(QStringLiteral("swing")), "the rename did not come back");
		QVERIFY(namesInPool().contains(QStringLiteral("be")));

		QVERIFY(run(QStringLiteral("groove.remove"),
			{{QStringLiteral("name"), QStringLiteral("be")}}).ok);
		QVERIFY(!namesInPool().contains(QStringLiteral("be")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(namesInPool().contains(QStringLiteral("be")),
			"the removed groove did not come back");
		// The STEPS came back too, not only the name: the recorded step writes
		// the captured element rather than rebuilding a neutral template.
		QVERIFY(stepsOf(QStringLiteral("be"))[1].position == -5);
		QVERIFY(run(QStringLiteral("groove.remove"),
			{{QStringLiteral("name"), QStringLiteral("be")}}).ok);
	}

	//! A clip edit reverses through the CLIP's journal checkpoint: one
	//! control.undo restores every position and velocity the command moved.
	void clipEditsAreReversibleThroughTheClipCheckpoint()
	{
		QVERIFY(run(QStringLiteral("groove.set"),
			{{QStringLiteral("name"), QStringLiteral("be")},
				{QStringLiteral("length_ticks"), 48}, {QStringLiteral("step_ticks"), 12},
				{QStringLiteral("steps"), QJsonArray{QJsonObject{
					{QStringLiteral("slot"), 1}, {QStringLiteral("timing"), -4},
					{QStringLiteral("velocity"), 25}}}}}).ok);

		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 12, 100));
		QVERIFY(run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("be")}}).ok);
		QVERIFY2((takeOf(clip) == QVector<NoteAt>{{8, 125}}), qPrintable(describe(takeOf(clip))));
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the groove edit");
		QVERIFY2((takeOf(clip) == QVector<NoteAt>{{12, 100}}), qPrintable(describe(takeOf(clip))));

		QVERIFY(addNote(clip, 62, 13, 100));
		QCOMPARE(takeOf(clip)[1].position, 13);
		QVERIFY(run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
				{QStringLiteral("strength"), 1.0}}).ok);
		QCOMPARE(takeOf(clip)[1].position, 12);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(takeOf(clip)[1].position, 13);
	}

	//! Every refusal is typed, and changes nothing - not the pool, not a clip.
	void refusalsAreTypedAndChangeNothing()
	{
		QVERIFY(run(QStringLiteral("groove.set"),
			{{QStringLiteral("name"), QStringLiteral("keep")},
				{QStringLiteral("length_ticks"), 12}, {QStringLiteral("step_ticks"), 12},
				{QStringLiteral("steps"), QJsonArray{QJsonObject{
					{QStringLiteral("slot"), 0}, {QStringLiteral("timing"), 2},
					{QStringLiteral("velocity"), 4}}}}}).ok);
		const int poolSize = namesInPool().size();

		// A step past half the slot width, an out-of-range slot, a misaligned
		// length, an empty name: every one typed, and none of them written.
		QCOMPARE(run(QStringLiteral("groove.set"), QJsonObject{
			{QStringLiteral("name"), QStringLiteral("bad")},
			{QStringLiteral("length_ticks"), 12}, {QStringLiteral("step_ticks"), 12},
			{QStringLiteral("steps"), QJsonArray{QJsonObject{
				{QStringLiteral("slot"), 0}, {QStringLiteral("timing"), 9}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.set"), QJsonObject{
			{QStringLiteral("name"), QStringLiteral("bad")},
			{QStringLiteral("length_ticks"), 12}, {QStringLiteral("step_ticks"), 12},
			{QStringLiteral("steps"), QJsonArray{QJsonObject{
				{QStringLiteral("slot"), 4}, {QStringLiteral("timing"), 0}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.set"), QJsonObject{
			{QStringLiteral("name"), QString()}, {QStringLiteral("length_ticks"), 12},
			{QStringLiteral("step_ticks"), 12}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.set"), QJsonObject{
			{QStringLiteral("name"), QStringLiteral("bad")},
			{QStringLiteral("length_ticks"), 13},
			{QStringLiteral("step_ticks"), 12}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.list"),
			{{QStringLiteral("name"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("groove.remove"),
			{{QStringLiteral("name"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("groove.rename"),
			{{QStringLiteral("name"), QStringLiteral("nope")},
				{QStringLiteral("to"), QStringLiteral("other")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), QStringLiteral("clip-9999")},
				{QStringLiteral("name"), QStringLiteral("keep")}}).errorKind,
			ControlErrorKind::NotFound);
		// A clip that is not a MidiClip, and a MidiClip with no notes, are two
		// different facts and both typed.
		const QString sample = makeClip(QStringLiteral("sample"));
		QCOMPARE(run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), sample}, {QStringLiteral("grid"), 12}}).errorKind,
			ControlErrorKind::Refused);
		const QString empty = makeClip();
		QCOMPARE(run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), empty}, {QStringLiteral("name"), QStringLiteral("nothing")},
				{QStringLiteral("grid"), 12}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), empty},
				{QStringLiteral("name"), QStringLiteral("keep")}}).errorKind,
			ControlErrorKind::Refused);
		// 'strength' outside 0..1 is refused rather than clamped.
		const QString clip = makeClip();
		QVERIFY(addNote(clip, 60, 5, 100));
		QCOMPARE(run(QStringLiteral("groove.apply"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("keep")},
				{QStringLiteral("strength"), 5.0}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
				{QStringLiteral("humanise_ticks"), 40}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("groove.quantize"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("grid"), 12},
				{QStringLiteral("mode"), QStringLiteral("sideways")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY2((takeOf(clip) == QVector<NoteAt>{{5, 100}}), qPrintable(describe(takeOf(clip))));
		QCOMPARE(namesInPool().size(), poolSize);
		QVERIFY(projectPool().find(QStringLiteral("bad")) == nullptr);
	}

	/*! THE PROJECT FILE, and the rule that makes an absence meaningful: the pool
	 *  is written ONLY when it holds a groove, and a project whose file carries
	 *  no <groove-pool> element loads into an EMPTY pool. */
	void theProjectFileCarriesThePoolAndAnAbsentElementClearsIt()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 5, 100));
		QVERIFY(addNote(clip, 62, 26, 100));
		QVERIFY(run(QStringLiteral("groove.extract"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("name"), QStringLiteral("saved")},
				{QStringLiteral("grid"), 12}, {QStringLiteral("length"), 48}}).ok);
		QCOMPARE(projectPool().size(), 1);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("groove.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), "the project could not be written");

		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(file.readAll());
		file.close();
		QVERIFY2(text.contains(QStringLiteral("<groove-pool")),
			"the saved project carries no groove-pool element");

		// The negative control: drop the element and the pool has to come back
		// EMPTY, not holding what the previous project left behind.
		QDomDocument document;
		QVERIFY(document.setContent(text));
		QDomNodeList pools = document.elementsByTagName(QStringLiteral("groove-pool"));
		QCOMPARE(pools.size(), 1);
		pools.at(0).parentNode().removeChild(pools.at(0));
		const QString withoutPool = document.toString();
		QVERIFY(!withoutPool.contains(QStringLiteral("<groove-pool")));
		const QString stripped = dir.filePath(QStringLiteral("no-groove.mmp"));
		QFile out(stripped);
		QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
		out.write(withoutPool.toUtf8());
		out.close();

		Engine::getSong()->loadProject(stripped);
		QVERIFY2(projectPool().empty(),
			"a project with no <groove-pool> element left the previous project's grooves behind");

		// ... and the element that IS there restores the groove exactly.
		Engine::getSong()->loadProject(path);
		QCOMPARE(projectPool().size(), 1);
		const GrooveTemplate* restored = projectPool().find(QStringLiteral("saved"));
		QVERIFY2(restored != nullptr, "the saved groove did not come back");
		QCOMPARE(restored->slotCount(), 4);
		const QVector<NoteAt> steps = stepsOf(QStringLiteral("saved"));
		QVERIFY2((steps == QVector<NoteAt>{{5, 0}, {0, 0}, {2, 0}, {0, 0}}),
			qPrintable(describe(steps)));
	}
};

QTEST_GUILESS_MAIN(ControlGrooveCommandsTest)
#include "ControlGrooveCommandsTest.moc"
