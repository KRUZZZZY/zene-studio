/*
 * TempoMapPanelTest.cpp - Edit > Tempo Map lists, adds, removes, activates and clears
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

/*! The panel over a real engine, every edit through its transport.tempo_map_* command: an event
 *  added at a tick appears as a row (with its bar) and in transport.tempo_map_get; the Active box
 *  follows and sets the map's switch; Remove deletes the selected row's event; Clear empties it. */

#include <QtTest>

#include <QCheckBox>
#include <QJsonArray>
#include <QPushButton>
#include <QTableWidget>

#include "ControlRegistry.h"
#include "Engine.h"
#include "TempoMapPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject mapState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_get"), QJsonObject{}).result;
}

} // namespace

class TempoMapPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanelEditsTheMapThroughTheCommands()
	{
		TempoMapPanel panel;
		QCOMPARE(panel.rowCount(), 0);
		QVERIFY(panel.addEvent(0, 120));
		QVERIFY(panel.addEvent(4 * 192, 90));  // bar 5
		QCOMPARE(panel.rowCount(), 2);
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 2);
		auto* table = panel.findChild<QTableWidget*>();
		QCOMPARE(table->item(1, 0)->text(), QStringLiteral("5"));
		QCOMPARE(table->item(1, 2)->text(), QStringLiteral("90"));

		auto* active = panel.findChild<QCheckBox*>();
		const bool wasActive = mapState().value(QStringLiteral("active")).toBool();
		QCOMPARE(active->isChecked(), wasActive);
		active->setChecked(!wasActive);
		QCOMPARE(mapState().value(QStringLiteral("active")).toBool(), !wasActive);

		QVERIFY(panel.removeRow(1));
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 1);
		QCOMPARE(panel.rowCount(), 1);

		for (QPushButton* button : panel.findChildren<QPushButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("transport.tempo_map_clear")) { button->click(); }
		}
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 0);
		QCOMPARE(panel.rowCount(), 0);
	}
};

QTEST_MAIN(TempoMapPanelTest)
#include "TempoMapPanelTest.moc"
