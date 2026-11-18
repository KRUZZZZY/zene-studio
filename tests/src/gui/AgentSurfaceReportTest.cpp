/*
 * AgentSurfaceReportTest.cpp - control.surface_report names every menu action with its whole container path
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
/*! control.surface_report is the agent's map of the window (SPEC A15): every action, the command id it
 *  declares, and the container it lives in. An action under File -> Export must be reported as living
 *  under "File/Export": the agent gate keys on "surface:container_path/text" and exempts whole subtrees by
 *  a "path:" prefix, so a container path that drops a parent menu names an action the agent cannot find
 *  and cannot exempt. Luna cluster 4016. */

#include <QtTest>

#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"

using namespace lmms;
using namespace lmms::gui;

//! A menu's container name, spelled out here rather than borrowed from the walker so the test asserts
//! the naming contract and not the implementation.
static QString menuLabel(const QMenu* menu)
{
	QString title = menu->title();
	const int tab = title.indexOf(QLatin1Char('\t'));
	if (tab >= 0) { title.truncate(tab); }
	title.remove(QLatin1Char('&'));
	title = title.simplified();
	if (!title.isEmpty()) { return title; }
	if (!menu->objectName().isEmpty()) { return menu->objectName(); }
	QString className = QString::fromLatin1(menu->metaObject()->className());
	const int separator = className.lastIndexOf(QStringLiteral("::"));
	return separator >= 0 ? className.mid(separator + 2) : className;
}

//! An action's label as a human reads it: the shortcut suffix is not part of it.
static QString actionText(const QAction* action)
{
	QString text = action->text();
	const int tab = text.indexOf(QLatin1Char('\t'));
	if (tab >= 0) { text.truncate(tab); }
	text.remove(QLatin1Char('&'));
	return text.simplified();
}

static void collectExpected(const QList<QAction*>& actions, const QString& parentPath, QJsonArray& out)
{
	for (QAction* action : actions)
	{
		if (action == nullptr || action->isSeparator()) { continue; }
		if (QMenu* submenu = action->menu())
		{
			const QString label = menuLabel(submenu);
			collectExpected(submenu->actions(),
				parentPath.isEmpty() ? label : parentPath + QLatin1Char('/') + label, out);
			continue;
		}
		out.append(QJsonObject{
			{QStringLiteral("container_path"), parentPath},
			{QStringLiteral("text"), actionText(action)}});
	}
}

//! Does the reported surface name this action, under this whole container path?
static bool reportHas(const QJsonArray& actions, const QString& path, const QString& text)
{
	for (const QJsonValue& value : actions)
	{
		const QJsonObject item = value.toObject();
		if (item.value(QStringLiteral("surface")).toString() == QLatin1String("menu")
			&& item.value(QStringLiteral("container_path")).toString() == path
			&& item.value(QStringLiteral("text")).toString() == text)
		{
			return true;
		}
	}
	return false;
}

class AgentSurfaceReportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Luna cluster 4016: recursing into a submenu used to replace the container path with that submenu's
	//! own label, so every action deeper than the top menu reported only "Export" instead of "File/Export".
	void nestedActionsKeepTheirWholeMenuPath()
	{
		MainWindow* window = getGUI()->mainWindow();
		QVERIFY(window->menuBar() != nullptr);

		QJsonArray expected;
		for (QAction* action : window->menuBar()->actions())
		{
			if (action != nullptr && action->menu() != nullptr)
			{
				collectExpected(action->menu()->actions(), menuLabel(action->menu()), expected);
			}
		}

		// non-vacuity: the window really does have actions below a submenu
		int nested = 0;
		for (const QJsonValue& value : expected)
		{
			if (value.toObject().value(QStringLiteral("container_path")).toString().contains(QLatin1Char('/')))
			{
				++nested;
			}
		}
		QVERIFY2(nested > 0, "no nested menu action was found, so this test would pass vacuously");

		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);
		const ControlResult report = registry->invoke(QStringLiteral("control.surface_report"));
		registry->setHeadless(headless);
		QVERIFY2(report.ok, qPrintable(report.errorMessage));

		const QJsonArray actions = report.result.value(QStringLiteral("actions")).toArray();
		for (const QJsonValue& value : expected)
		{
			const QJsonObject want = value.toObject();
			const QString path = want.value(QStringLiteral("container_path")).toString();
			const QString text = want.value(QStringLiteral("text")).toString();
			QVERIFY2(reportHas(actions, path, text),
				qPrintable(QStringLiteral("surface_report lost menu action \"%1\" in \"%2\"").arg(text, path)));
		}
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(AgentSurfaceReportTest)
#include "AgentSurfaceReportTest.moc"
