/*
 * RackMacrosTest.cpp - the MACRO half of the rack.* command group (SPEC
 *                      A11-A16): the named, persisted scalars that drive
 *                      existing model parameters through range windows.
 *
 * The rack ENGINE landed with #599 (include/Rack.h, the <rack> element inside a
 * <mixerchannel>, RackTest); its report (docs/RACKS.md section 5) names macros
 * as the half it did not do. The load-bearing assertion here is that a macro's
 * write is REAL and EXACT: the window is a fraction of the parameter's own
 * min..max, so one macro value produces 25 on a 0..100 control and -50 on a
 * -100..100 one, and an inverted window drives the other way. A macro that only
 * stored a number would pass a weaker test and drive nothing.
 *
 * The zone half is RackZonesTest.cpp; the fixture both share is
 * RackTestSupport.h.
 *
 * Evidence is printed unconditionally (stdout, flushed) so the numbers can be
 * pasted into docs/RACK-MACROS.md.
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
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "AllocationProbe.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlRackSupport.h"
#include "ControlReversibility.h"
#include "RackMacros.h"
#include "RackTestSupport.h"
#include "RackZones.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;

namespace
{

//! What control.id_contract reports as the session's fx-<n> count.
int fxIdCount()
{
	const ControlResult contract = revtest::run(QStringLiteral("control.id_contract"));
	if (!contract.ok) { return -1; }
	for (const QJsonValue& family : contract.result.value(QStringLiteral("families")).toArray())
	{
		const QJsonObject entry = family.toObject();
		if (entry.value(QStringLiteral("prefix")).toString() == QLatin1String("fx-"))
		{
			return entry.value(QStringLiteral("count")).toInt();
		}
	}
	return -1;
}

} // namespace

class RackMacrosTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase()
	{
		revtest::run(QStringLiteral("control.set_undo_coalescing"),
			{{QStringLiteral("window_ms"), control::UndoCoalesceWindowMs}});
		teardownRackFixture();
	}

	//! A macro writes the REAL model, through the window, exactly - on two
	//! parameters whose ranges are deliberately different, including an
	//! inverted window.
	void macrosDriveParametersThroughTheirWindows()
	{
		RackMacros& macros = rackUnderTest().macros();
		const int gainMacro = macros.addMacro(QStringLiteral("Gain sweep"), 0.0f);
		QCOMPARE(gainMacro, 0);
		QCOMPARE(macros.addTarget(gainMacro, targetOf(kGainName, 0.25f, 0.75f)), 0);
		QCOMPARE(macros.addTarget(gainMacro, targetOf(kPanName, 0.0f, 1.0f)), 1);

		FloatModel* const gain = gainModel();
		FloatModel* const pan = panModel();
		QVERIFY2(gain != nullptr, "the driven chain's 0..100 parameter was not found");
		QVERIFY2(pan != nullptr, "the driven chain's -100..100 parameter was not found");
		QCOMPARE(gain->value(), 50.0f);
		QCOMPARE(pan->value(), 0.0f);

		// value 0 -> the window's low end: a quarter of 0..100, all of -100..100.
		QVERIFY(macros.setValue(gainMacro, 0.0f));
		QCOMPARE(static_cast<int>(macros.apply(gainMacro, rackUnderTest()).size()), 2);
		QCOMPARE(gain->value(), 25.0f);
		QCOMPARE(pan->value(), kPanMinimum);
		printEvidence("window-low",
			QStringLiteral("value 0.0: gain (0..100, window 0.25..0.75) -> %1; pan (-100..100, "
				"window 0..1) -> %2")
				.arg(static_cast<double>(gain->value()))
				.arg(static_cast<double>(pan->value())));

		// value 0.5 -> the middle of the 0.25..0.75 window: half of 0..100.
		QVERIFY(macros.setValue(gainMacro, 0.5f));
		QCOMPARE(static_cast<int>(macros.apply(gainMacro, rackUnderTest()).size()), 2);
		QCOMPARE(gain->value(), 50.0f);
		QCOMPARE(pan->value(), 0.0f);

		// value 1 -> the window's high end. The FRACTION is what carries: the
		// same macro moves both ranges by the same share of their own span.
		QVERIFY(macros.setValue(gainMacro, 1.0f));
		QCOMPARE(static_cast<int>(macros.apply(gainMacro, rackUnderTest()).size()), 2);
		QCOMPARE(gain->value(), 75.0f);
		QCOMPARE(pan->value(), kPanMaximum);
		printEvidence("window-arithmetic",
			QStringLiteral("value 1.0: gain -> 75 (0.75 of 0..100); pan -> 100 (1.0 of -100..100)"));

		// An inverted window (high < low) drives the other way, exactly.
		const int inverted = macros.addMacro(QStringLiteral("Inverted"), 0.0f);
		QCOMPARE(macros.addTarget(inverted, targetOf(kGainName, 0.75f, 0.25f)), 0);
		QVERIFY(macros.setValue(inverted, 0.0f));
		QCOMPARE(static_cast<int>(macros.apply(inverted, rackUnderTest()).size()), 1);
		QCOMPARE(gain->value(), 75.0f);
		QVERIFY(macros.setValue(inverted, 1.0f));
		macros.apply(inverted, rackUnderTest());
		QCOMPARE(gain->value(), 25.0f);
		printEvidence("window-inverted",
			QStringLiteral("window 0.75..0.25: value 0.0 -> 75, value 1.0 -> 25"));

		// A value outside 0..1 is clamped by the macro, not silently applied.
		QVERIFY(macros.setValue(inverted, 4.0f));
		QCOMPARE(macros.macro(inverted)->value, 1.0f);

		// Every write is reported with its previous value, which is what the
		// command layer records as the inverse.
		QVERIFY(macros.setValue(inverted, 0.0f));
		const std::vector<RackMacroWrite> reported = macros.apply(inverted, rackUnderTest());
		QCOMPARE(static_cast<int>(reported.size()), 1);
		QCOMPARE(reported[0].previous, 25.0f);
		QCOMPARE(reported[0].written, 75.0f);

		// Back to one macro at its middle, so the later slots start from here.
		QVERIFY(macros.removeMacro(inverted));
		QVERIFY(macros.setValue(gainMacro, 0.5f));
		macros.apply(gainMacro, rackUnderTest());
		QCOMPARE(macros.macroCount(), 1);
	}

	//! 0.5.0 L10 (MX-105): a target's curve shapes the response across its window, and is saved
	//! only when it is not linear.
	void aCurveShapesTheResponse()
	{
		QCOMPARE(shapeMacroValue(0.5f, 0.0f), 0.5f);
		QVERIFY(std::abs(shapeMacroValue(0.5f, 1.0f) - std::pow(0.5f, 0.25f)) < 1e-5f);  // rises early
		QVERIFY(std::abs(shapeMacroValue(0.5f, -1.0f) - 0.0625f) < 1e-5f);              // rises late
		QCOMPARE(shapeMacroValue(1.0f, -1.0f), 1.0f);  // the ends stay where they are
		RackMacroTarget bent = targetOf(kGainName, 0.0f, 1.0f);
		bent.curve = -1.0f;
		QVERIFY(isValidMacroTarget(bent));
		RackMacros& macros = rackUnderTest().macros();
		const int curved = macros.addMacro(QStringLiteral("Curved"), 0.5f);
		QCOMPARE(macros.addTarget(curved, bent), 0);
		macros.apply(curved, rackUnderTest());
		QVERIFY(std::abs(gainModel()->value() - 6.25f) < 1e-3f);  // 0.5^4 of 0..100
		QDomDocument doc;
		QDomElement rack = doc.createElement(QStringLiteral("rack"));
		doc.appendChild(rack);  // in the document, so its text holds what was saved
		macros.saveSettings(doc, rack);
		QVERIFY(doc.toString().contains(QStringLiteral("curve=\"-1\"")));
		RackMacros reloaded;
		reloaded.loadSettings(rack);
		QCOMPARE(reloaded.macro(curved)->targets.front().curve, -1.0f);
		bent.curve = 1.5f;
		QVERIFY(!isValidMacroTarget(bent));
		QVERIFY(macros.removeMacro(curved));
		macros.apply(0, rackUnderTest());
	}

	//! A target whose chain, effect or parameter is gone is SKIPPED, and the
	//! applied count says so - it is never guessed at.
	void aMacroSkipsTargetsItCannotResolve()
	{
		RackMacros& macros = rackUnderTest().macros();
		RackMacroTarget gone = targetOf(kGainName, 0.0f, 1.0f);
		gone.effect = 7;

		const int macro = macros.addMacro(QStringLiteral("Half dead"), 1.0f);
		QCOMPARE(macros.addTarget(macro, gone), 0);
		QCOMPARE(macros.addTarget(macro, targetOf("NoSuchParameter", 0.0f, 1.0f)), 1);
		QCOMPARE(macros.addTarget(macro, targetOf(kGainName, 0.0f, 1.0f)), 2);

		const std::vector<RackMacroWrite> writes = macros.apply(macro, rackUnderTest());
		QCOMPARE(static_cast<int>(writes.size()), 1);
		QCOMPARE(writes[0].written, kGainMaximum);
		printEvidence("skipped-targets",
			QStringLiteral("targets=3 applied=1 (a missing effect and an unknown parameter are "
				"skipped, not guessed)"));
		QVERIFY(macros.removeMacro(macro));
	}

	//! A macro's value is a FINITE 0..1 scalar. std::clamp does not make a
	//! nonfinite value one - every comparison against NaN is false, so clamp
	//! hands NaN straight back - and QString reads a "nan" attribute as NaN,
	//! so a project can carry one. Such a value is refused at every door that
	//! stores it, and it never reaches a parameter model.
	void aNonfiniteMacroValueIsRefused()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		RackMacros& macros = rackUnderTest().macros();
		const int before = macros.macroCount();

		// The door: addMacro refuses it and adds NOTHING (no half-added macro).
		QCOMPARE(macros.addMacro(QStringLiteral("NaN"), nan), -1);
		QCOMPARE(macros.macroCount(), before);

		// The door: setValue refuses it and the macro keeps the value it had.
		const int good = macros.addMacro(QStringLiteral("Fine"), 0.5f);
		QVERIFY(good >= 0);
		QVERIFY(!macros.setValue(good, nan));
		QCOMPARE(macros.macro(good)->value, 0.5f);
		QVERIFY(!macros.setValue(good, std::numeric_limits<float>::infinity()));
		QCOMPARE(macros.macro(good)->value, 0.5f);

		// The door: insertMacro refuses it too.
		RackMacro broken;
		broken.name = QStringLiteral("Broken");
		broken.value = nan;
		QVERIFY(!macros.insertMacro(macros.macroCount(), broken));
		QCOMPARE(macros.macroCount(), before + 1);

		// The load path: a project that says value="nan" loads a NUMBER, not a
		// NaN - a NaN here would be written into every parameter the macro
		// drives.
		QDomDocument doc;
		QDomElement rack = doc.createElement(QStringLiteral("rack"));
		doc.appendChild(rack);
		QDomElement macroElement = doc.createElement(QStringLiteral("macro"));
		macroElement.setAttribute(QStringLiteral("name"), QStringLiteral("Loaded"));
		macroElement.setAttribute(QStringLiteral("value"), QStringLiteral("nan"));
		rack.appendChild(macroElement);
		RackMacros reloaded;
		reloaded.loadSettings(rack);
		QCOMPARE(reloaded.macroCount(), 1);
		QVERIFY(std::isfinite(reloaded.macro(0)->value));
		QCOMPARE(reloaded.macro(0)->value, 0.0f);

		// The agent surface: the schema lets NaN through (every comparison
		// against it is false), so the command has to refuse it itself.
		const QString channel = underTestChannel();
		const ControlResult added = revtest::run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QStringLiteral("NaN add")},
				{QStringLiteral("value"), std::numeric_limits<double>::quiet_NaN()}});
		QVERIFY2(!added.ok, "rack.macro_add took a NaN value");
		QCOMPARE(added.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(rackUnderTest().macros().macroCount(), before + 1);

		const ControlResult set = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-%1").arg(good)},
				{QStringLiteral("value"), std::numeric_limits<double>::quiet_NaN()}});
		QVERIFY2(!set.ok, "rack.macro_set took a NaN value");
		QCOMPARE(set.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(macros.macro(good)->value, 0.5f);

		QVERIFY(macros.removeMacro(good));
		QCOMPARE(macros.macroCount(), before);
	}

	//! A macro target's window is a pair of fractions and a curve, and a NaN is
	//! not a fraction: every comparison against NaN is false, so the range
	//! checks let it through - and the applied fraction is then a NaN, which
	//! lands in the parameter model.
	void aNonfiniteMacroTargetNeverReachesAParameter()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();

		// A target that carries a NaN never reaches the model: the applied
		// fraction would be a NaN, and so would the value written.
		RackMacros& macros = rackUnderTest().macros();
		const int macro = macros.addMacro(QStringLiteral("NaN window"), 1.0f);
		RackMacroTarget nanCurve = targetOf(kGainName, 0.0f, 1.0f);
		nanCurve.curve = nan;
		QCOMPARE(macros.addTarget(macro, nanCurve), 0);
		FloatModel* const gain = gainModel();
		QVERIFY2(gain != nullptr, "the driven chain's 0..100 parameter was not found");
		const float before = gain->value();
		// The fixture is shared: put the parameter back whatever happens, so a
		// failure cannot leave a NaN behind for the next case.
		const QScopeGuard restore([gain, before]() { gain->setValue(before); });
		const std::vector<RackMacroWrite> writes = macros.apply(macro, rackUnderTest());
		QCOMPARE(static_cast<int>(writes.size()), 0);
		QCOMPARE(gain->value(), before);
		QVERIFY(std::isfinite(gain->value()));
		QVERIFY(macros.removeMacro(macro));

		// The range check itself: a NaN bound or curve is not a fraction.
		QVERIFY(!isValidMacroTarget(nanCurve));
		RackMacroTarget nanLow = targetOf(kGainName, 0.0f, 1.0f);
		nanLow.low = nan;
		QVERIFY(!isValidMacroTarget(nanLow));
		RackMacroTarget nanHigh = targetOf(kGainName, 0.0f, 1.0f);
		nanHigh.high = nan;
		QVERIFY(!isValidMacroTarget(nanHigh));

		// A project that says curve="nan" loads no such target: QString reads
		// a "nan" attribute as a NaN.
		QDomDocument doc;
		QDomElement rack = doc.createElement(QStringLiteral("rack"));
		doc.appendChild(rack);
		QDomElement macroElement = doc.createElement(QStringLiteral("macro"));
		macroElement.setAttribute(QStringLiteral("name"), QStringLiteral("Loaded"));
		macroElement.setAttribute(QStringLiteral("value"), QStringLiteral("1"));
		QDomElement targetElement = doc.createElement(QStringLiteral("target"));
		targetElement.setAttribute(QStringLiteral("chain"), kDrivenChain);
		targetElement.setAttribute(QStringLiteral("effect"), 0);
		targetElement.setAttribute(QStringLiteral("parameter"), QString::fromLatin1(kGainName));
		targetElement.setAttribute(QStringLiteral("low"), QStringLiteral("0"));
		targetElement.setAttribute(QStringLiteral("high"), QStringLiteral("1"));
		targetElement.setAttribute(QStringLiteral("curve"), QStringLiteral("nan"));
		macroElement.appendChild(targetElement);
		rack.appendChild(macroElement);
		RackMacros reloaded;
		reloaded.loadSettings(rack);
		QCOMPARE(reloaded.macroCount(), 1);
		QCOMPARE(static_cast<int>(reloaded.macro(0)->targets.size()), 0);
	}

	//! A project may not carry a macro value or a target window outside the
	//! 0..1 contract: the loader clamps the value into it and drops a window
	//! that is not a fraction pair, instead of driving the parameter to its
	//! maximum for every macro value. A project that is inside the contract
	//! loads exactly as it did.
	void aProjectLoadedMacroIsValidated()
	{
		FloatModel* const gain = gainModel();
		QVERIFY2(gain != nullptr, "the driven chain's 0..100 parameter was not found");
		const float before = gain->value();
		// The fixture is shared: put the parameter back whatever happens.
		const QScopeGuard restore([gain, before]() { gain->setValue(before); });

		// A project that says value="2" with a window 2..3.
		QDomDocument doc;
		QDomElement rack = doc.createElement(QStringLiteral("rack"));
		doc.appendChild(rack);
		QDomElement macroElement = doc.createElement(QStringLiteral("macro"));
		macroElement.setAttribute(QStringLiteral("name"), QStringLiteral("Out of range"));
		macroElement.setAttribute(QStringLiteral("value"), QStringLiteral("2"));
		QDomElement targetElement = doc.createElement(QStringLiteral("target"));
		targetElement.setAttribute(QStringLiteral("chain"), kDrivenChain);
		targetElement.setAttribute(QStringLiteral("effect"), 0);
		targetElement.setAttribute(QStringLiteral("parameter"), QString::fromLatin1(kGainName));
		targetElement.setAttribute(QStringLiteral("low"), QStringLiteral("2"));
		targetElement.setAttribute(QStringLiteral("high"), QStringLiteral("3"));
		macroElement.appendChild(targetElement);
		rack.appendChild(macroElement);

		RackMacros reloaded;
		reloaded.loadSettings(rack);
		QCOMPARE(reloaded.macroCount(), 1);
		// The loaded state is inside the 0..1 contract.
		QVERIFY(reloaded.macro(0)->value >= 0.0f);
		QVERIFY(reloaded.macro(0)->value <= 1.0f);
		QCOMPARE(reloaded.macro(0)->value, 1.0f);
		// The window 2..3 is not a fraction pair: it is not loaded, so the
		// parameter is not pinned at its maximum.
		QCOMPARE(static_cast<int>(reloaded.macro(0)->targets.size()), 0);
		gain->setValue(0.0f);
		const std::vector<RackMacroWrite> writes = reloaded.apply(0, rackUnderTest());
		QCOMPARE(static_cast<int>(writes.size()), 0);
		QCOMPARE(gain->value(), 0.0f);

		// A project inside the contract loads exactly as before: the value,
		// the window and the write it makes are the ones it always was.
		QDomDocument good;
		QDomElement goodRack = good.createElement(QStringLiteral("rack"));
		good.appendChild(goodRack);
		QDomElement goodMacro = good.createElement(QStringLiteral("macro"));
		goodMacro.setAttribute(QStringLiteral("name"), QStringLiteral("Inside"));
		goodMacro.setAttribute(QStringLiteral("value"), QStringLiteral("0.5"));
		QDomElement goodTarget = good.createElement(QStringLiteral("target"));
		goodTarget.setAttribute(QStringLiteral("chain"), kDrivenChain);
		goodTarget.setAttribute(QStringLiteral("effect"), 0);
		goodTarget.setAttribute(QStringLiteral("parameter"), QString::fromLatin1(kGainName));
		goodTarget.setAttribute(QStringLiteral("low"), QStringLiteral("0.25"));
		goodTarget.setAttribute(QStringLiteral("high"), QStringLiteral("0.75"));
		goodMacro.appendChild(goodTarget);
		goodRack.appendChild(goodMacro);

		RackMacros loaded;
		loaded.loadSettings(goodRack);
		QCOMPARE(loaded.macroCount(), 1);
		QCOMPARE(loaded.macro(0)->value, 0.5f);
		QCOMPARE(static_cast<int>(loaded.macro(0)->targets.size()), 1);
		QCOMPARE(loaded.macro(0)->targets.front().low, 0.25f);
		QCOMPARE(loaded.macro(0)->targets.front().high, 0.75f);
		const std::vector<RackMacroWrite> goodWrites = loaded.apply(0, rackUnderTest());
		QCOMPARE(static_cast<int>(goodWrites.size()), 1);
		QCOMPARE(goodWrites[0].written, 50.0f);
		QCOMPARE(gain->value(), 50.0f);
	}

	//! With macros and zones configured the rack's own process() allocates
	//! nothing: a macro is not on the audio path, and a zone's routing (0.5.0
	//! E20) reads blocks prepared when the rack re-wired.
	void configuringMacrosAndZonesLeavesTheAudioPathAllocatingNothing()
	{
		Rack& rack = rackUnderTest();
		RackZone zone;
		zone.lowKey = 60;
		zone.highKey = 72;
		zone.chain = kDrivenChain;
		QCOMPARE(rack.zones().addZone(zone), 0);

		QVERIFY2(rack.canProcessThroughRack(currentBus()),
			"the rack is not on the path - the allocation claim would be vacuous");
		QVERIFY(rack.macros().macroCount() > 0);
		QVERIFY(rack.zones().zoneCount() > 0);

		auto bus = currentBus();
		// The first block may touch lazy internals; measure the second.
		rack.processAudioBuffer(bus);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		rack.processAudioBuffer(bus);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		printEvidence("macro-zone-rack-allocations",
			QStringLiteral("allocations=%1").arg(static_cast<qulonglong>(allocations)));
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! The group is registered: every id exists, carries both schemas, and has
	//! the SPEC A16 class the contract table states.
	void theGroupIsRegisteredWithSchemasAndA16Classes()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const struct
		{
			const char* id;
			bool mutating;
			const char* cls;
		} expected[] = {
			{"rack.get_state", false, "not_mutating"},
			{"rack.add_chain", true, "true_inverse"},
			{"rack.remove_chain", true, "snapshot"},
			{"rack.set_selected", true, "true_inverse"},
			{"rack.macro_add", true, "true_inverse"},
			{"rack.macro_remove", true, "true_inverse"},
			{"rack.macro_target_add", true, "true_inverse"},
			{"rack.macro_target_remove", true, "true_inverse"},
			{"rack.macro_set", true, "true_inverse"},
			{"rack.zone_add", true, "true_inverse"},
			{"rack.zone_remove", true, "true_inverse"},
			{"rack.zone_resolve", false, "not_mutating"},
		};

		int checked = 0;
		for (const auto& want : expected)
		{
			const QString id = QString::fromLatin1(want.id);
			const ControlCommand* cmd = registry->command(id);
			QVERIFY2(cmd != nullptr, qPrintable(id + " is not registered"));
			QCOMPARE(cmd->group, QStringLiteral("rack"));
			QVERIFY2(!cmd->argsSchema.isEmpty(), qPrintable(id + " has no args schema"));
			QVERIFY2(!cmd->resultSchema.isEmpty(), qPrintable(id + " has no result schema"));
			QVERIFY2(!cmd->description.isEmpty(), qPrintable(id + " has no description"));
			QCOMPARE(cmd->mutating, want.mutating);
			// The class comes from the ONE contract table, not from the handler:
			// an id here with no row would be an unclassified command, which the
			// anti-drift test refuses as well.
			const control::ReversibilityEntry* entry =
				control::ReversibilityTable::instance().lookup(id);
			QVERIFY2(entry != nullptr, qPrintable(id + " has no reversibility row"));
			QCOMPARE(control::reversibilityClassName(entry->cls), QString::fromLatin1(want.cls));
			QVERIFY2(!entry->reason.isEmpty(), qPrintable(id + " has an empty reason"));
			QVERIFY2(!entry->mechanism.isEmpty(), qPrintable(id + " has an empty mechanism"));
			++checked;
		}
		printEvidence("group-registered",
			QStringLiteral("commands=%1 mutating=10 read_only=2").arg(checked));
	}

	//! The end-to-end path an agent takes: add a macro, bind a parameter, drive
	//! it - then take it back with control.undo, which must restore BOTH the
	//! macro's value and the parameter it wrote.
	void theSurfaceCreatesDrivesAndUndoesAMacro()
	{
		FloatModel* const gain = gainModel();
		QVERIFY2(gain != nullptr, "the driven chain's 0..100 parameter was not found");
		gain->setValue(kGainMaximum);
		const QString channel = underTestChannel();  // the id the fixture's channel carries, not kChannel

		const ControlResult state = revtest::run(QStringLiteral("rack.get_state"),
			QJsonObject{{QStringLiteral("channel"), channel}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("chain_count")).toInt(), 2);
		QVERIFY(state.result.value(QStringLiteral("macros")).toArray().size() >= 1);

		const ControlResult added = revtest::run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), channel},
				{QStringLiteral("name"), QStringLiteral("Level")},
				{QStringLiteral("value"), 0.0}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString macro = added.result.value(QStringLiteral("macro")).toString();

		const ControlResult bound = revtest::run(QStringLiteral("rack.macro_target_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), macro},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("low"), 0.25}, {QStringLiteral("high"), 0.75}});
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		QCOMPARE(bound.result.value(QStringLiteral("target_index")).toInt(), 0);

		const ControlResult driven = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), macro},
				{QStringLiteral("value"), 1.0}});
		QVERIFY2(driven.ok, qPrintable(driven.errorMessage));
		QCOMPARE(driven.result.value(QStringLiteral("applied")).toInt(), 1);
		QCOMPARE(driven.result.value(QStringLiteral("skipped")).toInt(), 0);
		QCOMPARE(gain->value(), 75.0f);
		printEvidence("surface-macro-set",
			QStringLiteral("gain 100 -> 75 through the window 0.25..0.75 at value 1.0"));

		QCOMPARE(revtest::stateOf(QStringLiteral("rack.macro_set"))
			.value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));
		QCOMPARE(revtest::stateOf(QStringLiteral("rack.macro_set"))
			.value(QStringLiteral("reversible")).toBool(), true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(gain->value(), kGainMaximum);
		const ControlResult after = revtest::run(QStringLiteral("rack.get_state"),
			QJsonObject{{QStringLiteral("channel"), channel}});
		QJsonObject restored;
		for (const QJsonValue& value : after.result.value(QStringLiteral("macros")).toArray())
		{
			if (value.toObject().value(QStringLiteral("macro")).toString() == macro)
			{
				restored = value.toObject();
			}
		}
		QVERIFY2(!restored.isEmpty(), "the macro disappeared from the rack after the undo");
		QCOMPARE(restored.value(QStringLiteral("value")).toDouble(), 0.0);
		printEvidence("surface-macro-undo",
			QStringLiteral("control.undo restored the macro to 0.0 and the parameter to 100"));
	}

	/*! control.id_contract's fx- count answers "how many fx- ids does this
	 *  session have". A mixer channel carries devices on its own chain, on the
	 *  rack's parallel chains and on its post-fader inserts - and all three hand
	 *  out fx- ids (ControlDeviceSupport's rackChainEffect walks exactly them) -
	 *  so the count must move when a device lands on any of the three. */
	void theIdContractCountsEveryChainThatHandsOutFxIds()
	{
		const int base = fxIdCount();
		MixerChannel* const channel = Engine::mixer()->mixerChannel(kChannel);
		QVERIFY(channel != nullptr);

		auto* inRack = new RackParamEffect(channel->m_rack.chain(kDrivenChain));
		channel->m_rack.chain(kDrivenChain)->appendEffect(inRack);
		QCOMPARE(fxIdCount(), base + 1);  // a rack chain's device counts
		channel->m_rack.chain(kDrivenChain)->removeEffect(inRack);

		auto* inPost = new RackParamEffect(&channel->m_postInserts.chain());
		channel->m_postInserts.chain().appendEffect(inPost);
		QCOMPARE(fxIdCount(), base + 1);  // and so does a post-fader insert's
		channel->m_postInserts.chain().removeEffect(inPost);

		QCOMPARE(fxIdCount(), base);  // both taken back out
		printEvidence("id-contract-fx-count",
			QStringLiteral("fx- count moved with a rack chain's and a post-insert's device"));
	}

	//! Typed refusals: a caller is told what is wrong, and never gets "ok" for a
	//! selection or a bind that cannot exist.
	void theSurfaceRefusesWhatCannotExist()
	{
		const QString channel = underTestChannel();  // the id the fixture's channel carries, not kChannel

		const ControlResult noChannel = revtest::run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), QStringLiteral("ch-99")},
				{QStringLiteral("name"), QStringLiteral("nope")}});
		QVERIFY(!noChannel.ok);
		QCOMPARE(noChannel.errorKind, ControlErrorKind::NotFound);

		const ControlResult noName = revtest::run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QString()}});
		QVERIFY(!noName.ok);
		QCOMPARE(noName.errorKind, ControlErrorKind::InvalidArgs);

		// A selected chain that does not exist is refused rather than left
		// unwired: Rack::setSelectedChain would wire nothing at all.
		const ControlResult badSelection = revtest::run(QStringLiteral("rack.set_selected"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 5}});
		QVERIFY(!badSelection.ok);
		QCOMPARE(badSelection.errorKind, ControlErrorKind::InvalidArgs);

		// Chain 0 is the channel's own chain: it is not the rack's to remove.
		const ControlResult removeBase = revtest::run(QStringLiteral("rack.remove_chain"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 0}});
		QVERIFY(!removeBase.ok);
		QCOMPARE(removeBase.errorKind, ControlErrorKind::Refused);

		// A target that names no parameter is refused at BIND time, so a macro
		// never carries one that can only fail.
		const ControlResult badTarget = revtest::run(QStringLiteral("rack.macro_target_add"),
			{{QStringLiteral("channel"), channel},
				{QStringLiteral("macro"), QStringLiteral("macro-0")},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QStringLiteral("NoSuchParameter")}});
		QVERIFY(!badTarget.ok);
		QCOMPARE(badTarget.errorKind, ControlErrorKind::InvalidArgs);

		// A window that is not a fraction pair is refused too.
		const ControlResult badWindow = revtest::run(QStringLiteral("rack.macro_target_add"),
			{{QStringLiteral("channel"), channel},
				{QStringLiteral("macro"), QStringLiteral("macro-0")},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("low"), 3.0}, {QStringLiteral("high"), 1.0}});
		QVERIFY(!badWindow.ok);
		QCOMPARE(badWindow.errorKind, ControlErrorKind::InvalidArgs);
		printEvidence("typed-refusals",
			QStringLiteral("no channel -> not_found; empty name / bad selection / dead target / bad "
				"window -> invalid_args; chain 0 removal -> refused"));
	}

	//! Luna cluster 5257: removing a chain renumbers the macro targets that
	//! named a LATER chain, and drops the ones that named the chain that is
	//! gone. Without that, a target on the chain after the removed one drives
	//! the wrong device - and a target on the removed chain drives whichever
	//! chain slid into its index.
	void removingAChainReindexesTheMacroTargets()
	{
		Rack& rack = rackUnderTest();
		while (rack.chainCount() < 5) { rack.addChain(); }
		QCOMPARE(rack.chainCount(), 5);

		const int macro = rack.macros().addMacro(QStringLiteral("Luna5257"), 0.5f);
		RackMacroTarget onRemoved = targetOf(kGainName, 0.0f, 1.0f);
		onRemoved.chain = 2;
		QCOMPARE(rack.macros().addTarget(macro, onRemoved), 0);
		RackMacroTarget later = targetOf(kPanName, 0.0f, 1.0f);
		later.chain = 4;
		QCOMPARE(rack.macros().addTarget(macro, later), 1);

		QVERIFY(rack.removeChain(2));
		QCOMPARE(rack.chainCount(), 4);

		const RackMacro* const kept = rack.macros().macro(macro);
		QVERIFY(kept != nullptr);
		// The target on the removed chain is gone; the one on chain 4 slid to 3.
		QCOMPARE(static_cast<int>(kept->targets.size()), 1);
		QCOMPARE(kept->targets[0].chain, 3);
		QCOMPARE(kept->targets[0].parameter, QString::fromLatin1(kPanName));
		printEvidence("chain-removal-reindexes-macro-targets",
			QStringLiteral("chains 5 -> 4; target on chain 4 -> chain 3; target on the removed "
				"chain 2 dropped"));
	}

	//! A macro/zone id that does not PARSE is a statement about the arguments,
	//! not about the rack: "macro-x" is malformed in the same way a blank name
	//! is, and resolveRackChannel already answers invalid_args for "ch-x". The
	//! two cases must not share one not_found: a caller retrying on not_found
	//! can never fix a typo, and a caller checking the rack's size from that
	//! message is told a real rack question about a malformed string.
	void malformedMacroAndZoneIdsAreInvalidArgs()
	{
		const QString channel = underTestChannel();

		// Malformed: the suffix is not a number at all.
		const ControlResult malformedMacro = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-x")},
				{QStringLiteral("value"), 0.5}});
		QVERIFY(!malformedMacro.ok);
		QCOMPARE(malformedMacro.errorKind, ControlErrorKind::InvalidArgs);

		// Malformed: the prefix itself is wrong ("macro0", not "macro-0").
		const ControlResult badPrefixMacro = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro0")},
				{QStringLiteral("value"), 0.5}});
		QVERIFY(!badPrefixMacro.ok);
		QCOMPARE(badPrefixMacro.errorKind, ControlErrorKind::InvalidArgs);

		// The same for a zone, on the verb that takes a zone id. It is refused
		// before anything is removed, so the fixture's zones are intact either way.
		const ControlResult malformedZone = revtest::run(QStringLiteral("rack.zone_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("zone"), QStringLiteral("zone-x")}});
		QVERIFY(!malformedZone.ok);
		QCOMPARE(malformedZone.errorKind, ControlErrorKind::InvalidArgs);

		// Well-formed but ABSENT stays not_found: the id parsed, the rack has no
		// such macro. This is the case the old single-kind refusal got right, and
		// it must not be flattened into invalid_args by this fix.
		const ControlResult absentMacro = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-99")},
				{QStringLiteral("value"), 0.5}});
		QVERIFY(!absentMacro.ok);
		QCOMPARE(absentMacro.errorKind, ControlErrorKind::NotFound);

		printEvidence("malformed-rack-ids",
			QStringLiteral("macro-x / macro0 / zone-x -> invalid_args; macro-99 -> not_found"));
	}

	//! Integer index fields must reject values that cannot be represented by
	//! the handlers' int indices before any floating-point narrowing occurs.
	void integerIndexArgumentsAreBoundedToInt()
	{
		const QString channel = underTestChannel();
		const int macroIndex = rackUnderTest().macros().addMacro(QStringLiteral("Range guard"), 0.0f);
		const QString macro = QStringLiteral("macro-%1").arg(macroIndex);
		const double aboveInt = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
		const ControlResult rejected = revtest::run(QStringLiteral("rack.macro_target_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), macro},
				{QStringLiteral("target"), aboveInt}});
		QVERIFY(rackUnderTest().macros().removeMacro(macroIndex));
		QVERIFY(!rejected.ok);
		QCOMPARE(rejected.errorKind, ControlErrorKind::InvalidArgs);

		const struct
		{
			QString command;
			QString argument;
		} indices[] = {
			{QStringLiteral("rack.remove_chain"), QStringLiteral("chain")},
			{QStringLiteral("rack.set_selected"), QStringLiteral("chain")},
			{QStringLiteral("rack.macro_target_add"), QStringLiteral("chain")},
			{QStringLiteral("rack.macro_target_add"), QStringLiteral("effect")},
			{QStringLiteral("rack.macro_target_remove"), QStringLiteral("target")},
		};
		for (const auto& index : indices)
		{
			const ControlCommand* command = ControlRegistry::instance()->command(index.command);
			QVERIFY(command != nullptr);
			const QJsonObject property = command->argsSchema.value(QStringLiteral("properties"))
				.toObject().value(index.argument).toObject();
			QCOMPARE(property.value(QStringLiteral("minimum")).toDouble(),
				static_cast<double>(std::numeric_limits<int>::min()));
			QCOMPARE(property.value(QStringLiteral("maximum")).toDouble(),
				static_cast<double>(std::numeric_limits<int>::max()));
		}
	}

	//! A repeated selector value is not an undo step of its own.
	void aNoOpSelectionDoesNotConsumeUndo()
	{
		QVERIFY(revtest::run(QStringLiteral("control.set_undo_coalescing"),
			{{QStringLiteral("window_ms"), 0}}).ok);
		Rack& rack = rackUnderTest();
		const int previous = rack.selectedChain();
		const int selected = previous == kDrivenChain ? Rack::Parallel : kDrivenChain;
		const QString channel = underTestChannel();
		const QJsonObject args{{QStringLiteral("channel"), channel},
			{QStringLiteral("chain"), selected}};
		QVERIFY2(revtest::run(QStringLiteral("rack.set_selected"), args).ok,
			"the selection change failed");
		QVERIFY2(revtest::run(QStringLiteral("rack.set_selected"), args).ok,
			"the repeated selection failed");

		REV_UNDO_OR_FAIL();
		QCOMPARE(rack.selectedChain(), previous);
	}

	//! A repeated macro value with no changed target write is not an undo step.
	void aNoOpMacroSetDoesNotConsumeUndo()
	{
		QVERIFY(revtest::run(QStringLiteral("control.set_undo_coalescing"),
			{{QStringLiteral("window_ms"), 0}}).ok);
		RackMacros& macros = rackUnderTest().macros();
		const int macro = macros.addMacro(QStringLiteral("Undo no-op"), 0.0f);
		QVERIFY(macro >= 0);
		QCOMPARE(macros.addTarget(macro, targetOf(kGainName, 0.25f, 0.75f)), 0);
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(kGainMaximum);
		const QJsonObject args{{QStringLiteral("channel"), underTestChannel()},
			{QStringLiteral("macro"), macroId(macro)}, {QStringLiteral("value"), 1.0}};
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set"), args).ok,
			"the macro change failed");
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set"), args).ok,
			"the repeated macro value failed");

		REV_UNDO_OR_FAIL();
		QCOMPARE(macros.macro(macro)->value, 0.0f);
		QCOMPARE(gain->value(), kGainMaximum);
		QVERIFY(macros.removeMacro(macro));
	}

	//! A macro with an unchanged scalar still needs undo when it repairs a target.
	void anUnchangedMacroScalarStillUndoesItsParameterWrite()
	{
		RackMacros& macros = rackUnderTest().macros();
		const int macro = macros.addMacro(QStringLiteral("Repair parameter"), 0.5f);
		QVERIFY(macro >= 0);
		QCOMPARE(macros.addTarget(macro, targetOf(kGainName, 0.0f, 1.0f)), 0);
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(10.0f);
		const ControlResult applied = revtest::run(QStringLiteral("rack.macro_set"),
			{{QStringLiteral("channel"), underTestChannel()}, {QStringLiteral("macro"), macroId(macro)},
				{QStringLiteral("value"), 0.5}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QCOMPARE(gain->value(), 50.0f);

		REV_UNDO_OR_FAIL();
		QCOMPARE(macros.macro(macro)->value, 0.5f);
		QCOMPARE(gain->value(), 10.0f);
		QVERIFY(macros.removeMacro(macro));
	}

	//! Cluster 5524: a project load and a model initialization are not a person's
	//! change, so neither may become the "last touched" model (ED-191). This is
	//! the same contract as the macro pair above - the store names what a command
	//! wrote - read where a load writes instead.
	void aLoadDoesNotClaimTheLastTouchedModel()
	{
		FloatModel* const edited = gainModel();
		QVERIFY(edited != nullptr);
		edited->setValue(edited->value() == 10.0f ? 11.0f : 10.0f);
		QCOMPARE(AutomatableModel::lastTouched(), edited->id());

		// The initial value a model is BUILT with is not an edit.
		FloatModel initialised(0.f, 0.f, 100.f, 1.f);
		initialised.setInitValue(42.f);
		QCOMPARE(initialised.value(), 42.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited->id());

		// The attribute form of a load (the shape a plain control is saved in).
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("control"));
		doc.appendChild(element);
		element.setAttribute(QStringLiteral("value"), QStringLiteral("42"));
		FloatModel loaded(0.f, 0.f, 100.f, 1.f);
		loaded.loadSettings(element, QStringLiteral("value"));
		QCOMPARE(loaded.value(), 42.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited->id());

		// The element form.
		QDomElement child = doc.createElement(QStringLiteral("value"));
		child.setAttribute(QStringLiteral("id"), QString::number(loaded.id()));
		child.setAttribute(QStringLiteral("value"), QStringLiteral("77"));
		element.appendChild(child);
		loaded.loadSettings(element, QStringLiteral("value"));
		QCOMPARE(loaded.value(), 77.f);
		QCOMPARE(AutomatableModel::lastTouched(), edited->id());

		// The reset-on-absence form is still a load.
		loaded.loadSettings(doc.createElement(QStringLiteral("empty")), QStringLiteral("value"));
		QCOMPARE(AutomatableModel::lastTouched(), edited->id());

		// A person writing the loaded model claims the store again.
		loaded.setValue(11.f);
		QCOMPARE(AutomatableModel::lastTouched(), loaded.id());
	}

	//! Repeating an unchanged XY pair must leave the first pair edit on top.
	void aNoOpMacroPairDoesNotConsumeUndo()
	{
		QVERIFY(revtest::run(QStringLiteral("control.set_undo_coalescing"),
			{{QStringLiteral("window_ms"), 0}}).ok);
		RackMacros& macros = rackUnderTest().macros();
		const int xMacro = macros.addMacro(QStringLiteral("Undo no-op X"), 0.0f);
		const int yMacro = macros.addMacro(QStringLiteral("Undo no-op Y"), 0.0f);
		QVERIFY(xMacro >= 0 && yMacro >= 0);
		QCOMPARE(macros.addTarget(xMacro, targetOf(kGainName, 0.0f, 1.0f)), 0);
		QCOMPARE(macros.addTarget(yMacro, targetOf(kPanName, 0.0f, 1.0f)), 0);
		FloatModel* const gain = gainModel();
		FloatModel* const pan = panModel();
		QVERIFY(gain != nullptr && pan != nullptr);
		gain->setValue(10.0f);
		pan->setValue(20.0f);
		gain->setValue(gain->value() == 10.0f ? 11.0f : 10.0f);
		gain->setValue(10.0f);
		const QJsonObject args{{QStringLiteral("channel"), underTestChannel()},
			{QStringLiteral("x_macro"), macroId(xMacro)}, {QStringLiteral("x_value"), 0.25},
			{QStringLiteral("y_macro"), macroId(yMacro)}, {QStringLiteral("y_value"), 0.75}};
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set_pair"), args).ok,
			"the macro pair change failed");
		QCOMPARE(AutomatableModel::lastTouched(), pan->id());
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set_pair"), args).ok,
			"the repeated macro pair failed");

		REV_UNDO_OR_FAIL();
		QCOMPARE(macros.macro(xMacro)->value, 0.0f);
		QCOMPARE(macros.macro(yMacro)->value, 0.0f);
		QCOMPARE(gain->value(), 10.0f);
		QCOMPARE(pan->value(), 20.0f);
		QVERIFY(macros.removeMacro(yMacro));
		QVERIFY(macros.removeMacro(xMacro));
	}

	//! One undo restores a parameter shared by both axes to its pre-pair value.
	void aNoOpSharedParameterMacroPairDoesNotConsumeUndo()
	{
		QVERIFY(revtest::run(QStringLiteral("control.set_undo_coalescing"),
			{{QStringLiteral("window_ms"), 0}}).ok);
		RackMacros& macros = rackUnderTest().macros();
		const int xMacro = macros.addMacro(QStringLiteral("Shared parameter X"), 0.0f);
		const int yMacro = macros.addMacro(QStringLiteral("Shared parameter Y"), 0.0f);
		QVERIFY(xMacro >= 0 && yMacro >= 0);
		QCOMPARE(macros.addTarget(xMacro, targetOf(kGainName, 0.0f, 1.0f)), 0);
		QCOMPARE(macros.addTarget(yMacro, targetOf(kGainName, 0.0f, 1.0f)), 0);
		FloatModel* const gain = gainModel();
		FloatModel* const pan = panModel();
		QVERIFY(gain != nullptr && pan != nullptr);
		gain->setValue(gain->value() == 10.0f ? 11.0f : 10.0f);
		gain->setValue(10.0f);
		pan->setValue(pan->value() == 20.0f ? 21.0f : 20.0f);
		pan->setValue(20.0f);
		const QJsonObject args{{QStringLiteral("channel"), underTestChannel()},
			{QStringLiteral("x_macro"), macroId(xMacro)}, {QStringLiteral("x_value"), 0.25},
			{QStringLiteral("y_macro"), macroId(yMacro)}, {QStringLiteral("y_value"), 0.75}};
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set_pair"), args).ok,
			"the macro pair change failed");
		QCOMPARE(gain->value(), 75.0f);
		QCOMPARE(AutomatableModel::lastTouched(), gain->id());
		QVERIFY2(revtest::run(QStringLiteral("rack.macro_set_pair"), args).ok,
			"the repeated macro pair failed");

		REV_UNDO_OR_FAIL();
		QCOMPARE(macros.macro(xMacro)->value, 0.0f);
		QCOMPARE(macros.macro(yMacro)->value, 0.0f);
		QCOMPARE(gain->value(), 10.0f);
		QVERIFY(macros.removeMacro(yMacro));
		QVERIFY(macros.removeMacro(xMacro));
	}
};

QTEST_GUILESS_MAIN(RackMacrosTest)
#include "RackMacrosTest.moc"
