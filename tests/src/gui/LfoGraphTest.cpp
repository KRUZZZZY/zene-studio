/*
 * LfoGraphTest.cpp - the LFO label backing follows the rendered font
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFontMetrics>
#include <QPainter>

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "FontHelper.h"
#include "LfoGraph.h"

using namespace lmms;
using namespace lmms::gui;

class LfoGraphTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void labelBackingUsesAdjustedFontMetrics()
	{
		EnvelopeAndLfoParameters params(0.f, nullptr);

		LfoGraph graph(nullptr);
		LfoGraph background(nullptr);
		graph.setModel(&params);
		graph.resize(400, 200);
		background.resize(graph.size());
		graph.show();
		background.show();

		const QImage rendered = graph.grab().toImage().convertToFormat(QImage::Format_ARGB32);
		const QImage backgroundImage = background.grab().toImage().convertToFormat(QImage::Format_ARGB32);
		QVERIFY(!rendered.isNull());
		QCOMPARE(rendered.size(), backgroundImage.size());

		const QString infoText = QStringLiteral("0.500 Hz");
		const QPoint textPosition(4, graph.height() - 6);
		const QFont originalFont = graph.font();
		const QFont adjustedFont = adjustedToPixelSize(originalFont, graph.height() * 0.2);
		const QRect expectedRect = QFontMetrics(adjustedFont).boundingRect(infoText).translated(textPosition);
		const QRect originalMetricsRect = QFontMetrics(originalFont).boundingRect(infoText).translated(textPosition);
		QVERIFY(expectedRect.width() > originalMetricsRect.width());

		QImage expectedBacking = backgroundImage.copy();
		{
			QPainter painter(&expectedBacking);
			painter.fillRect(expectedRect, QColor{0, 0, 0, 192});
		}

		int backingPixelsOutsideOriginalMetrics = 0;
		const QRect visibleExpectedRect = expectedRect.intersected(rendered.rect());
		for (int y = visibleExpectedRect.top(); y <= visibleExpectedRect.bottom(); ++y)
		{
			for (int x = visibleExpectedRect.left(); x <= visibleExpectedRect.right(); ++x)
			{
				const QPoint point{x, y};
				if (!originalMetricsRect.contains(point) &&
					rendered.pixelColor(point) == expectedBacking.pixelColor(point) &&
					rendered.pixelColor(point) != backgroundImage.pixelColor(point))
				{
					++backingPixelsOutsideOriginalMetrics;
				}
			}
		}
		QVERIFY2(backingPixelsOutsideOriginalMetrics > 0,
			"the label background did not cover the adjusted-font area beyond the original metrics");
	}
};

QTEST_MAIN(LfoGraphTest)
#include "LfoGraphTest.moc"
