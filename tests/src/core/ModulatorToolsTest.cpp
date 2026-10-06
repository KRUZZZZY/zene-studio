/*
 * ModulatorToolsTest.cpp - formula modulators and modulators restarted by a track's notes
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
/*! 0.5.0 L10 (W3): a formula compiles into a bounded program and evaluates to what it says, and
 *  refuses what it cannot read (MX-118); modulator.formula_set and modulator.trigger_set write the
 *  layer, are saved only when set, and undo; a note on the followed track restarts the modulator's
 *  cycle (MX-121). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>

#include <cmath>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ModulationFormula.h"
#include "ModulationLayer.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

double value(const QString& text, double phase, double cycles = 0.0)
{
	FormulaProgram program;
	QString why;
	if (!compileFormula(text, &program, &why)) { return 99.0; }
	return evaluateFormula(program, phase, cycles);
}

bool compiles(const QString& text)
{
	FormulaProgram program;
	QString why;
	return compileFormula(text, &program, &why);
}

ModulationLayerPublisher& publisher() { return Engine::getSong()->modulationLayer(); }

const Modulator& first() { return *publisher().layer().modulator(0); }

QString created()
{
	return run(QStringLiteral("modulator.create"), {{QStringLiteral("name"), QStringLiteral("Mod")}, {QStringLiteral("rate"), 1.0}})
		.result.value(QStringLiteral("modulator")).toString();
}

} // namespace

class ModulatorToolsTest : public QObject
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

	void init()
	{
		publisher().edit([](ModulationLayer& layer, ModulationRuntime& runtime) {
			layer.clear();
			runtime = ModulationRuntime{};
			return true;
		});
		Engine::projectJournal()->clearJournal();
	}

	//! MX-118: the language's operators, functions and names, the bounds and the refusals.
	void aFormulaEvaluatesAndRefuses()
	{
		QVERIFY(std::abs(value(QStringLiteral("sin(2*pi*t)"), 0.25) - 1.0) < 1e-6);
		QVERIFY(std::abs(value(QStringLiteral("tri(t)"), 0.0) + 1.0) < 1e-6);
		QVERIFY(std::abs(value(QStringLiteral("saw(t) * 0.5"), 0.75) - 0.25) < 1e-6);
		QVERIFY(std::abs(value(QStringLiteral("1 - 2 * 3 ^ 2 / 18"), 0.0) - 0.0) < 1e-6);  // precedence: 1 - (2 * 9) / 18
		QVERIFY(std::abs(value(QStringLiteral("min(t, 0.2) + max(-1, -2)"), 0.5) + 0.8) < 1e-6);
		QCOMPARE(value(QStringLiteral("rand(x)"), 0.0, 3.0), value(QStringLiteral("rand(x)"), 0.9, 3.4));  // per whole cycle
		QCOMPARE(value(QStringLiteral("1 / (t - t)"), 0.3), 0.0);  // a division by zero reads as 0
		QCOMPARE(value(QStringLiteral("5"), 0.0), 1.0);             // kept to -1..1
		QCOMPARE(value(QStringLiteral("-x"), 0.0, 0.5), -0.5);
		QVERIFY(std::abs(value(QStringLiteral("cos(pi)"), 0.0) + 1.0) < 1e-6);
		QVERIFY(!compiles(QStringLiteral("sin(")));
		QVERIFY(!compiles(QStringLiteral("wobble(t)")));
		QVERIFY(!compiles(QStringLiteral("t +")));
		QVERIFY(!compiles(QStringLiteral("min(t)")));
		QVERIFY(!compiles(QStringLiteral("")));
		QVERIFY(!compiles(QStringLiteral("1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+t))))))))))))")));  // deeper than the stack
		FormulaProgram program;
		QString why;
		QVERIFY(!compileFormula(QStringLiteral("t ) 1"), &program, &why));
		QVERIFY(why.contains(QStringLiteral("character")));
		QVERIFY(program.empty());
	}

	//! Cluster 5171: an exponent with no sign is read whole. "1e3" is 1000 and
	//! "1e30" is 1e30, both clamped to 1 - not a parse failure and not 1e0.
	void positiveExponentsAreReadWhole()
	{
		QVERIFY(compiles(QStringLiteral("1e3")));
		QVERIFY(compiles(QStringLiteral("1e30")));
		QVERIFY(compiles(QStringLiteral("1.5e2")));
		QVERIFY(compiles(QStringLiteral("2E4")));
		QCOMPARE(value(QStringLiteral("1e3"), 0.0), 1.0);
		QCOMPARE(value(QStringLiteral("1e30"), 0.0), 1.0);
		QCOMPARE(value(QStringLiteral("1.5e2"), 0.0), 1.0);
		// The sign still works, and every digit of the exponent is consumed:
		// 1e-1 reads as 0.1 (a float constant, hence the fuzzy compare), and
		// 1e-2 * 100 is exactly 1 - which it cannot be if "2" were dropped.
		QVERIFY(std::abs(value(QStringLiteral("1e-1"), 0.0) - 0.1) < 1e-6);
		QVERIFY(std::abs(value(QStringLiteral("1e-2 * 100"), 0.0) - 1.0) < 1e-6);
		// A formula mixing an exponent into arithmetic must not leave the
		// digits behind for the next term.
		QVERIFY(std::abs(value(QStringLiteral("1e-1 + 0.1"), 0.0) - 0.2) < 1e-6);
	}

	//! Cluster 3265: a finite literal larger than the float range is refused at
	//! compile time, because the evaluator turns the resulting infinity into
	//! silence rather than the clamped 1 the contract promises.
	void aLiteralTheEvaluatorCannotHoldIsRefused()
	{
		QVERIFY(!compiles(QStringLiteral("1e100")));
		QVERIFY(!compiles(QStringLiteral("-1e100")));
		// The largest float is still representable, and reads as the clamped 1.
		QVERIFY(compiles(QStringLiteral("3.4e38")));
		QCOMPARE(value(QStringLiteral("3.4e38"), 0.0), 1.0);
		// A value inside the float range compiles and clamps as it always did.
		QVERIFY(compiles(QStringLiteral("1e30")));
		QCOMPARE(value(QStringLiteral("1e30"), 0.0), 1.0);
	}

	//! MX-118: formula_set gives the modulator the shape and the program; the layer's LFO runs it;
	//! it is saved with the shape and read back; a bad formula is refused; undo restores the shape.
	void formulaSetDrivesTheModulator()
	{
		const QString id = created();
		QCOMPARE(run(QStringLiteral("modulator.formula_set"), {{QStringLiteral("modulator"), id},
			{QStringLiteral("formula"), QStringLiteral("sin(")}}).errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult set = run(QStringLiteral("modulator.formula_set"), {{QStringLiteral("modulator"), id},
			{QStringLiteral("formula"), QStringLiteral("saw(t)")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("formula")).toString(), QStringLiteral("saw(t)"));
		QCOMPARE(first().source.shape, ModulationShape::Formula);
		QVERIFY(std::abs(ModulationLayer::outputAt(first().source, 0.75) - 0.5f) < 1e-5f);  // 1 Hz: t = 0.75
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		QVERIFY(publisher().layer().saveSettings(doc, root));
		const QDomElement saved = root.firstChildElement(QStringLiteral("modulation-layer")).firstChildElement(QStringLiteral("modulator"));
		QCOMPARE(saved.attribute(QStringLiteral("formula")), QStringLiteral("saw(t)"));
		QVERIFY(!saved.hasAttribute(QStringLiteral("trigger")));
		ModulationLayer back;
		QVERIFY(back.loadSettings(root.firstChildElement(QStringLiteral("modulation-layer"))));
		QCOMPARE(back.modulator(0)->formula, QStringLiteral("saw(t)"));
		QVERIFY(std::abs(ModulationLayer::outputAt(back.modulator(0)->source, 0.25) + 0.5f) < 1e-5f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(first().source.shape, ModulationShape::Sine);
		QVERIFY(first().formula.isEmpty());
	}

	//! MX-121: trigger_set follows an instrument track; before its first note the modulator waits at
	//! the start of its cycle, and a note restarts it; a track with no notes is refused; undo frees it.
	void aNoteRestartsAFollowingModulator()
	{
		const QString id = created();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Track* audio = Track::create(Track::Type::Sample, Engine::getSong());
		QCOMPARE(run(QStringLiteral("modulator.trigger_set"), {{QStringLiteral("modulator"), id},
			{QStringLiteral("track"), control::trackIdOf(audio)}}).errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult set = run(QStringLiteral("modulator.trigger_set"), {{QStringLiteral("modulator"), id},
			{QStringLiteral("track"), control::trackIdOf(track)}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(first().source.triggerTrack, track->id());
		QCOMPARE(publisher().sinceTrigger(0, track->id(), 5.0), 0.0);  // no note yet
		publisher().setBlockSeconds(2.0);
		publisher().noteStarted(track->id() + 1000);  // another track's note
		QCOMPARE(publisher().sinceTrigger(0, track->id(), 5.0), 0.0);
		publisher().noteStarted(track->id());
		QCOMPARE(publisher().sinceTrigger(0, track->id(), 2.5), 0.5);
		QCOMPARE(publisher().sinceTrigger(0, -1, 2.5), 2.5);  // a free modulator runs on the timeline
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		QVERIFY(publisher().layer().saveSettings(doc, root));
		QCOMPARE(root.firstChildElement(QStringLiteral("modulation-layer")).firstChildElement(QStringLiteral("modulator"))
			.attribute(QStringLiteral("trigger")).toInt(), track->id());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(first().source.triggerTrack, -1);
	}
};

QTEST_MAIN(ModulatorToolsTest)
#include "ModulatorToolsTest.moc"
