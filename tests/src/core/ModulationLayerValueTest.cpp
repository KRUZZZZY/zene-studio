/*
 * ModulationLayerValueTest.cpp - the #602 modulation layer's PURE half: the LFO arithmetic, the source
 * validation and the layer's own bounds. No Engine, no Mixer, no registry -
 * values only, so a change to the arithmetic fails here and nowhere else.
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

using namespace modtest;

// This file deliberately starts no Engine: every assertion here is over a
// VALUE (the LFO's output for a source, a source's validity, the layer's own
// bounds), which is what makes a change to the arithmetic fail here and
// nowhere else. The engine half is ModulationLayerTest.cpp.


class ModulationLayerValueTest : public QObject
{
	Q_OBJECT
private slots:

	//! The four shapes, their phase offsets, and what unipolar does to them.
	//! Stated as exact values because the whole layer is arithmetic: a route's
	//! depth has to mean the same thing on every shape.
	void lfoShapesAndPolarity()
	{
		// Sine: 0 at phase 0, +1 at a quarter cycle, -1 at three quarters.
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.0f, false), 0.0)), 0.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.0f, false), 0.25)), 1.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.0f, false), 0.75)), -1.0);
		// A rate of 2 Hz reaches the same phase in half the time.
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(2.0f, 0.0f, false), 0.125)), 1.0);
		// A phase offset is where in the cycle the modulator starts.
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.25f, false), 0.0)), 1.0);

		// Triangle starts at 0 and rises: +1 at a quarter cycle, back to 0 at
		// half, -1 at three quarters. It is linear where sine is not.
		ModulatorSource triangle = sineAt(1.0f, 0.0f, false);
		triangle.shape = ModulationShape::Triangle;
		QCOMPARE(rounded(ModulationLayer::outputAt(triangle, 0.0)), 0.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(triangle, 0.125)), 0.5);
		QCOMPARE(rounded(ModulationLayer::outputAt(triangle, 0.25)), 1.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(triangle, 0.5)), 0.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(triangle, 0.75)), -1.0);

		// Saw starts at -1 and rises to +1; square starts at +1 and drops.
		ModulatorSource saw = sineAt(1.0f, 0.0f, false);
		saw.shape = ModulationShape::Saw;
		QCOMPARE(rounded(ModulationLayer::outputAt(saw, 0.0)), -1.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(saw, 0.5)), 0.0);
		ModulatorSource square = sineAt(1.0f, 0.0f, false);
		square.shape = ModulationShape::Square;
		QCOMPARE(rounded(ModulationLayer::outputAt(square, 0.0)), 1.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(square, 0.6)), -1.0);

		// Unipolar maps -1..1 onto 0..1, which is what makes a modulator an
		// offset-only source when that is what a caller wants.
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.0f, true), 0.75)), 0.0);
		QCOMPARE(rounded(ModulationLayer::outputAt(sineAt(1.0f, 0.0f, true), 0.25)), 1.0);
		evidence("lfo-shapes", QStringLiteral("4 shapes, phase offsets, unipolar"));
	}

	//! A source outside the layer's own bounds is refused rather than clamped:
	//! a rate of 0 or a phase of 3 would run something nobody authored.
	void sourcesAreValidated()
	{
		ModulationLayer layer;
		QString why;
		QVERIFY(layer.validSource(sineAt(1.0f, 0.0f, false), &why));
		QVERIFY(!layer.validSource(sineAt(0.0f, 0.0f, false), &why));
		QVERIFY(why.contains(QStringLiteral("rate")));
		QVERIFY(!layer.validSource(sineAt(1000.0f, 0.0f, false), &why));
		QVERIFY(!layer.validSource(sineAt(1.0f, 1.0f, false), &why));
		QVERIFY(why.contains(QStringLiteral("phase")));
		QVERIFY(layer.validSource(sineAt(ModulationLayer::MaxRateHz, 0.99f, true), &why));
	}

	//! The layer is bounded, so a project file can never make it unbounded.
	void modulatorsAndRoutesAreBounded()
	{
		ModulationLayer layer;
		for (int i = 0; i < ModulationLayer::MaxModulators; ++i)
		{
			QCOMPARE(layer.addModulator(QStringLiteral("m%1").arg(i), sineAt(1.0f, 0.0f, false)), i);
		}
		QCOMPARE(layer.addModulator(QStringLiteral("one-too-many"), sineAt(1.0f, 0.0f, false)), -1);
		for (int i = 0; i < ModulationLayer::MaxRoutesPerModulator; ++i)
		{
			QCOMPARE(layer.addRoute(0, routeOf("Gain", 0.5f)), i);
		}
		QCOMPARE(layer.addRoute(0, routeOf("Gain", 0.5f)), -1);
		// Removal is by index, and the index a caller gets is the list position.
		QVERIFY(layer.removeRoute(0, 0));
		QCOMPARE(layer.modulator(0)->routeCount(), ModulationLayer::MaxRoutesPerModulator - 1);
		QVERIFY(layer.removeModulator(0));
		QCOMPARE(layer.modulatorCount(), ModulationLayer::MaxModulators - 1);
		// The same address twice is what findRoute is for; the LAYER does not
		// enforce it (the command does, with a typed refusal).
		QVERIFY(layer.findRoute(routeOf("Gain", 0.1f)) == nullptr);
		QVERIFY(layer.findRoute(routeOf("Panning", 0.1f)) == nullptr);
	}

};

QTEST_GUILESS_MAIN(ModulationLayerValueTest)
#include "ModulationLayerValueTest.moc"
