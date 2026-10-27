/*
 * EnvelopeGraphTest.cpp - the envelope graph toggles a negative amount off
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

#include <array>

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "EnvelopeGraph.h"
#include "Graph.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! Convolves {1, 2, 3, 4} with the kernel {2, 0} at the given centre offset, then checks
//! every slot against the expected values (BUG new-p9-162). Each slot is a doubled copy of
//! the input rotated by the offset; a slot that is never written keeps its input value,
//! so a missed write shows up as a mismatch.
void expectConvolve(int centerOffset, const std::array<float, 4>& expected)
{
	constexpr std::array<float, 4> input{1.0f, 2.0f, 3.0f, 4.0f};
	constexpr std::array<float, 2> kernel{2.0f, 0.0f};

	graphModel graph(-1.0f, 1.0f, static_cast<int>(input.size()), nullptr);
	graph.setSamples(input.data());
	graph.convolve(kernel.data(), static_cast<int>(kernel.size()), centerOffset);

	for (std::size_t i = 0; i < expected.size(); i++)
	{
		QCOMPARE(graph.samples()[i], expected[i]);
	}
}

} // namespace

class EnvelopeGraphTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void leftClickTurnsOffNegativeAmount()
	{
		EnvelopeAndLfoParameters params(0.0f, nullptr);
		EnvelopeGraph graph(nullptr);
		graph.setModel(&params);
		graph.resize(100, 100);
		graph.show();

		params.getAmountModel().setValue(-0.5f);
		QTest::mouseClick(&graph, Qt::LeftButton);
		QCOMPARE(params.getAmountModel().value(), 0.0f);

		QTest::mouseClick(&graph, Qt::LeftButton);
		QCOMPARE(params.getAmountModel().value(), 1.0f);
	}

	// graphModel::convolve: every centre offset, negative ones included, lands inside the samples.
	void convolveZeroOffsetKeepsSamples()
	{
		expectConvolve(0, {2.0f, 4.0f, 6.0f, 8.0f});
	}

	void convolvePositiveOffsetRotatesRight()
	{
		expectConvolve(1, {8.0f, 2.0f, 4.0f, 6.0f});
	}

	void convolveNegativeOffsetRotatesLeft()
	{
		// Slot i moves to (i - 1) mod 4, so the first sample wraps to the end.
		expectConvolve(-1, {4.0f, 6.0f, 8.0f, 2.0f});
	}

	void convolveNegativeOffsetEqualToLengthIsZeroShift()
	{
		expectConvolve(-4, {2.0f, 4.0f, 6.0f, 8.0f});
	}

	void convolveNegativeOffsetBeyondLengthWraps()
	{
		// -5 mod 4 is -1, so this is the same rotation as -1.
		expectConvolve(-5, {4.0f, 6.0f, 8.0f, 2.0f});
	}
};

QTEST_MAIN(EnvelopeGraphTest)
#include "EnvelopeGraphTest.moc"
