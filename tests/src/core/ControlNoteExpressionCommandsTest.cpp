/*
 * ControlNoteExpressionCommandsTest.cpp - the note.expression.* half of the #602 surface: set / get / clear over the
 * per-note MPE fields task #601 already stores, with a MidiClip checkpoint as
 * the inverse.
 *
 * Splitting of the #602 modulation layer's tests for the file-length ratchet:
 * the helpers these files share are in tests/src/core/ModulationTestSupport.h
 * and the shared fixture in tests/src/core/RackTestSupport.h.
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
#include <QDomElement>
#include <QJsonObject>
#include <QString>

#include "ModulationTestSupport.h"
#include "MpeExpression.h"

using namespace modtest;


class ControlNoteExpressionCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	void init() { resetLayer(); }

	//! The per-note half: it edits task #601's own Note fields and its optional
	//! attributes, and a MidiClip checkpoint takes it back.
	void noteExpressionEditsAndUndoes()
	{
		QString clip;
		QString note;
		QVERIFY(buildClipWithNote(&clip, &note));

		// A note that never carried expression says so.
		QJsonObject state = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}}).result;
		QCOMPARE(state.value(QStringLiteral("has_expression")).toBool(), false);
		QCOMPARE(state.value(QStringLiteral("pitch_cents")).toInt(), 0);
		QCOMPARE(state.value(QStringLiteral("max_pitch_cents")).toInt(),
			MpeNoteExpression::MaxPitchCents);

		const ControlResult set = run(QStringLiteral("note.expression_set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note},
				{QStringLiteral("pitch"), 1200}, {QStringLiteral("pressure"), 64},
				{QStringLiteral("timbre"), 32}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("has_expression")).toBool(), true);
		QCOMPARE(set.result.value(QStringLiteral("pitch_cents")).toInt(), 1200);

		// An axis the call omits keeps its value; the note stays expressive.
		QVERIFY(run(QStringLiteral("note.expression_set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note},
				{QStringLiteral("pitch"), -2400}}).ok);
		state = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}}).result;
		QCOMPARE(state.value(QStringLiteral("pitch_cents")).toInt(), -2400);
		QCOMPARE(state.value(QStringLiteral("pressure")).toInt(), 64);
		QCOMPARE(state.value(QStringLiteral("timbre")).toInt(), 32);

		// The clip-wide form lists the note that carries expression.
		const QJsonObject listed = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}}).result;
		QCOMPARE(listed.value(QStringLiteral("expression_count")).toInt(), 1);
		QCOMPARE(listed.value(QStringLiteral("notes")).toArray().at(0).toObject()
			.value(QStringLiteral("note")).toString(), note);

		// The schema bounds a bend to #601's own range.
		ControlResult bad = run(QStringLiteral("note.expression_set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note},
				{QStringLiteral("pitch"), 99999}});
		QCOMPARE(bad.errorKind, ControlErrorKind::InvalidArgs);
		bad = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), QStringLiteral("note-9")}});
		QCOMPARE(bad.errorKind, ControlErrorKind::NotFound);

		// One undo takes the last set back - axis by axis, not the whole note.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		state = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}}).result;
		QCOMPARE(state.value(QStringLiteral("pitch_cents")).toInt(), 1200);

		// Clearing drops the expression and its presence flag, and undo brings
		// all three axes back.
		const ControlResult cleared = run(QStringLiteral("note.expression_clear"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("has_expression")).toBool(), false);
		// A second clear is a typed refusal, not a silent write.
		bad = run(QStringLiteral("note.expression_clear"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}});
		QCOMPARE(bad.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		state = run(QStringLiteral("note.expression_get"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("note"), note}}).result;
		QCOMPARE(state.value(QStringLiteral("has_expression")).toBool(), true);
		QCOMPARE(state.value(QStringLiteral("pitch_cents")).toInt(), 1200);
		QCOMPARE(state.value(QStringLiteral("pressure")).toInt(), 64);
		evidence("note-expression",
			QStringLiteral("set/get/clear round trip with a MidiClip checkpoint"));
	}
};

QTEST_GUILESS_MAIN(ControlNoteExpressionCommandsTest)
#include "ControlNoteExpressionCommandsTest.moc"
