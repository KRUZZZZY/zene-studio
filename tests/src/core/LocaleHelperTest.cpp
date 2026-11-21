/*
 * LocaleHelperTest.cpp - the parse contract of LocaleHelper::toFloat, and the
 * one project-loader leg that feeds it a non-finite value.
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
#include <QDomDocument>
#include <QStringList>

#include <cfloat>
#include <cmath>

#include "AutomatableModel.h"
#include "Engine.h"
#include "LocaleHelper.h"

namespace {

struct LoadedVolume
{
	float value;
	float initValue;
};

// Load one <volume> setting the way a project does, and return the model's value and initValue.
LoadedVolume loadVolume(const QString& xml)
{
	QDomDocument doc;
	doc.setContent(xml);
	lmms::FloatModel model(0.5f, 0.0f, 1.0f, 0.01f);
	model.loadSettings(doc.documentElement(), QStringLiteral("volume"));
	return {model.value(), model.initValue()};
}

} // namespace

class LocaleHelperTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanupTestCase();

	void finiteCAndGermanDecimalsStillParse();
	void nonFiniteTextIsRejected();
	void overflowLiteralsAreRejected();
	void nullOkStillRejects();
	void floatMaxBoundaryStillLoads();
	void projectLoadKeepsNonFiniteOutOfTheModel();
	void projectLoadKeepsNonFiniteOutOfTheAttribute();
};

void LocaleHelperTest::initTestCase()
{
	lmms::Engine::init(true);
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
}

void LocaleHelperTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

void LocaleHelperTest::finiteCAndGermanDecimalsStillParse()
{
	const struct Row { QString text; float expected; } rows[] = {
		{QStringLiteral("1.5"), 1.5f},
		{QStringLiteral("1,5"), 1.5f},
		{QStringLiteral("0"), 0.0f},
		{QStringLiteral("-2.25"), -2.25f},
	};
	for (const Row& row : rows)
	{
		bool ok = false;
		const float value = lmms::LocaleHelper::toFloat(row.text, &ok);
		QVERIFY2(ok, qPrintable(QStringLiteral("'%1' did not parse").arg(row.text)));
		QCOMPARE(value, row.expected);
	}
}

void LocaleHelperTest::nonFiniteTextIsRejected()
{
	const QStringList texts{QStringLiteral("nan"), QStringLiteral("NaN"), QStringLiteral("inf"),
		QStringLiteral("-inf"), QStringLiteral("+inf")};
	for (const QString& text : texts)
	{
		bool ok = true;
		const float value = lmms::LocaleHelper::toFloat(text, &ok);
		QVERIFY2(!ok, qPrintable(QStringLiteral("'%1' was accepted (ok=true)").arg(text)));
		QVERIFY2(std::isfinite(value), qPrintable(QStringLiteral("'%1' returned a non-finite value").arg(text)));
		QCOMPARE(value, 0.0f);
	}
}

void LocaleHelperTest::overflowLiteralsAreRejected()
{
	const QStringList texts{QStringLiteral("1e999"), QStringLiteral("-1e999"), QStringLiteral("3.5e38")};
	for (const QString& text : texts)
	{
		bool ok = true;
		const float value = lmms::LocaleHelper::toFloat(text, &ok);
		QVERIFY2(!ok, qPrintable(QStringLiteral("'%1' was accepted (ok=true)").arg(text)));
		QVERIFY2(std::isfinite(value), qPrintable(QStringLiteral("'%1' returned a non-finite value").arg(text)));
		QCOMPARE(value, 0.0f);
	}
}

void LocaleHelperTest::nullOkStillRejects()
{
	// Callers that pass no ok pointer still get a finite value, never NaN.
	const float value = lmms::LocaleHelper::toFloat(QStringLiteral("nan"));
	QVERIFY2(std::isfinite(value), "toFloat(\"nan\") with a null ok returned a non-finite value");
	QCOMPARE(value, 0.0f);
}

void LocaleHelperTest::floatMaxBoundaryStillLoads()
{
	bool ok = false;
	float value = lmms::LocaleHelper::toFloat(QStringLiteral("1e38"), &ok);
	QVERIFY(ok);
	QCOMPARE(value, 1e38f);

	ok = false;
	value = lmms::LocaleHelper::toFloat(QStringLiteral("3.4028234663852886e38"), &ok);
	QVERIFY2(ok, "FLT_MAX itself was rejected");
	QCOMPARE(value, FLT_MAX);

	ok = true;
	value = lmms::LocaleHelper::toFloat(QStringLiteral("3.4028235e38"), &ok);
	QVERIFY2(!ok, "a value just above FLT_MAX was accepted");
	QCOMPARE(value, 0.0f);
}

void LocaleHelperTest::projectLoadKeepsNonFiniteOutOfTheModel()
{
	const LoadedVolume fromChild = loadVolume(QStringLiteral("<root><volume value=\"nan\"/></root>"));
	QVERIFY2(std::isfinite(fromChild.value), "a child <volume value=\"nan\"/> loaded a non-finite model value");

	QCOMPARE(loadVolume(QStringLiteral("<root><volume value=\"0.25\"/></root>")).value, 0.25f);
}

void LocaleHelperTest::projectLoadKeepsNonFiniteOutOfTheAttribute()
{
	const LoadedVolume fromAttribute = loadVolume(QStringLiteral("<root volume=\"nan\"/>"));
	QVERIFY2(std::isfinite(fromAttribute.value), "an attribute volume=\"nan\" loaded a non-finite model value");
	QVERIFY2(std::isfinite(fromAttribute.initValue), "an attribute volume=\"nan\" loaded a non-finite model initValue");

	const LoadedVolume attribute = loadVolume(QStringLiteral("<root volume=\"0.25\"/>"));
	QCOMPARE(attribute.value, 0.25f);
	QCOMPARE(attribute.initValue, 0.25f);
}

QTEST_GUILESS_MAIN(LocaleHelperTest)
#include "LocaleHelperTest.moc"
