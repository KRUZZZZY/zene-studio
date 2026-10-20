/*
 * ScaleIntervalOverflowTest.cpp - a stored scale interval must not WRAP
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor
 * Boston, MA 02110-1301 USA.
 *
 */

//! The persisted ratio of a scale interval is read as TEXT and narrowed into
//! the interval's 32-bit members. A stored value above UINT32_MAX must not
//! wrap - 4294967298 is two more than a uint32 can hold, so a narrowing
//! assignment silently turns the stored 4294967298/1 into 2/1 (a ratio of 2.0)
//! and the loaded scale is not the scale the file asks for. The value that
//! cannot be represented uses the cents fallback the class already has.
//!
//! Every value here is injected as the file's own text, so the test is
//! deterministic and platform independent - the same input gives the same
//! reading on a 32-bit and on a 64-bit build.

#include <QtTest>

#include <QDomDocument>

#include "Scale.h"

using namespace lmms;

namespace
{

//! A stored <interval num="..." den="..."> element - the shape a project file
//! carries for a ratio interval.
QDomElement ratioInterval(QDomDocument& document, const QString& numerator, const QString& denominator)
{
	QDomElement element = document.createElement(QStringLiteral("interval"));
	element.setAttribute(QStringLiteral("num"), numerator);
	element.setAttribute(QStringLiteral("den"), denominator);
	return element;
}

//! The stored <interval cents="..."> element - the shape for a cents interval.
QDomElement centsInterval(QDomDocument& document, const QString& cents)
{
	QDomElement element = document.createElement(QStringLiteral("interval"));
	element.setAttribute(QStringLiteral("cents"), cents);
	return element;
}

} // namespace


class ScaleIntervalOverflowTest : public QObject
{
	Q_OBJECT
private slots:

	//! An oversized stored numerator must not wrap: the safe reading of a
	//! ratio that does not fit is the cents fallback, a unison - never the
	//! 2/1 the wrapped 4294967298 would have become.
	void anOversizedStoredIntervalDoesNotWrap()
	{
		QDomDocument document;
		Interval interval;
		interval.restoreState(ratioInterval(document, QStringLiteral("4294967298"), QStringLiteral("1")));
		QCOMPARE(interval.getRatio(), 1.0f);
	}

	//! The denominator wraps just as silently: a stored 3/4294967298 would
	//! become 3/2, a ratio of 1.5.
	void anOversizedStoredDenominatorDoesNotWrap()
	{
		QDomDocument document;
		Interval interval;
		interval.restoreState(ratioInterval(document, QStringLiteral("3"), QStringLiteral("4294967298")));
		QCOMPARE(interval.getRatio(), 1.0f);
	}

	//! A ratio that fits loads exactly as it always did.
	void aStoredRatioInRangeLoadsUnchanged()
	{
		QDomDocument document;
		Interval interval;
		interval.restoreState(ratioInterval(document, QStringLiteral("3"), QStringLiteral("2")));
		QCOMPARE(interval.getRatio(), 1.5f);
	}

	//! The cents shape is untouched by the range check.
	void aStoredCentsIntervalLoadsUnchanged()
	{
		QDomDocument document;
		Interval interval;
		interval.restoreState(centsInterval(document, QStringLiteral("100")));
		QCOMPARE(interval.getRatio(), Interval(100.0f).getRatio());
	}

	//! The project path - the <scale> element of a saved project - holds no
	//! wrapped ratio, and the valid interval next to the oversized one still
	//! loads.
	void aScaleWithAnOversizedIntervalHoldsNoWrappedRatio()
	{
		QDomDocument document;
		QDomElement scaleElement = document.createElement(QStringLiteral("scale"));
		document.appendChild(scaleElement);
		scaleElement.appendChild(ratioInterval(document, QStringLiteral("4294967298"), QStringLiteral("1")));
		scaleElement.appendChild(ratioInterval(document, QStringLiteral("3"), QStringLiteral("2")));

		Scale scale;
		scale.loadSettings(scaleElement);

		bool sawValidInterval = false;
		for (const Interval& loaded : scale.getIntervals())
		{
			QVERIFY2(loaded.getRatio() != 2.0f,
				"the stored 4294967298/1 must not wrap into the ratio 2.0");
			if (loaded.getRatio() == 1.5f) { sawValidInterval = true; }
		}
		QVERIFY2(sawValidInterval, "the valid 3/2 interval must still load");
	}
};

QTEST_GUILESS_MAIN(ScaleIntervalOverflowTest)
#include "ScaleIntervalOverflowTest.moc"
