/*
 * SessionTuningMenuTest.cpp - Edit > Session Tuning loads a Scala scale and resets to 12-TET
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

/*! A 3-degree just scale written to a throwaway file: Load Scala Scale... (a cancelled pick loads
 *  nothing) activates the session table with that scale and the line names it; Reset to 12-TET turns
 *  it off again; Publish as MTS-ESP Master is offered exactly when the library is present. */

#include <QtTest>

#include <QAction>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SessionTuningMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject tuning()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("mts.get_state"), QJsonObject{}).result;
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class SessionTuningMenuTest : public QObject
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
		ControlRegistry::instance()->invoke(QStringLiteral("mts.reset"), QJsonObject{});
		setSessionTuningPicker({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aScaleIsLoadedAndResetFromTheMenu()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString scl = dir.filePath(QStringLiteral("ji3.scl"));
		{
			QFile file(scl);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
			file.write("! ji3.scl\n3-note just scale\n3\n1/1\n5/4\n3/2\n");
		}
		QMenu parent;
		QMenu* menu = addSessionTuningMenu(&parent);
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("12-TET (no session table)"));

		setSessionTuningPicker([](const QString&) { return QString(); });
		item(menu, QStringLiteral("Load Scala Scale..."))->trigger();
		QVERIFY(!tuning().value(QStringLiteral("active")).toBool());

		setSessionTuningPicker([scl](const QString& filter) { return filter.contains(QStringLiteral("scl")) ? scl : QString(); });
		item(menu, QStringLiteral("Load Scala Scale..."))->trigger();
		QVERIFY(tuning().value(QStringLiteral("active")).toBool());
		// The line carries the scale's own description line, as mts.get_state reports it.
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("Scale: 3-note just scale"));

		item(menu, QStringLiteral("Reset to 12-TET"))->trigger();
		QVERIFY(!tuning().value(QStringLiteral("active")).toBool());
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("12-TET (no session table)"));

		QAction* master = item(menu, QStringLiteral("Publish as MTS-ESP Master"));
		QVERIFY(master != nullptr);
		QCOMPARE(master->isEnabled(), tuning().value(QStringLiteral("mts_library")).toString() == QStringLiteral("present"));
	}
};

QTEST_MAIN(SessionTuningMenuTest)
#include "SessionTuningMenuTest.moc"
