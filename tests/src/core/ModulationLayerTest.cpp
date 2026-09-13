/*
 * ModulationLayerTest.cpp - the ENGINE half of the #602 modulation layer: the
 *                           LFO arithmetic, the layer's own bounds, the target
 *                           resolver, the per-block relative write, the
 *                           allocation-free audio path and the persistence
 *                           round trip (docs/MODULATION.md).
 *
 * The SURFACE half - the ten registered commands, their schemas, their A16
 * classes and their inverses - is ControlModulatorCommandsTest.cpp. They are
 * two files for the same reason the rack group's are: this fork's file-length
 * ratchet measures a file as a unit.
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
#include <QString>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "AllocationProbe.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "ModulationLayer.h"
#include "ModulationTestSupport.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace modtest;


class ModulationLayerTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	/*! Every test starts from a layer nobody has edited.
	 *
	 *  NOT Song::clearProject(): that rebuilds the mixer, and the fixture this
	 *  file shares with the rack group (RackTestSupport.h) is built once in
	 *  initTestCase - clearing the project would take the rack chain and its
	 *  two parameters out from under every test after the first.
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
	}

	//! A route is resolved through the rack lane's own addressing, so a
	//! modulator route and a macro target cannot disagree about what a
	//! parameter name means.
	void routesResolveThroughTheRackAddress()
	{
		QVERIFY(gainModel() != nullptr);
		QString why;
		QCOMPARE(static_cast<const void*>(modulationTargetModel(routeOf("Gain", 0.5f), &why)),
			static_cast<const void*>(gainModel()));
		QCOMPARE(static_cast<const void*>(modulationTargetModel(routeOf("Panning", 0.5f), &why)),
			static_cast<const void*>(panModel()));
		QVERIFY(modulationTargetModel(routeOf("NoSuchParameter", 0.5f), &why) == nullptr);
		QVERIFY(!why.isEmpty());
		// A channel that is not there is a typed refusal from the resolveRack
		// path, not a crash and not a guess.
		ModulationRoute stray = routeOf("Gain", 0.5f);
		stray.channel = kChannels + 3;
		QVERIFY(modulationTargetModel(stray, &why) == nullptr);
		QVERIFY(!why.isEmpty());
	}

	//! THE LOAD-BEARING CASE. The same depth moves two parameters with
	//! different ranges and different values by the same FRACTION of their own
	//! range, which is what "independent of each parameter's absolute value"
	//! means.
	void aBlockWritesTheRelativeOffset()
	{
		FloatModel* const gain = gainModel();
		FloatModel* const pan = panModel();
		QVERIFY(gain != nullptr && pan != nullptr);
		// Bases chosen so BOTH half cycles stay inside each range: the clamp
		// is `theBlockClampsAtTheRange`'s subject, not this test's.
		gain->setValue(50.0f, false);
		pan->setValue(0.0f, false);

		ModulationRuntime runtime;
		ModulationRuntime::Entry first;
		first.model = gain;
		first.modulator = 0;
		first.depth = 0.25f;
		first.base = 50.0f;
		first.minimum = kGainMinimum;
		first.maximum = kGainMaximum;
		ModulationRuntime::Entry second = first;
		second.model = pan;
		second.base = 0.0f;
		second.minimum = kPanMinimum;
		second.maximum = kPanMaximum;
		runtime.entries[0] = first;
		runtime.entries[1] = second;
		runtime.entryCount = 2;
		runtime.sources[0] = sineAt(1.0f, 0.0f, false);
		runtime.sourceCount = 1;

		// Quarter of a cycle: the sine is +1, so each writes base + depth*range.
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 50.0f + 0.25f * 100.0f);
		QCOMPARE(valueOf(pan), 0.0f + 0.25f * 200.0f);
		// Three quarters: -1, so the offset is the same size in the other
		// direction - and the SAME fraction of each range.
		applyModulationBlock(runtime, 0.75);
		QCOMPARE(valueOf(gain), 50.0f - 0.25f * 100.0f);
		QCOMPARE(valueOf(pan), 0.0f - 0.25f * 200.0f);
		evidence("relative-offset",
			QStringLiteral("depth 0.25 on 0..100 and -100..100 moved both by 25% of their range"));
	}

	//! The write is clamped to the parameter's own range, so a deep modulator
	//! on a parameter near its limit cannot push it out of bounds.
	void theBlockClampsAtTheRange()
	{
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		ModulationRuntime runtime;
		ModulationRuntime::Entry entry;
		entry.model = gain;
		entry.modulator = 0;
		entry.depth = 1.0f;
		entry.base = 90.0f;
		entry.minimum = kGainMinimum;
		entry.maximum = kGainMaximum;
		runtime.entries[0] = entry;
		runtime.entryCount = 1;
		runtime.sources[0] = sineAt(1.0f, 0.0f, false);
		runtime.sourceCount = 1;
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), kGainMaximum);
		applyModulationBlock(runtime, 0.75);
		QCOMPARE(valueOf(gain), kGainMinimum);
	}

	//! An empty or switched-off layer is exactly today's engine: nothing is
	//! written, so the whole path is a no-op - the property the release's
	//! behaviour-preservation claim rests on.
	void anEmptyOrInactiveLayerWritesNothing()
	{
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(42.0f, false);

		applyModulationBlock(ModulationRuntime{}, 0.25);
		QCOMPARE(valueOf(gain), 42.0f);

		ModulationRuntime runtime;
		ModulationRuntime::Entry entry;
		entry.model = gain;
		entry.modulator = 0;
		entry.depth = 1.0f;
		entry.base = 42.0f;
		entry.minimum = kGainMinimum;
		entry.maximum = kGainMaximum;
		runtime.entries[0] = entry;
		runtime.entryCount = 1;
		ModulatorSource off = sineAt(1.0f, 0.0f, false);
		off.active = false;
		runtime.sources[0] = off;
		runtime.sourceCount = 1;
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 42.0f);

		// A zero depth is bookkeeping: bound, but driving nothing.
		runtime.sources[0].active = true;
		runtime.entries[0].depth = 0.0f;
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 42.0f);

		// A target destroyed under the audio thread reads null and is skipped.
		runtime.entries[0].depth = 1.0f;
		runtime.entries[0].model = nullptr;
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 42.0f);

		// A modulator index the runtime does not describe is skipped too.
		runtime.entries[0].model = gain;
		runtime.entries[0].modulator = 7;
		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 42.0f);
	}

	//! Workspace rule 4, measured: the block path allocates nothing.
	void theAudioPathAllocatesNothing()
	{
		ModulationRuntime runtime;
		for (int i = 0; i < 2; ++i)
		{
			ModulationRuntime::Entry entry;
			entry.model = (i == 0) ? static_cast<AutomatableModel*>(gainModel())
								   : static_cast<AutomatableModel*>(panModel());
			QVERIFY(!entry.model.isNull());
			entry.modulator = 0;
			entry.depth = 0.5f;
			entry.base = i == 0 ? 50.0f : 0.0f;
			entry.minimum = i == 0 ? kGainMinimum : kPanMinimum;
			entry.maximum = i == 0 ? kGainMaximum : kPanMaximum;
			runtime.entries[static_cast<std::size_t>(i)] = entry;
		}
		runtime.entryCount = 2;
		runtime.sources[0] = sineAt(2.0f, 0.0f, false);
		runtime.sourceCount = 1;
		QVERIFY2(runtime.active(), "the runtime is not on the path - the allocation claim would be vacuous");

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			applyModulationBlock(runtime, 0.001 * static_cast<double>(block));
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		evidence("modulation-allocations",
			QStringLiteral("allocations=%1 over 64 blocks").arg(static_cast<qulonglong>(allocations)));
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! restoreModulationBases hands every target back, which is what
	//! Song::stop() and every layer edit call it for.
	void restoringHandsTheParameterBack()
	{
		FloatModel* const gain = gainModel();
		QVERIFY(gain != nullptr);
		gain->setValue(20.0f, false);
		ModulationRuntime runtime;
		ModulationRuntime::Entry entry;
		entry.model = gain;
		entry.modulator = 0;
		entry.depth = 0.25f;
		entry.base = 20.0f;
		entry.minimum = kGainMinimum;
		entry.maximum = kGainMaximum;
		runtime.entries[0] = entry;
		runtime.entryCount = 1;
		runtime.sources[0] = sineAt(1.0f, 0.0f, false);
		runtime.sourceCount = 1;

		applyModulationBlock(runtime, 0.25);
		QCOMPARE(valueOf(gain), 45.0f);
		restoreModulationBases(runtime);
		QCOMPARE(valueOf(gain), 20.0f);
	}

	//! The whole authored layer survives a save/load round trip, and an empty
	//! layer writes nothing at all - the property that keeps a project which
	//! never used a modulator byte-identical.
	void theLayerPersistsAndReloads()
	{
		ModulationLayer layer;
		QVERIFY(!layer.shouldPersist());
		QDomDocument empty;
		QDomElement emptyRoot = empty.createElement(QStringLiteral("song"));
		empty.appendChild(emptyRoot);
		// An empty layer writes NO element at all, which is the property that
		// keeps a project that never used a modulator byte-identical.
		QVERIFY(!layer.saveSettings(empty, emptyRoot));
		QVERIFY(emptyRoot.childNodes().isEmpty());

		const int sine = layer.addModulator(QStringLiteral("Wobble"), sineAt(2.5f, 0.125f, true));
		const int saw = layer.addModulator(QStringLiteral("Ramp"), sineAt(0.25f, 0.0f, false));
		QCOMPARE(sine, 0);
		QCOMPARE(saw, 1);
		ModulatorSource ramp;
		ramp.shape = ModulationShape::Saw;
		ramp.rateHz = 0.25f;
		ramp.phase = 0.5f;
		layer.setSource(saw, ramp);
		QCOMPARE(layer.addRoute(sine, routeOf("Gain", 0.75f)), 0);
		QCOMPARE(layer.addRoute(sine, routeOf("Panning", -0.5f)), 1);
		QCOMPARE(layer.addRoute(saw, routeOf("Gain", 0.25f)), 0);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("song"));
		doc.appendChild(root);
		QVERIFY(layer.saveSettings(doc, root));
		QDomElement element = root.firstChildElement(QStringLiteral("modulation-layer"));
		QVERIFY(!element.isNull());
		QCOMPARE(element.attribute(QStringLiteral("version")).toInt(), 1);
		QCOMPARE(element.attribute(QStringLiteral("modulators")).toInt(), 2);
		QCOMPARE(element.elementsByTagName(QStringLiteral("modulator")).count(), 2);
		QCOMPARE(element.elementsByTagName(QStringLiteral("route")).count(), 3);

		ModulationLayer reloaded;
		QVERIFY(reloaded.loadSettings(element));
		QCOMPARE(reloaded.modulatorCount(), 2);
		QCOMPARE(reloaded.modulator(0)->name, QStringLiteral("Wobble"));
		QCOMPARE(reloaded.modulator(0)->source.rateHz, 2.5f);
		QCOMPARE(reloaded.modulator(0)->source.phase, 0.125f);
		QCOMPARE(reloaded.modulator(0)->source.unipolar, true);
		QCOMPARE(reloaded.modulator(1)->source.shape, ModulationShape::Saw);
		QCOMPARE(reloaded.modulator(1)->source.phase, 0.5f);
		QCOMPARE(reloaded.modulator(0)->routeCount(), 2);
		QCOMPARE(reloaded.modulator(0)->routes[0].parameter, QStringLiteral("Gain"));
		QCOMPARE(reloaded.modulator(0)->routes[0].depth, 0.75f);
		QCOMPARE(reloaded.modulator(0)->routes[1].depth, -0.5f);
		QCOMPARE(reloaded.modulator(0)->routes[0].channel, kChannel);
		QCOMPARE(reloaded.modulator(0)->routes[0].chain, kDrivenChain);

		// Re-save is stable: the round trip is not lossy in either direction.
		QDomDocument again;
		QDomElement againRoot = again.createElement(QStringLiteral("song"));
		again.appendChild(againRoot);
		QVERIFY(reloaded.saveSettings(again, againRoot));
		QCOMPARE(again.toString(), doc.toString());
		evidence("persistence-round-trip",
			QStringLiteral("2 modulators, 3 routes, saved = loaded = re-saved"));
	}

	//! A corrupt or empty block degrades to NO layer, never to a wrong one -
	//! the same rule the tempo map's reader follows.
	void aCorruptBlockLoadsAsAnEmptyLayer()
	{
		ModulationLayer layer;
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("modulation-layer"));
		doc.appendChild(root);
		QCOMPARE(layer.loadSettings(root), false);
		QCOMPARE(layer.modulatorCount(), 0);

		// A modulator whose rate is out of bounds is refused, not repaired.
		QDomElement bad = doc.createElement(QStringLiteral("modulator"));
		bad.setAttribute(QStringLiteral("name"), QStringLiteral("Broken"));
		bad.setAttribute(QStringLiteral("shape"), QStringLiteral("sine"));
		bad.setAttribute(QStringLiteral("rate"), QStringLiteral("0"));
		bad.setAttribute(QStringLiteral("phase"), QStringLiteral("0"));
		root.appendChild(bad);
		QCOMPARE(layer.loadSettings(root), false);
		QCOMPARE(layer.modulatorCount(), 0);

		// An unknown shape name is not guessed at either: it falls back to the
		// default the reader declares, and the row is still bounded.
		QDomElement unknown = doc.createElement(QStringLiteral("modulator"));
		unknown.setAttribute(QStringLiteral("name"), QStringLiteral("Odd"));
		unknown.setAttribute(QStringLiteral("shape"), QStringLiteral("fractal"));
		unknown.setAttribute(QStringLiteral("rate"), QStringLiteral("1"));
		unknown.setAttribute(QStringLiteral("phase"), QStringLiteral("0"));
		QDomElement route = doc.createElement(QStringLiteral("route"));
		route.setAttribute(QStringLiteral("channel"), kChannel);
		route.setAttribute(QStringLiteral("chain"), kDrivenChain);
		route.setAttribute(QStringLiteral("effect"), 0);
		route.setAttribute(QStringLiteral("parameter"), QStringLiteral("Gain"));
		route.setAttribute(QStringLiteral("depth"), QStringLiteral("0.5"));
		unknown.appendChild(route);
		root.appendChild(unknown);
		QVERIFY(layer.loadSettings(root));
		QCOMPARE(layer.modulatorCount(), 1);
		QCOMPARE(layer.modulator(0)->source.shape, ModulationShape::Sine);
		// A route with no parameter name is dropped: it names nothing.
		QDomElement nameless = doc.createElement(QStringLiteral("route"));
		nameless.setAttribute(QStringLiteral("channel"), kChannel);
		unknown.appendChild(nameless);
		QVERIFY(layer.loadSettings(root));
		QCOMPARE(layer.modulator(0)->routeCount(), 1);
	}
};

QTEST_GUILESS_MAIN(ModulationLayerTest)
#include "ModulationLayerTest.moc"
