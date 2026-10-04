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
#include <QMenu>
#include <QMenuBar>

#include "HelpTasks.h"
#include "ShortcutsPage.h"

using namespace lmms::gui;

class ShortcutsPageTest : public QObject
{
	Q_OBJECT

private slots:
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
};

QTEST_MAIN(ShortcutsPageTest)
#include "ShortcutsPageTest.moc"
