/*
 * XyPadPairTest.cpp - W1: an XY pad drag is one undo step, and a refused move is shown
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

/*! The pad over a real channel rack with two macros: a drag of several moves sets both macros and ONE control.undo puts
 *  both back (it used to be one step per axis per move); with the Y macro removed by another client while the pad is open,
 *  a move is refused whole - X does not move either - and the pad's status line names the refusal. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "XyPad.h"
#include "../../../plugins/Delay/DelayControlsDialog.h"

#define PLUGIN_NAME delay
#include "../../../plugins/Delay/DelayControlsDialog.cpp"
#undef PLUGIN_NAME
#include "moc_DelayControlsDialog.cpp"
#include "moc_EqFader.cpp"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

double macroValue(const QString& channel, const QString& macro)
{
	for (const QJsonValue& entry : run(QStringLiteral("rack.get_state"), {{QStringLiteral("channel"), channel}})
		.result.value(QStringLiteral("macros")).toArray())
	{
		if (entry.toObject().value(QStringLiteral("macro")).toString() == macro) { return entry.toObject().value(QStringLiteral("value")).toDouble(); }
	}
	return -1.0;
}

QLabel* statusOf(XyPadDialog& dialog)
{
	for (QLabel* label : dialog.findChildren<QLabel*>())
	{
		if (label->accessibleName() == QLatin1String("XY pad status")) { return label; }
	}
	return nullptr;
}

struct DelayXyPadFixture
{
	FloatModel xModel{0.2f, 0.f, 1.f, 0.001f};
	FloatModel yModel{0.3f, 0.f, 1.f, 0.001f};
	XyPad pad{nullptr, &xModel, &yModel};

	DelayXyPadFixture()
	{
		pad.resize(100, 100);
		pad.show();
		QCoreApplication::processEvents();
	}
};

} // namespace

class XyPadPairTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		while (Engine::mixer()->numChannels() <= 1) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void delayXyPadIgnoresRightDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QTest::mouseRelease(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.2f);
		QCOMPARE(fixture.yModel.value(), 0.3f);
	}

	void delayXyPadIgnoresMiddleDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::MiddleButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QTest::mouseRelease(&fixture.pad, Qt::MiddleButton, Qt::NoModifier, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.2f);
		QCOMPARE(fixture.yModel.value(), 0.3f);
	}

	void delayXyPadLeftDragUpdatesBothModels()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.8f);
		QCOMPARE(fixture.yModel.value(), 0.7f);
		QTest::mouseRelease(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(80, 70));
	}

	void delayXyPadIgnoresOtherButtonReleaseDuringLeftDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mousePress(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseRelease(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.8f);
		QCOMPARE(fixture.yModel.value(), 0.7f);
		QTest::mouseRelease(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(80, 70));
	}

	void aDragIsOneStepAndARefusalIsShown()
	{
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(1));
		for (const char* name : {"Cutoff", "Drive"})
		{
			QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QLatin1String(name)}}).ok);
		}
		const double x0 = macroValue(channel, QStringLiteral("macro-0"));
		const double y0 = macroValue(channel, QStringLiteral("macro-1"));
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 60000}}).ok);

		XyPadDialog dialog(channel);
		auto* area = dialog.findChild<XyPadArea*>();
		QLabel* status = statusOf(dialog);
		QVERIFY(area != nullptr && area->moved && status != nullptr);
		QCOMPARE(area->property("controlCommand").toString(), QStringLiteral("rack.macro_set_pair"));
		for (const QPointF point : {QPointF(0.2, 0.8), QPointF(0.25, 0.85), QPointF(0.3, 0.9)}) { area->moved(point); }
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - 0.3) < 1e-4);
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-1")) - 0.9) < 1e-4);
		QVERIFY(status->text().isEmpty());

		const ControlResult undo = run(QStringLiteral("control.undo"));  // ONE undo: the whole drag, both axes
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - x0) < 1e-4);
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-1")) - y0) < 1e-4);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), control::UndoCoalesceWindowMs}}).ok);

		// Another client removes the Y macro while the pad is open: the next move is refused whole, and says so.
		QVERIFY(run(QStringLiteral("rack.macro_remove"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-1")}}).ok);
		area->moved(QPointF(0.7, 0.7));
		QVERIFY2(status->text().startsWith(QStringLiteral("Not set")), qPrintable(status->text()));
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - x0) < 1e-4);  // X did not move alone
	}
};

QTEST_MAIN(XyPadPairTest)
#include "XyPadPairTest.moc"
