/*
 * WcagContrastTest.cpp - the WCAG contrast helper at the AA boundary
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
 *
 */

#include <QtTest>

#include "WcagContrast.h"

//! include/WcagContrast.h, pinned to the numbers the standard defines: the
//! endpoints (21:1, 1:1), the published constants (red = 0.2126), symmetry,
//! and - the case the helper exists for - a pair one step either side of the
//! 4.5:1 AA text boundary, so a regression in the piecewise linearisation
//! fails here instead of shipping a theme nobody can read.
class WcagContrastTest : public QObject
{
	Q_OBJECT

private slots:
	void blackOnWhiteIsTheTwentyOneToOneMaximum()
	{
		QVERIFY(qAbs(lmms::a11y::relativeLuminance(Qt::white) - 1.0) < 1e-9);
		QVERIFY(qAbs(lmms::a11y::relativeLuminance(Qt::black)) < 1e-9);
		QVERIFY(qAbs(lmms::a11y::contrastRatio(Qt::black, Qt::white) - 21.0) < 1e-9);
		QVERIFY(qAbs(lmms::a11y::contrastRatio(Qt::white, Qt::white) - 1.0) < 1e-9);
	}

	void luminanceOfPureRedIsTheWcagConstant()
	{
		// The WCAG definition uses exactly 0.2126 / 0.7152 / 0.0722, so a pure
		// red's luminance is that coefficient, not an approximation of it.
		QVERIFY(qAbs(lmms::a11y::relativeLuminance(QColor(255, 0, 0)) - 0.2126) < 1e-4);
		QVERIFY(qAbs(lmms::a11y::relativeLuminance(QColor(0, 255, 0)) - 0.7152) < 1e-4);
		QVERIFY(qAbs(lmms::a11y::relativeLuminance(QColor(0, 0, 255)) - 0.0722) < 1e-4);
	}

	void ratioIsSymmetric()
	{
		const QColor slate(91, 101, 113); // LmmsPalette's fallback background
		const QColor text(224, 224, 224); // LmmsPalette's fallback text
		QVERIFY(qAbs(lmms::a11y::contrastRatio(slate, text)
			- lmms::a11y::contrastRatio(text, slate)) < 1e-12);
	}

	void discriminatesOneStepEitherSideOfTheAaTextBoundary()
	{
		// #767676 on white measures 4.54:1 (passes AA) and #777777 on white
		// 4.48:1 (fails) - the classic pair that catches an off-by-epsilon or a
		// wrong linearisation at exactly the threshold users are held to.
		QVERIFY(lmms::a11y::meetsTextContrast(QColor(QStringLiteral("#767676")),
			Qt::white));
		QVERIFY(!lmms::a11y::meetsTextContrast(QColor(QStringLiteral("#777777")),
			Qt::white));
	}

	void componentThresholdIsThreeToOne()
	{
		QVERIFY(lmms::a11y::meetsComponentContrast(QColor(QStringLiteral("#777777")),
			Qt::white));
		// #999999 on white is ~2.85:1: fine for text's 4.5? no, and short of
		// the 3:1 component floor too - one colour, both verdicts.
		QVERIFY(!lmms::a11y::meetsComponentContrast(QColor(QStringLiteral("#999999")),
			Qt::white));
	}
};

// Pure colour maths: no widgets, no platform plugin, no display.
QTEST_GUILESS_MAIN(WcagContrastTest)

#include "WcagContrastTest.moc"
