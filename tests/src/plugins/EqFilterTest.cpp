/*
 * EqFilterTest.cpp - edge-frequency behaviour of the equalizer filters
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
 */

#include <QtTest>

#include <cmath>

#include "SampleFrame.h"
#include "../../../plugins/Eq/EqFilter.h"

class EqFilterTest : public QObject
{
	Q_OBJECT

private slots:
	void peakFilterAtNyquistProducesFiniteAudio()
	{
		lmms::EqPeakFilter filter;
		filter.setParameters(32000.0f, 16000.0f, 0.3f, 12.0f);

		QVERIFY(std::isfinite(filter.update(1.0f, 0, 1.0f)));
	}
};

QTEST_APPLESS_MAIN(EqFilterTest)

#include "EqFilterTest.moc"
