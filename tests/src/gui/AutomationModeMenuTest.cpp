/*
 * AutomationModeMenuTest.cpp - every control's context menu sets its automation mode
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

/*! KNOWN-LIMITATIONS "automation modes are drivable through the socket but not from the
 *  interface": a control's own context menu (AutomatableModelView::addDefaultActions) now ends
 *  with an "Automation mode" submenu. Against a REAL instrument's parameter: the submenu is
 *  there, lists the five modes with the current one checked, and choosing one sets the
 *  parameter's mode through automation.mode_set - what automation.get_state then reports. A
 *  model the command surface cannot name gets no submenu. */

#include <QtTest>

#include <QMenu>

#include "../core/ReversibilityTestSupport.h"
#include "AutomatableModel.h"
#include "AutomationModeMenu.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Knob.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QMenu* modeMenuOf(QMenu& menu)
{
	for (QAction* action : menu.actions())
	{
		if (action->menu() != nullptr && action->data().toString() == QLatin1String("automation.mode_set")) { return action->menu(); }
	}
	return nullptr;
}

QString checkedLabel(QMenu* modes)
{
	for (QAction* action : modes->actions())
	{
		if (action->isChecked()) { return action->text(); }
	}
	return QString();
}

} // namespace

class AutomationModeMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = revtest::addInstrumentTrack();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theContextMenuSetsTheMode()
	{
#ifdef Q_OS_WIN
		if (m_track.isEmpty()) { QSKIP("plugin modules cannot load in a Windows test host (ControlAutomationModesTest)"); }
#endif
		QVERIFY2(!m_track.isEmpty(), "no instrument to drive (LMMS_TEST_PLUGIN_DIR)");
		ControlTarget target;
		ControlResult error;
		QVERIFY(resolveControlTarget(m_track, &target, &error));
		const QList<control::AutomationParameter> parameters = control::automationParameters(target);
		QVERIFY(!parameters.isEmpty());
		AutomatableModel* model = parameters.first().model;
		auto* floatModel = dynamic_cast<FloatModel*>(model);
		QVERIFY2(floatModel != nullptr, "the first instrument parameter is not a float");

		// The real context menu: the one a knob on this parameter builds.
		Knob knob(KnobType::Bright26);
		knob.setModel(floatModel);
		QMenu menu;
		knob.addDefaultActions(&menu);
		QMenu* modes = modeMenuOf(menu);
		QVERIFY2(modes != nullptr, "the control's context menu has no Automation mode submenu");
		QCOMPARE(modes->actions().size(), 5);
		QVERIFY(checkedLabel(modes).startsWith(QStringLiteral("Read")));

		modes->actions().at(2)->trigger();  // Touch
		QCOMPARE(model->automationMode(), AutomatableModel::AutomationMode::Touch);
		QMenu again;
		knob.addDefaultActions(&again);
		QVERIFY(checkedLabel(modeMenuOf(again)).startsWith(QStringLiteral("Touch")));
		modeMenuOf(again)->actions().at(1)->trigger();  // back to Read
		QCOMPARE(model->automationMode(), AutomatableModel::AutomationMode::Read);
	}

	void aControlTheSocketCannotNameGetsNoSubmenu()
	{
		FloatModel loose(0.f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Loose"));
		QMenu menu;
		addAutomationModeMenu(&menu, &loose);
		QVERIFY(menu.actions().isEmpty());
	}

private:
	QString m_track;
};

QTEST_MAIN(AutomationModeMenuTest)
#include "AutomationModeMenuTest.moc"
