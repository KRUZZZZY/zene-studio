/*
 * ShortcutsPageTest.cpp - M3 item 4: the shortcuts page lists what the menus bind
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

#include <QAction>
#include <QFile>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "HelpTasks.h"
#include "MainWindow.h"
#include "ShortcutsPage.h"

using namespace lmms;
using namespace lmms::gui;

class ShortcutsPageTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		QTest::qWaitForWindowExposed(getGUI()->mainWindow());
		QTest::qWait(100);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void itListsEveryBoundActionAndOnlyThose()
	{
		QMenuBar bar;
		QMenu* file = bar.addMenu(QStringLiteral("&File"));
		file->addAction(QStringLiteral("&Save"))->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
		file->addAction(QStringLiteral("Export..."));  // no shortcut: not listed
		QMenu* view = bar.addMenu(QStringLiteral("&View"));
		QAction* palette = view->addAction(QStringLiteral("Command Palette"));
		palette->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
		palette->setProperty("controlCommand", QStringLiteral("window.command_palette"));

		const QList<PaletteEntry> entries = shortcutEntries(&bar);
		QCOMPARE(entries.size(), 2);
		QCOMPARE(entries[0].path.left(4), QStringLiteral("File"));  // ordered by menu
		QVERIFY(entries[1].command == QStringLiteral("window.command_palette"));

		ShortcutsPage page(&bar);
		QCOMPARE(page.rowCount(), 2);
		page.setFilter(QStringLiteral("palette"));
		QCOMPARE(page.visibleRowCount(), 1);
		page.setFilter(QStringLiteral("ctrl+s"));
		QCOMPARE(page.visibleRowCount(), 2);  // "Ctrl+S" and "Ctrl+Shift+P" both contain it
		page.setFilter(QString());
		QCOMPARE(page.visibleRowCount(), 2);
	}

	//! Design theme 21: the shortcuts are grouped by task - the manual's sections, in its order, under its own
	//! titles - and found by a task's name and by a command's id as well as by what the columns show.
	void theShortcutsAreGroupedByTask()
	{
		QMenuBar bar;
		QMenu* file = bar.addMenu(QStringLiteral("&File"));
		file->addAction(QStringLiteral("&Save"))->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
		QMenu* view = bar.addMenu(QStringLiteral("&View"));
		QAction* mixer = view->addAction(QStringLiteral("Mixer"));
		mixer->setShortcut(QKeySequence(QStringLiteral("F9")));
		mixer->setProperty("controlCommand", QStringLiteral("mixer.get_state"));

		ShortcutsPage page(&bar);
		QCOMPARE(page.rowCount(), 2);
		QCOMPARE(page.taskOf(0), lmms::gui::help::topicTitle(QStringLiteral("mix")));  // Mixing comes before Projects
		QCOMPARE(page.taskOf(1), lmms::gui::help::topicTitle(QStringLiteral("projects")));
		page.setFilter(lmms::gui::help::topicTitle(QStringLiteral("projects")));
		QCOMPARE(page.visibleRowCount(), 1);
		page.setFilter(QStringLiteral("mixer.get_state"));
		QCOMPARE(page.visibleRowCount(), 1);
		QVERIFY(lmms::gui::help::menuTopic(QString(), QStringLiteral("Help ▸ Manual")) == QStringLiteral("access"));
	}

	//! Q050-202: the page offers the shipped maps, and choosing one goes through the registered command.
	void thePageAppliesAShippedKeyboardMap()
	{
		ShortcutsPage page(getGUI()->mainWindow()->menuBar());
		auto* button = page.findChild<QPushButton*>(QStringLiteral("shortcutsPresetButton"));
		QVERIFY(button != nullptr);
		QVERIFY(button->menu() != nullptr);
		QCOMPARE(button->menu()->actions().size(), 4);
		QAction* accessibility = nullptr;
		for (QAction* action : button->menu()->actions())
		{
			if (action->objectName() == QLatin1String("shortcutPreset_accessibility")) { accessibility = action; }
		}
		QVERIFY(accessibility != nullptr);
		QCOMPARE(accessibility->property("controlCommand").toString(), QStringLiteral("shortcuts.preset_apply"));
		accessibility->trigger();
		bool found = false;
		for (const QJsonValue& value : ControlRegistry::instance()->invoke(QStringLiteral("shortcuts.list")).result
			.value(QStringLiteral("bindings")).toArray())
		{
			const QJsonObject row = value.toObject();
			if (row.value(QStringLiteral("command")).toString() == QLatin1String("transport.record"))
			{
				found = true;
				QCOMPARE(row.value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+R"));
			}
		}
		QVERIFY(found);
	}

	//! Q050-202: the manual has setup guidance for the three supported readers and links the
	//! honestly-marked workflow matrix.
	void theManualNamesTheReadersAndLinksTheTestedWorkflowMatrix()
	{
		QFile manual(QStringLiteral("../../data/manual/index.html"));
		QVERIFY(manual.open(QIODevice::ReadOnly));
		const QString html = QString::fromUtf8(manual.readAll());
		for (const QString& reader : {QStringLiteral("NVDA"), QStringLiteral("VoiceOver"), QStringLiteral("Orca")})
		{
			QVERIFY2(html.contains(reader), qPrintable(reader));
		}
		const QString url = QStringLiteral("https://github.com/KRUZZZZY/zene-studio/blob/main/docs/ACCESSIBILITY-TEST-MATRIX.md");
		QVERIFY(html.contains(url));
		QFile matrix(QStringLiteral("../../docs/ACCESSIBILITY-TEST-MATRIX.md"));
		QVERIFY(matrix.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(matrix.readAll());
		for (const QString& row : {QStringLiteral("NVDA"), QStringLiteral("VoiceOver"), QStringLiteral("Orca"),
			QStringLiteral("Not tested in this environment")})
		{
			QVERIFY2(text.contains(row), qPrintable(row));
		}
	}

	void importRejectsTwoShortcutSources()
	{
		const QString path = m_home.filePath(QStringLiteral("shortcuts.json"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray bytes = QJsonDocument(QJsonObject{
			{QStringLiteral("format"), QStringLiteral("zene-shortcuts")},
			{QStringLiteral("version"), 1},
			{QStringLiteral("overrides"), QJsonObject{{QStringLiteral("menu.save"), QStringLiteral("Ctrl+S")}}}}).toJson();
		QCOMPARE(file.write(bytes), qint64(bytes.size()));

		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("shortcuts.import"),
			{{QStringLiteral("file"), path},
				{QStringLiteral("overrides"), QJsonObject{{QStringLiteral("menu.save"), QStringLiteral("Ctrl+Shift+K")}}},
				{QStringLiteral("replace"), true}});
		QVERIFY2(!result.ok, qPrintable(QStringLiteral("shortcuts.import accepted both sources: ") + result.errorMessage));
		QVERIFY(result.errorKind == ControlErrorKind::InvalidArgs);
		QCOMPARE(result.errorMessage, QStringLiteral("give 'file' or 'overrides', not both"));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ShortcutsPageTest)
#include "ShortcutsPageTest.moc"
