/*
 * EqCurveTest.cpp - first-paint regression coverage for the EQ response curve.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <QtTest>

#include <array>
#include <cmath>
#include <memory>

#include <QImage>
#include <QPainter>

#include "Engine.h"
#include "EqCurve.h"

using namespace lmms;
using namespace lmms::gui;

class PaintableEqCurve final : public EqCurve
{
public:
	using EqCurve::EqCurve;

	void paintTo(QPainter& painter)
	{
		paint(&painter, nullptr, nullptr);
	}
};

class EqCurveTest final : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void firstPaintBuildsTheRestoredActiveBandCurve()
	{
		constexpr int width = 450;
		constexpr int height = 200;
		std::array<std::unique_ptr<EqHandle>, 8> handles;
		QList<EqHandle*> handleList;
		for (int i = 0; i < static_cast<int>(handles.size()); ++i)
		{
			handles[static_cast<size_t>(i)] = std::make_unique<EqHandle>(i, width, height);
			handleList.append(handles[static_cast<size_t>(i)].get());
		}

		// The widget creates EqCurve before changeHandle() applies the saved band state.
		PaintableEqCurve curve(&handleList, width, height);
		for (int i = 0; i < static_cast<int>(handles.size()); ++i)
		{
			auto* handle = handles[static_cast<size_t>(i)].get();
			handle->setType(EqHandleType::HighShelf);
			handle->setResonance(1.0f);
			handle->setPos(225.0, 100.0);
			handle->setHandleActive(false);
		}

		auto* restoredBand = handles[6].get();
		restoredBand->setPos(225.0, 45.0);
		restoredBand->setHandleActive(true);

		const int sampleX = width - 1;
		const int expectedY = qRound(restoredBand->getHighShelfCurve(sampleX));
		QVERIFY(std::abs(expectedY - height / 2) > 5);

		QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		curve.paintTo(painter);
		painter.end();

		bool foundResponsePixel = false;
		for (int x = sampleX - 1; x <= sampleX; ++x)
		{
			for (int y = expectedY - 2; y <= expectedY + 2; ++y)
			{
				if (image.pixelColor(x, y).alpha() > 0)
				{
					foundResponsePixel = true;
				}
			}
		}
		QVERIFY2(foundResponsePixel,
			"the first paint did not draw the restored active band's response curve");
	}
};

QTEST_MAIN(EqCurveTest)
#include "EqCurveTest.moc"
