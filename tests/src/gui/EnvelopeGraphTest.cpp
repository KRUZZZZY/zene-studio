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

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "EnvelopeGraph.h"

using namespace lmms;
using namespace lmms::gui;

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
};

QTEST_MAIN(EnvelopeGraphTest)
#include "EnvelopeGraphTest.moc"
