/*
 * ControlModulatorCommandsTest.cpp - the SURFACE half of the #602 modulation
 *                                    layer: the ten registered commands, their
 *                                    schemas, their SPEC A16 classes, their
 *                                    typed refusals and their inverses - plus
 *                                    the note.expression.* group, which is #602's
 *                                    per-note half driving task #601's own
 *                                    fields.
 *
 * The ENGINE half (the LFO arithmetic, the target resolver, the per-block write,
 * the allocation-free path and the persistence round trip) is
 * ModulationLayerTest.cpp. They are two files for the same reason the rack
 * group's are: this fork's file-length ratchet measures a file as a unit.
 *
 * The load-bearing case is `bindDriveUndoAndRebind`: a modulator is created,
 * a parameter is bound to it, the PUBLISHED runtime is applied for a block (the
 * real audio path, minus the Song wrapper), and control.undo then has to take
 * both the route and the parameter back. A modulator that could not be undone
 * would leave a parameter permanently offset, which is the failure this group
 * exists to prevent.
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

#include <cstdio>
#include <limits>

#include "AllocationProbe.h"
#include "ControlModulationSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ModulationLayer.h"
#include "ModulationTestSupport.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace modtest;

namespace
{

ModulationLayer& layer() { return Engine::getSong()->modulationLayer().layer(); }
const ModulationRuntime& runtime() { return Engine::getSong()->modulationLayer().runtime(); }

QJsonObject targetArgs(const QString& modulator, const char* parameter, double depth)
{
	return QJsonObject{{QStringLiteral("modulator"), modulator},
		{QStringLiteral("channel"), channelIdOf()},
		{QStringLiteral("chain"), kDrivenChain},
		{QStringLiteral("effect"), 0},
		{QStringLiteral("parameter"), QString::fromLatin1(parameter)},
		{QStringLiteral("depth"), depth}};
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace


class ControlModulatorCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	/*! Every test starts from a layer nobody has edited - and NOT from
	 *  Song::clearProject(), which rebuilds the mixer and would take the
	 *  shared rack fixture (RackTestSupport.h) out from under the tests that
	 *  follow. The fixture is built once, in initTestCase.
	 */
	void init()
	{
		Engine::getSong()->modulationLayer().edit([](ModulationLayer& layer,
			ModulationRuntime& runtime) {
			layer.clear();
			runtime = ModulationRuntime{};
			return true;
		});
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	//! Both groups declare the contract's parts: a group.verb id, both schemas,
	//! a description, an empty requires (headless parity, SPEC A13) and the
	//! mutating flag the A16 table expects.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("modulator.create"), QStringLiteral("modulator.remove"),
			QStringLiteral("modulator.rate_set"), QStringLiteral("modulator.target_set"),
			QStringLiteral("modulator.depth_set"), QStringLiteral("modulator.target_remove"),
			QStringLiteral("note.expression_set"), QStringLiteral("note.expression_clear")};
		const QStringList all = mutating
			+ QStringList{QStringLiteral("modulator.get_state"),
				QStringLiteral("note.expression_get")};
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
		// The group name is the id's own prefix, which is what the MCP bridge
		// derives its tool grouping from.
		QCOMPARE(registry->command(QStringLiteral("modulator.get_state"))->group,
			QStringLiteral("modulator"));
		QCOMPARE(registry->command(QStringLiteral("note.expression_set"))->group,
			QStringLiteral("note"));
		evidence("group-registered", QStringLiteral("10 commands, ids and schemas"));
	}

	//! A fresh project reports an empty layer that drives nothing - the state
	//! the engine was in before this feature existed.
	void aFreshProjectReportsAnEmptyLayer()
	{
		const QJsonObject state = run(QStringLiteral("modulator.get_state")).result;
		QCOMPARE(state.value(QStringLiteral("modulator_count")).toInt(), 0);
		QCOMPARE(state.value(QStringLiteral("driving")).toInt(), 0);
		QCOMPARE(state.value(QStringLiteral("engine_active")).toBool(), false);
		QCOMPARE(state.value(QStringLiteral("modulators")).toArray().size(), 0);
		QCOMPARE(state.value(QStringLiteral("max_modulators")).toInt(),
			ModulationLayer::MaxModulators);
	}

	//! THE LOAD-BEARING CASE: create, bind, drive a block, undo - twice, so a
	//! re-bind after the undo is proven to work as well.
	void bindDriveUndoAndRebind()
	{
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(40.0f, false);

		const ControlResult created = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Wobble")},
				{QStringLiteral("rate"), 1.0}, {QStringLiteral("shape"), QStringLiteral("sine")}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		const QString id = created.result.value(QStringLiteral("modulator")).toString();
		QCOMPARE(id, QStringLiteral("modulator-0"));

		const ControlResult bound = run(QStringLiteral("modulator.target_set"),
			targetArgs(id, "Gain", 0.25));
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		QCOMPARE(bound.result.value(QStringLiteral("target_count")).toInt(), 1);
		QCOMPARE(runtime().entryCount, 1);
		QCOMPARE(runtime().entries[0].base, 40.0f);

		// The published runtime, applied for one block at a quarter cycle.
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 40.0f + 0.25f * 100.0f);
		evidence("surface-drive",
			QStringLiteral("base 40 + depth 0.25 * range 100 = %1")
				.arg(static_cast<double>(valueOf(gain))));

		// ONE undo takes the route back AND hands the parameter back.
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the bind");
		QCOMPARE(layer().modulator(0)->routeCount(), 0);
		QCOMPARE(runtime().entryCount, 0);
		QCOMPARE(valueOf(gain), 40.0f);
		// And it does not write anything afterwards: the layer drives nothing.
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 40.0f);

		// Re-bind after the undo: the base is re-captured from the value the
		// parameter was handed back to.
		gain->setValue(10.0f, false);
		QVERIFY(run(QStringLiteral("modulator.target_set"), targetArgs(id, "Gain", 0.5)).ok);
		QCOMPARE(runtime().entries[0].base, 10.0f);
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 10.0f + 0.5f * 100.0f);

		// A remove hands the parameter back and drops the write target with it.
		QVERIFY(run(QStringLiteral("modulator.remove"),
			{{QStringLiteral("modulator"), id}}).ok);
		QCOMPARE(layer().modulatorCount(), 0);
		QCOMPARE(runtime().entryCount, 0);
		QCOMPARE(valueOf(gain), 10.0f);
		// Its inverse re-inserts the modulator with the route list intact.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layer().modulatorCount(), 1);
		QCOMPARE(layer().modulator(0)->routeCount(), 1);
		QCOMPARE(runtime().entryCount, 1);
	}

	//! The same depth on two parameters with different ranges: the fraction of
	//! each range is what is shared, which is the whole point of #602.
	void oneModulatorDrivesASetOfParameters()
	{
		FloatModel* const gain = gainModel();
		FloatModel* const pan = panModel();
		QVERIFY(gain != nullptr && pan != nullptr);
		gain->setValue(0.0f, false);
		pan->setValue(0.0f, false);
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Both")},
				{QStringLiteral("rate"), 1.0}}).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 0.2)).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Panning", 0.2)).ok);
		QCOMPARE(runtime().entryCount, 2);
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 20.0f);
		QCOMPARE(valueOf(pan), 40.0f);
		evidence("set-of-parameters",
			QStringLiteral("one depth 0.2 moved 0..100 by 20 and -100..100 by 40"));
	}

	//! depth_set changes the amount and KEEPS the base; rate_set switching the
	//! modulator off hands every target back.
	void depthAndRateKeepTheirPromises()
	{
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(30.0f, false);
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Amount")},
				{QStringLiteral("rate"), 1.0}}).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 0.1)).ok);
		const float base = runtime().entries[0].base;
		QCOMPARE(base, 30.0f);

		QVERIFY(run(QStringLiteral("modulator.depth_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 0}, {QStringLiteral("depth"), 0.4}}).ok);
		// A depth change is not a move of the parameter: the base is kept.
		QCOMPARE(runtime().entries[0].base, base);
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 30.0f + 0.4f * 100.0f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(runtime().entries[0].depth, 0.1f);

		// Switching the modulator off hands the parameter back and writes
		// nothing, while the route stays bound.
		QVERIFY(run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("active"), false}}).ok);
		QCOMPARE(valueOf(gain), 30.0f);
		QCOMPARE(runtime().entries[0].model.isNull(), false);
		applyModulationBlock(runtime(), 0.25);
		QCOMPARE(valueOf(gain), 30.0f);
		QVERIFY(run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("active"), true}, {QStringLiteral("rate"), 4.0},
				{QStringLiteral("shape"), QStringLiteral("square")}}).ok);
		QCOMPARE(layer().modulator(0)->source.rateHz, 4.0f);
		QCOMPARE(layer().modulator(0)->source.shape, ModulationShape::Square);
	}

	//! Cluster new-p18-1: a repeated depth is a successful no-op, so Ctrl+Z
	//! still undoes the real depth edit underneath it.
	void identicalDepthSetDoesNotShadowUndo()
	{
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Depth")}}).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 0.1)).ok);
		QVERIFY(run(QStringLiteral("modulator.depth_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 0}, {QStringLiteral("depth"), 0.4}}).ok);
		QVERIFY(run(QStringLiteral("modulator.depth_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 0}, {QStringLiteral("depth"), 0.4}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layer().modulator(0)->routes[0].depth, 0.1f);
	}

	//! The rate_set sibling follows the same no-op rule as depth_set.
	void identicalRateSetDoesNotShadowUndo()
	{
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Rate")}}).ok);
		QVERIFY(run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("rate"), 2.0}}).ok);
		QVERIFY(run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("rate"), 2.0}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layer().modulator(0)->source.rateHz, 1.0f);
	}

	//! The voice_set sibling must leave the preceding trigger change as one undo.
	void identicalVoiceSetDoesNotShadowUndo()
	{
		const ControlResult track = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const QString trackId = track.result.value(QStringLiteral("track")).toString();
		QVERIFY(!trackId.isEmpty());
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Voice")}}).ok);
		const QJsonObject args{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
			{QStringLiteral("per_voice"), false}, {QStringLiteral("trigger"), trackId}};
		QVERIFY(run(QStringLiteral("modulator.voice_set"), args).ok);
		QVERIFY(run(QStringLiteral("modulator.voice_set"), args).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layer().modulator(0)->source.triggerTrack, -1);
	}

	//! Cluster 201: the route creation contract rejects invalid depth values but
	//! keeps both inclusive endpoints usable.
	void targetSetEnforcesDocumentedDepthRange()
	{
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Depth bounds")}}).ok);
		const ControlResult track = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const ControlResult invalidInstrumentDepth = run(QStringLiteral("modulator.instrument_target_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), track.result.value(QStringLiteral("track")).toString()},
				{QStringLiteral("parameter"), QStringLiteral("Unavailable")},
				{QStringLiteral("depth"), 5.0}});
		QCOMPARE(invalidInstrumentDepth.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(invalidInstrumentDepth.errorMessage.contains(QStringLiteral("'depth'")));
		QCOMPARE(layer().modulator(0)->routeCount(), 0);

		ControlResult result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 5.0));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(layer().modulator(0)->routeCount(), 0);
		QCOMPARE(runtime().entryCount, 0);
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Panning", -5.0));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(layer().modulator(0)->routeCount(), 0);
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain",
				std::numeric_limits<double>::infinity()));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(layer().modulator(0)->routeCount(), 0);

		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 1.0)).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Panning", -1.0)).ok);
		QCOMPARE(layer().modulator(0)->routes[0].depth, 1.0f);
		QCOMPARE(layer().modulator(0)->routes[1].depth, -1.0f);
	}

	//! Cluster 4802: replay the reported target_remove inverse against the same
	//! route, including its depth and per-sample setting.
	void targetRemoveInverseRecreatesCapturedRoute()
	{
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Inverse")}}).ok);
		QJsonObject bind = targetArgs(QStringLiteral("modulator-0"), "Gain", 0.35);
		bind.insert(QStringLiteral("per_sample"), true);
		QVERIFY(run(QStringLiteral("modulator.target_set"), bind).ok);
		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Panning", -0.2)).ok);
		QVERIFY(run(QStringLiteral("modulator.target_remove"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 0}}).ok);
		const ControlRegistry::Transaction* transaction = ControlRegistry::instance()->lastTransaction();
		QVERIFY(transaction != nullptr);
		QCOMPARE(transaction->command, QStringLiteral("modulator.target_remove"));
		const QString inverseOp = transaction->inverse.value(QStringLiteral("op")).toString();
		const QJsonObject inverseArgs = transaction->inverse.value(QStringLiteral("args")).toObject();
		QCOMPARE(inverseOp, QStringLiteral("modulator.target_set"));
		const ControlResult replayed = run(inverseOp, inverseArgs);
		QVERIFY2(replayed.ok, qPrintable(replayed.errorMessage));
		const ModulationRoute& restored = layer().modulator(0)->routes[0];
		const ModulationRoute& following = layer().modulator(0)->routes[1];
		QCOMPARE(restored.channel, underTestChannelId());
		QCOMPARE(restored.chain, kDrivenChain);
		QCOMPARE(restored.effect, 0);
		QCOMPARE(restored.parameter, QStringLiteral("Gain"));
		QCOMPARE(restored.depth, 0.35f);
		QCOMPARE(restored.perSample, true);
		QCOMPARE(following.parameter, QStringLiteral("Panning"));
		QCOMPARE(following.depth, -0.2f);
	}

	//! Every refusal is typed, changes nothing, and records no transaction -
	//! so it cannot shadow the undo of a real edit underneath it.
	void refusalsAreTypedAndChangeNothing()
	{
		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Kept")}}).ok);
		QCOMPARE(layer().modulatorCount(), 1);

		ControlResult result = run(QStringLiteral("modulator.create"), QJsonObject());
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("   ")}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(layer().modulatorCount(), 1);
		result = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Bad")},
				{QStringLiteral("rate"), 500.0}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Bad")},
				{QStringLiteral("shape"), QStringLiteral("fractal")}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Bad")},
				{QStringLiteral("phase"), 2.0}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(layer().modulatorCount(), 1);

		// A malformed id and an absent one are different failures.
		result = run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("one")}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.rate_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-9")}});
		QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(layer().modulatorCount(), 1);

		// A target that does not resolve, an empty parameter name, a malformed
		// channel and a depth outside -1..1.
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "NoSuchParameter", 0.5));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "", 0.5));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.target_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("channel"), QStringLiteral("channel-1")},
				{QStringLiteral("effect"), 0}, {QStringLiteral("parameter"), QStringLiteral("Gain")},
				{QStringLiteral("depth"), 0.5}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "garbage", 0.5));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);

		QVERIFY(run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 0.5)).ok);
		// One parameter has one depth: the same address twice is refused.
		result = run(QStringLiteral("modulator.target_set"),
			targetArgs(QStringLiteral("modulator-0"), "Gain", 0.9));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(result.errorMessage.contains(QStringLiteral("already")));
		QCOMPARE(layer().modulator(0)->routeCount(), 1);

		// A target index that is not there, and a depth out of range.
		result = run(QStringLiteral("modulator.depth_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 4}, {QStringLiteral("depth"), 0.5}});
		QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
		result = run(QStringLiteral("modulator.depth_set"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 0}, {QStringLiteral("depth"), 3.0}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("modulator.target_remove"),
			{{QStringLiteral("modulator"), QStringLiteral("modulator-0")},
				{QStringLiteral("target"), 4}});
		QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(layer().modulator(0)->routeCount(), 1);

		// A refused call records NO transaction.
		QVERIFY(ControlRegistry::instance()->lastTransaction() != nullptr);
		const QString recorded = ControlRegistry::instance()->lastTransaction()->command;
		QCOMPARE(recorded, QStringLiteral("modulator.target_set"));
	}

	//! The contract table classifies every id of both groups, and the registry
	//! stamps the class the table states - not the one a handler claims.
	void contractRowsClassifyTheGroup()
	{
		const QStringList action = {
			QStringLiteral("modulator.create"), QStringLiteral("modulator.remove"),
			QStringLiteral("modulator.rate_set"), QStringLiteral("modulator.target_set"),
			QStringLiteral("modulator.depth_set"), QStringLiteral("modulator.target_remove")};
		for (const QString& id : action)
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(row->mechanism.contains(QStringLiteral("action checkpoint")));
		}
		for (const QString& id : {QStringLiteral("note.expression_set"),
				QStringLiteral("note.expression_clear")})
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->mechanism.contains(QStringLiteral("MidiClip checkpoint")));
		}
		for (const QString& id : {QStringLiteral("modulator.get_state"),
				QStringLiteral("note.expression_get")})
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY(row != nullptr);
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("not_mutating"));
		}

		QVERIFY(run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Stamped")}}).ok);
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
		QCOMPARE(tx->reversible, true);
		// The recorded inverse names a real command a reader can re-issue.
		QCOMPARE(tx->inverse.value(QStringLiteral("op")).toString(),
			QStringLiteral("modulator.remove"));
		QCOMPARE(tx->before.value(QStringLiteral("modulator_count")).toInt(), 0);
	}

};

QTEST_GUILESS_MAIN(ControlModulatorCommandsTest)
#include "ControlModulatorCommandsTest.moc"
