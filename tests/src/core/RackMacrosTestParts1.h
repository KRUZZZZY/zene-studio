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

// Complete fixture method definitions; included once after the fixture declaration.

void RackMacrosTest::initTestCase() {
	QString why;
	QVERIFY2(initRackFixture(&why), qPrintable(why));
}
void RackMacrosTest::cleanupTestCase() {
	revtest::run(QStringLiteral("control.set_undo_coalescing"),
		{{QStringLiteral("window_ms"), control::UndoCoalesceWindowMs}});
	teardownRackFixture();
}
void RackMacrosTest::macrosDriveParametersThroughTheirWindows() {
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
void RackMacrosTest::aCurveShapesTheResponse() {
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
void RackMacrosTest::aMacroSkipsTargetsItCannotResolve() {
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
void RackMacrosTest::aNonfiniteMacroValueIsRefused() {
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
void RackMacrosTest::aNonfiniteMacroTargetNeverReachesAParameter() {
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
void RackMacrosTest::aProjectLoadedMacroIsValidated() {
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
void RackMacrosTest::configuringMacrosAndZonesLeavesTheAudioPathAllocatingNothing() {
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
void RackMacrosTest::theGroupIsRegisteredWithSchemasAndA16Classes() {
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
