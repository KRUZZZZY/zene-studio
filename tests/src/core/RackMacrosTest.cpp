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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "AllocationProbe.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "RackMacros.h"
#include "RackTestSupport.h"
#include "RackZones.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;

class RackMacrosTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

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

	//! Macros and zones are NOT on the audio path: with both configured, the
	//! rack's own process() still allocates nothing.
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
		const QString channel = revtest::channelId(kChannel);

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

	//! Typed refusals: a caller is told what is wrong, and never gets "ok" for a
	//! selection or a bind that cannot exist.
	void theSurfaceRefusesWhatCannotExist()
	{
		const QString channel = revtest::channelId(kChannel);

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
};

QTEST_GUILESS_MAIN(RackMacrosTest)
#include "RackMacrosTest.moc"
