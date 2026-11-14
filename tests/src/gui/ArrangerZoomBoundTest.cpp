/*
 * ArrangerZoomBoundTest.cpp - a step zoom at the end of the zoom range says so instead of a silent no-op
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

/*! gui-1106-menu-navigate-zoom-in: Navigate > Zoom In at the maximum zoom (400 px per bar, which the
 *  arranger can inherit from the saved UI settings) answered success and changed nothing, so the menu
 *  closed with no sign of why. Like zoom_back / zoom_forward with nothing to walk, a step zoom already at
 *  the end it steps toward is now refused, and the menu floats the reason. A step that still moves (even
 *  one clamped onto the bound), a zero step and a pixels_per_bar zoom to the current value stay successes. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>

#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuBarExtras.h"
#include "SongEditor.h"
#include "TextFloat.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The floating messages on screen (a TextFloat is a top-level window of its own).
QString floated()
{
	QString text;
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		if (dynamic_cast<TextFloat*>(window) == nullptr) { continue; }
		for (QLabel* label : window->findChildren<QLabel*>()) { text += label->text() + QLatin1Char(' '); }
	}
	return text;
}

void closeFloats()
{
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		if (dynamic_cast<TextFloat*>(window) != nullptr) { window->close(); delete window; }
	}
}

QAction* navigateAction(const QString& text)
{
	QMenu* menu = installNavigateMenu(getGUI()->mainWindow()->menuBar());
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class ArrangerZoomBoundTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		ControlRegistry::instance()->setHeadless(false);  // the offscreen platform is a display: a refusal is shown
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { closeFloats(); }

	//! The finding's repro: the arranger at 400 px per bar, then Navigate > Zoom In.
	void zoomInAtTheMaximumIsRefusedAndSaysSo()
	{
		IntModel* model = m_editor->zoomingModel();
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 400}}).ok);
		QCOMPARE(model->value(), model->maxValue());

		const ControlResult refused = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}});
		QVERIFY2(!refused.ok, "a zoom in at the maximum answered success without changing the view");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("maximum")), qPrintable(refused.errorMessage));
		QCOMPARE(model->value(), model->maxValue());

		QAction* zoomIn = navigateAction(QObject::tr("Zoom In"));
		QVERIFY(zoomIn != nullptr);
		zoomIn->trigger();
		QCoreApplication::processEvents();
		QVERIFY2(floated().contains(QStringLiteral("maximum")), qPrintable(QStringLiteral("floated: '%1'").arg(floated())));
		QCOMPARE(model->value(), model->maxValue());
	}

	//! The mirror end: Zoom Out at the minimum.
	void zoomOutAtTheMinimumIsRefused()
	{
		IntModel* model = m_editor->zoomingModel();
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 4}}).ok);
		QCOMPARE(model->value(), model->minValue());
		const ControlResult refused = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -4}});
		QVERIFY2(!refused.ok, "a zoom out at the minimum answered success without changing the view");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("minimum")), qPrintable(refused.errorMessage));
		QCOMPARE(model->value(), model->minValue());
		// Zooming the other way from the bound still works.
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}}).ok);
		QCOMPARE(model->value(), model->minValue() + 4);
	}

	//! What still succeeds: a step clamped onto the bound (it moves), a zero step, and an absolute zoom to
	//! the value already in force (the presets and the view memory restore).
	void movingClampedZeroAndAbsoluteZoomsStaySuccesses()
	{
		IntModel* model = m_editor->zoomingModel();
		model->setValue(model->maxValue() - 1);
		const ControlResult clamped = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}});
		QVERIFY2(clamped.ok, qPrintable(clamped.errorMessage));
		QCOMPARE(model->value(), model->maxValue());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 0}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 400}}).ok);
		QCOMPARE(model->value(), model->maxValue());
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
};

QTEST_MAIN(ArrangerZoomBoundTest)
#include "ArrangerZoomBoundTest.moc"
