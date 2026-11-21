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

void RackMacrosTest::theSurfaceCreatesDrivesAndUndoesAMacro() {
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
void RackMacrosTest::theIdContractCountsEveryChainThatHandsOutFxIds() {
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
void RackMacrosTest::theSurfaceRefusesWhatCannotExist() {
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
void RackMacrosTest::removingAChainReindexesTheMacroTargets() {
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
void RackMacrosTest::malformedMacroAndZoneIdsAreInvalidArgs() {
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
void RackMacrosTest::integerIndexArgumentsAreBoundedToInt() {
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
void RackMacrosTest::aNoOpSelectionDoesNotConsumeUndo() {
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
void RackMacrosTest::aNoOpMacroSetDoesNotConsumeUndo() {
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
void RackMacrosTest::anUnchangedMacroScalarStillUndoesItsParameterWrite() {
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
void RackMacrosTest::aLoadDoesNotClaimTheLastTouchedModel() {
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
void RackMacrosTest::aNoOpMacroPairDoesNotConsumeUndo() {
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
void RackMacrosTest::aNoOpSharedParameterMacroPairDoesNotConsumeUndo() {
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
